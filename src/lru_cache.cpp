#include "lru_cache.h"
#include <iostream>
#include <utility>
#include <chrono>
#include <cstddef>
#include <sstream> // For parsing WAL
#include <vector>  // For splitting strings

// --- Constructor ---
LRUCache::LRUCache(std::size_t cap, int ttl) : capacity(cap), ttl_seconds(ttl) {
    if (capacity == 0) {
       std::cerr << "Warning: Invalid cache capacity 0. Setting to 1." << std::endl;
       capacity = 1;
    }
    head = new Node("", "");
    tail = new Node("", "");
    head->next = tail;
    tail->prev = head;
    wal_stream_ = nullptr; // Ensure WAL is initially off
}

// --- Destructor ---
LRUCache::~LRUCache() {
    // WAL stream is managed externally (e.g., in server main), just clear pointer
    wal_stream_ = nullptr;

    Node* current = head->next;
    while (current != tail) {
        Node* toDelete = current;
        current = current->next;
        delete toDelete;
    }
    delete head;
    delete tail;
}

// --- WAL Stream Setter ---
void LRUCache::setWalStream(std::ofstream* stream) {
    std::lock_guard<std::mutex> lock(mtx); // Lock while changing stream pointer
    wal_stream_ = stream;
}

// --- Internal Logging Helper ---
// Assumes lock is held
bool LRUCache::writeLogEntry(const std::string& entry) {
    if (!wal_stream_) {
        return true; // WAL disabled, treat as success
    }
    *wal_stream_ << entry << std::endl; // Append newline automatically
    if (!wal_stream_->good()) {
        std::cerr << "ERROR: Failed to write to WAL file!" << std::endl;
        // In a real system, might try to reopen/recover or stop accepting writes
        return false;
    }
    wal_stream_->flush(); // Flush buffer to OS (not necessarily to disk)
    return wal_stream_->good();
}


// --- Internal Sync Methods (Modified for WAL) ---

// Return optional string: empty optional if not found/expired
std::optional<std::string> LRUCache::get_sync(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = cache.find(key);
    if (it == cache.end()) {
        return std::nullopt; // Not found
    }
    Node* node = it->second;
    if (isExpired(node)) {
        // Don't log expiration, just remove internally
        removeInternal(node); // removeInternal deletes the node
        return std::nullopt; // Expired
    }
    moveToHead(node);
    node->timestamp = std::chrono::steady_clock::now(); // Reset TTL on access
    return node->value; // Found
}

bool LRUCache::put_sync(const std::string& key, const std::string& value, bool is_recovery) {
    std::lock_guard<std::mutex> lock(mtx);
    Node* existing_node = nullptr;
    auto it = cache.find(key);
    if (it != cache.end()) {
        existing_node = it->second;
        if (isExpired(existing_node)) {
            // Treat expired node during put as if it wasn't there
            removeInternal(existing_node); // Remove old expired node
            existing_node = nullptr; // Reset pointer
        }
    }

    // --- Log BEFORE changing state (if not in recovery) ---
    if (!is_recovery) {
        // Format: PUT,key,value (simple CSV-like)
        // Need basic escaping if key/value can contain commas/newlines
        // For simplicity, assume they don't for now.
        std::string log_entry = "PUT," + key + "," + value;
        if (!writeLogEntry(log_entry)) {
            return false; // WAL write failed, abort operation
        }
    }

    // --- Apply change to memory ---
    if (existing_node) {
        // Update existing node
        existing_node->value = value;
        existing_node->timestamp = std::chrono::steady_clock::now();
        moveToHead(existing_node);
    } else {
        // Insert new node, handle eviction if necessary
        if (cache.size() >= capacity) {
            Node* tailNode = popTail(); // Removes from list
            if (tailNode != nullptr) {
                // Need to log eviction? No, WAL replays puts, eviction happens naturally.
                cache.erase(tailNode->key); // Remove from map
                delete tailNode;            // Delete node data
            }
        }
        Node* newNode = new Node(key, value);
        cache[key] = newNode;
        addNodeToHead(newNode); // Add to list
    }
    return true; // Success
}

bool LRUCache::remove_sync(const std::string& key, bool is_recovery) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = cache.find(key);
    if (it == cache.end()) {
        return true; // Key doesn't exist, removal is trivially successful
    }

    Node* node_to_remove = it->second;
    // Check expiration? If expired, maybe don't log DEL? Let's log DEL always for simplicity.

    // --- Log BEFORE changing state (if not in recovery) ---
    if (!is_recovery) {
        // Format: DEL,key
        std::string log_entry = "DEL," + key;
         if (!writeLogEntry(log_entry)) {
            return false; // WAL write failed, abort operation
        }
    }

    // --- Apply change to memory ---
    removeInternal(node_to_remove); // Removes from map/list and deletes node
    return true; // Success
}

