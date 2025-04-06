#include <iostream>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <vector>
#include <chrono>
#include <ctime>
#include <string> // Include string header
#include <functional> // Include functional for std::ref

using namespace std; // Be mindful of using namespace std in headers

struct Node {
    string key, value;
    Node* prev;
    Node* next;
    std::chrono::steady_clock::time_point timestamp; // Timestamp for TTL
    Node(string k, string v) : key(k), value(v), prev(nullptr), next(nullptr), timestamp(std::chrono::steady_clock::now()) {}
};

class LRUCache {
private:
    int capacity;
    unordered_map<string, Node*> cache;
    Node* head; // Dummy head
    Node* tail; // Dummy tail
    std::mutex mtx; // Mutex protecting cache data structures
    int ttl_seconds; // Time-to-live in seconds

    // --- Helper methods (assume mtx is already held by caller) ---

    // Links node right after head
    void addNodeToHead(Node* node) {
        // cout << "Adding node to head: " << node->key << endl; // Debug
        node->next = head->next;
        node->prev = head;
        head->next->prev = node;
        head->next = node;
    }

    // Unlinks node from the list
    void removeNodeFromList(Node* node) {
        if (node == nullptr || node->prev == nullptr || node->next == nullptr) {
            // Should not happen with valid internal calls, but good for robustness
            // cout << "Attempted to remove an invalid or disconnected node." << endl; // Debug
            return;
        }
        // cout << "Removing node from list: " << node->key << endl; // Debug
        node->prev->next = node->next;
        node->next->prev = node->prev;
        // Optional: Null out pointers of the removed node
        // node->prev = nullptr;
        // node->next = nullptr;
    }

    // Moves an existing node to the head (assumes node is already in list)
    void moveToHead(Node* node) {
        // cout << "Moving node to head: " << node->key << endl; // Debug
        removeNodeFromList(node);
        addNodeToHead(node);
    }

    // Removes and returns the tail node (the least recently used)
    // Returns nullptr if list is empty
    Node* popTail() {
        if (tail->prev == head) {
            // cout << "List is empty, nothing to pop." << endl; // Debug
            return nullptr; // List is empty
        }
        Node* lastNode = tail->prev;
        // cout << "Popping tail node: " << lastNode->key << endl; // Debug
        removeNodeFromList(lastNode);
        return lastNode;
    }

    // *** Core removal logic - DOES NOT LOCK ***
    // Should only be called when mtx is already held
    void removeInternal(Node* node) {
        if (node == nullptr) return;
        // cout << "Internal remove: " << node->key << endl; // Debug
        removeNodeFromList(node);
        cache.erase(node->key);
        delete node; // Free memory
    }

    bool isExpired(Node* node) const { // Mark const as it doesn't modify state
        if (ttl_seconds <= 0) return false; // Handle disabled TTL
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - node->timestamp);
        return duration.count() > ttl_seconds;
    }

public:
    LRUCache(int cap, int ttl) : capacity(cap), ttl_seconds(ttl) {
        if (capacity <= 0) {
           // Consider throwing an exception for invalid capacity
           capacity = 1; // Or handle gracefully
        }
        head = new Node("", ""); // Sentinel node
        tail = new Node("", ""); // Sentinel node
        head->next = tail;
        tail->prev = head;
    }

    ~LRUCache() {
        // No lock needed here, destructor assumes exclusive access
        Node* current = head->next;
        while (current != tail) {
            Node* toDelete = current;
            current = current->next;
            delete toDelete;
        }
        delete head;
        delete tail;
        // Map will clean itself up
    }

    // Disallow copy and assignment
    LRUCache(const LRUCache&) = delete;
    LRUCache& operator=(const LRUCache&) = delete;


    string get(const string& key) {
        std::lock_guard<std::mutex> lock(mtx); // Acquire lock
        auto it = cache.find(key);
        if (it == cache.end()) {
            return ""; // Not found
        }

        Node* node = it->second;

        // Check for expiration *after* finding the node
        if (isExpired(node)) {
            // cout << "Get: Node expired: " << key << endl; // Debug
            removeInternal(node); // Use internal remove (lock already held)
            return ""; // Treat expired as not found
        }

        // Node exists and is not expired, move to head (most recently used)
        moveToHead(node);
        return node->value;
    }

    void put(string key, string value) {
        std::lock_guard<std::mutex> lock(mtx); // Acquire lock
        auto it = cache.find(key);

        if (it != cache.end()) {
            // Key already exists
            Node* node = it->second;

            if (isExpired(node)) {
                 // cout << "Put: Node expired: " << key << endl; // Debug
                 // Remove the expired node first
                 removeInternal(node);
                 // Proceed to insert as a new node below
            } else {
                // Update existing node's value and timestamp, move to head
                // cout << "Put: Updating existing node: " << key << endl; // Debug
                node->value = value;
                node->timestamp = std::chrono::steady_clock::now();
                moveToHead(node);
                return; // Done
            }
        }

        // --- Insert new node or replace expired node ---

        // Check capacity *before* creating the new node
        if (cache.size() >= capacity) {
            // Evict least recently used node (tail)
            Node* tailNode = popTail();
            if (tailNode != nullptr) {
                // cout << "Put: Evicting node: " << tailNode->key << endl; // Debug
                cache.erase(tailNode->key);
                delete tailNode;
            }
        }

        // Create and insert the new node
        // cout << "Put: Inserting new node: " << key << endl; // Debug
        Node* newNode = new Node(key, value);
        cache[key] = newNode;
        addNodeToHead(newNode);
    }

    // Public remove function
    void remove(const string& key) {
        std::lock_guard<std::mutex> lock(mtx); // Acquire lock
        auto it = cache.find(key);
        if (it != cache.end()) {
            removeInternal(it->second); // Use internal remove
        }
    }

    void print() {
        std::lock_guard<std::mutex> lock(mtx); // Acquire lock
        Node* current = head->next;
        cout << "[ ";
        while (current != tail) {
            cout << "(" << current->key << ": " << current->value << ") ";
            // Optional: Add timestamp check for clarity during debug
            // if (isExpired(current)) cout << "(Expired) ";
            current = current->next;
        }
        cout << "]" << endl;
    }
};