// --- Public API Wrappers ---
// These now just call the internal sync methods
std::optional<std::string> LRUCache::get(const std::string& key) {
    return get_sync(key);
}

bool LRUCache::put(const std::string& key, const std::string& value) {
    return put_sync(key, value, false); // 'false' means it's NOT recovery
}

bool LRUCache::remove(const std::string& key) {
    return remove_sync(key, false); // 'false' means it's NOT recovery
}


// --- WAL Recovery Method ---
// Helper to split string (basic version)
static std::vector<std::string> splitString(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

bool LRUCache::loadFromWAL(const std::string& wal_filename, LRUCache& cache_instance) {
    std::ifstream wal_file(wal_filename);
    if (!wal_file.is_open()) {
        // File might not exist on first run, which is okay.
        if (errno == ENOENT) {
             std::cout << "WAL file '" << wal_filename << "' not found. Starting with empty cache." << std::endl;
             return true;
        }
        std::cerr << "ERROR: Could not open WAL file '" << wal_filename << "' for reading." << std::endl;
        return false; // Indicate failure
    }

    std::cout << "Loading cache state from WAL file: " << wal_filename << std::endl;
    std::string line;
    int line_num = 0;
    int applied_puts = 0;
    int applied_dels = 0;
    while (std::getline(wal_file, line)) {
        line_num++;
        if (line.empty()) continue; // Skip empty lines

        std::vector<std::string> parts = splitString(line, ',');
        if (parts.empty()) {
             std::cerr << "Warning: Skipping empty or invalid line " << line_num << " in WAL." << std::endl;
             continue;
        }

        std::string& op = parts[0];
        if (op == "PUT" && parts.size() == 3) {
            // Call put_sync with is_recovery = true
            if (cache_instance.put_sync(parts[1], parts[2], true)) {
                 applied_puts++;
            } else {
                 std::cerr << "Error applying PUT from WAL line " << line_num << std::endl;
                 // Decide: stop recovery or continue? Let's continue for now.
            }
        } else if (op == "DEL" && parts.size() == 2) {
             // Call remove_sync with is_recovery = true
            if (cache_instance.remove_sync(parts[1], true)) {
                applied_dels++;
            } else {
                 std::cerr << "Error applying DEL from WAL line " << line_num << std::endl;
            }
        } else {
            std::cerr << "Warning: Skipping unrecognized or malformed WAL entry at line "
                      << line_num << ": " << line << std::endl;
        }
    }

    std::cout << "WAL recovery complete. Applied " << applied_puts << " PUTs and "
              << applied_dels << " DELs." << std::endl;
    wal_file.close();
    return true;
}


// --- Helper Methods (Unchanged, but need lock acquisition) ---
void LRUCache::addNodeToHead(Node* node) {
    // Assumes lock is held
    node->next = head->next;
    node->prev = head;
    head->next->prev = node;
    head->next = node;
}

void LRUCache::removeNodeFromList(Node* node) {
    // Assumes lock is held
    if (node == nullptr || node->prev == nullptr || node->next == nullptr) return;
    node->prev->next = node->next;
    node->next->prev = node->prev;
}

void LRUCache::moveToHead(Node* node) {
    // Assumes lock is held
    removeNodeFromList(node);
    addNodeToHead(node);
}

Node* LRUCache::popTail() {
    // Assumes lock is held
    if (tail->prev == head) return nullptr;
    Node* lastNode = tail->prev;
    removeNodeFromList(lastNode);
    return lastNode;
}

// Combined removal from map/list and deletion
void LRUCache::removeInternal(Node* node) {
    // Assumes lock is held
    if (node == nullptr) return;
    cache.erase(node->key); // Remove from map first
    removeNodeFromList(node); // Then from list
    delete node; // Free memory
}

bool LRUCache::isExpired(const Node* node) const {
    // Assumes lock is held (or called from method holding lock)
    if (ttl_seconds <= 0) return false;
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - node->timestamp);
    return duration.count() > ttl_seconds;
}

void LRUCache::print() const {
    std::lock_guard<std::mutex> lock(mtx); // Use mutable mtx
    Node* current = head->next;
    std::cout << "Cache State (Head -> Tail): [ ";
    while (current != tail) {
        std::cout << "(" << current->key << ": " << current->value << ") ";
        current = current->next;
    }
    std::cout << "]" << std::endl;
}