// Global mutex for protecting cout (safer than local static)
std::mutex cout_mutex;

void accessCache(LRUCache& cache, string key, string value, bool isPut) {
    try { // Add basic exception handling for thread functions
        if (isPut) {
            cache.put(key, value);
             {
                 std::lock_guard<std::mutex> lock(cout_mutex);
                 // cout << "Thread " << std::this_thread::get_id() << " put " << key << "=" << value << endl; // More detailed debug
             }
        } else {
            string val = cache.get(key);
            {
                std::lock_guard<std::mutex> lock(cout_mutex);
                cout << "Thread " << std::this_thread::get_id() << " get " << key << ": " << (val.empty() ? "<Not Found/Expired>" : val) << endl;
            }
        }
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(cout_mutex);
        cerr << "Exception in thread " << std::this_thread::get_id() << ": " << e.what() << endl;
    } catch (...) {
        std::lock_guard<std::mutex> lock(cout_mutex);
        cerr << "Unknown exception in thread " << std::this_thread::get_id() << endl;
    }
}

int main() {
    int ttl_seconds = 2; // Cache items expire after 2 seconds
    LRUCache cache(3, ttl_seconds); // Capacity 3

    cout << "Initial Put Operations:" << endl;
    vector<thread> putThreads;
    putThreads.push_back(thread(accessCache, std::ref(cache), "A", "Apple", true));
    putThreads.push_back(thread(accessCache, std::ref(cache), "B", "Banana", true));
    putThreads.push_back(thread(accessCache, std::ref(cache), "C", "Cherry", true));

    for (auto& t : putThreads) t.join();
    cout << "Cache after initial puts: "; cache.print(); // Print state

    cout << "\nPutting D (should evict A or B or C - depends on thread scheduling):" << endl;
    thread putD(accessCache, std::ref(cache), "D", "Date", true);
    putD.join();
    cout << "Cache after putting D: "; cache.print(); // Print state

    cout << "\nGet Operations Before TTL Expires (approx 1 sec later):" << endl;
    this_thread::sleep_for(std::chrono::seconds(1));

    vector<thread> getBeforeTTL;
    // Accessing B and C will make them more recent than D
    getBeforeTTL.push_back(thread(accessCache, std::ref(cache), "B", "", false));
    getBeforeTTL.push_back(thread(accessCache, std::ref(cache), "C", "", false));
    getBeforeTTL.push_back(thread(accessCache, std::ref(cache), "D", "", false)); // Access D too

    for (auto& t : getBeforeTTL) t.join();
    cout << "Cache after gets (B, C, D accessed): "; cache.print();

    // Wait long enough for original items B, C, D to expire (relative to their *last access*)
    cout << "\nWaiting for TTL (" << ttl_seconds << "s) to expire..." << endl;
    this_thread::sleep_for(std::chrono::seconds(ttl_seconds + 1));

    cout << "\nGet Operations After TTL Should Have Expired:" << endl;
    vector<thread> getAfterTTL;
    getAfterTTL.push_back(thread(accessCache, std::ref(cache), "B", "", false)); // Should be expired
    getAfterTTL.push_back(thread(accessCache, std::ref(cache), "C", "", false)); // Should be expired
    getAfterTTL.push_back(thread(accessCache, std::ref(cache), "D", "", false)); // Should be expired

    for (auto& t : getAfterTTL) t.join();

    cout << "\nFinal Cache State: ";
    cache.print(); // Should likely be empty now

    return 0;
}