#include "lru_cache.h" // Includes node.h indirectly
#include <iostream>
#include <utility>
#include <chrono>      // Still needed here for time calculations in isExpired/get/put
#include <cstddef>

// --- LRUCache Private Helper Method Implementations ---
// No changes needed in the implementation logic

void LRUCache::addNodeToHead(Node* node) {
    node->next = head->next;
    node->prev = head;
    head->next->prev = node;
    head->next = node;
}

void LRUCache::removeNodeFromList(Node* node) {
    if (node == nullptr || node->prev == nullptr || node->next == nullptr) {
        return;
    }
    node->prev->next = node->next;
    node->next->prev = node->prev;
}

void LRUCache::moveToHead(Node* node) {
    removeNodeFromList(node);
    addNodeToHead(node);
}

Node* LRUCache::popTail() {
    if (tail->prev == head) {
        return nullptr;
    }
    Node* lastNode = tail->prev;
    removeNodeFromList(lastNode);
    return lastNode;
}

void LRUCache::removeInternal(Node* node) {
    if (node == nullptr) return;
    removeNodeFromList(node);
    cache.erase(node->key);
    delete node;
}

bool LRUCache::isExpired(const Node* node) const {
    if (ttl_seconds <= 0) return false;
    // Need <chrono> for this part
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - node->timestamp);
    return duration.count() > ttl_seconds;
}

// --- LRUCache Public Method Implementations ---
// No changes needed in the implementation logic

LRUCache::LRUCache(std::size_t cap, int ttl) : capacity(cap), ttl_seconds(ttl) {
    // Adjust the check: capacity cannot be < 0, just check for 0.
    if (capacity == 0) {
       std::cerr << "Warning: Invalid cache capacity 0. Setting to 1." << std::endl;
       capacity = 1; // Or throw std::invalid_argument("Capacity must be positive");
    }
    head = new Node("", "");
    tail = new Node("", "");
    head->next = tail;
    tail->prev = head;
}

LRUCache::~LRUCache() {
    Node* current = head->next;
    while (current != tail) {
        Node* toDelete = current;
        current = current->next;
        delete toDelete;
    }
    delete head;
    delete tail;
}

std::string LRUCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = cache.find(key);
    if (it == cache.end()) {
        return "";
    }
    Node* node = it->second;
    if (isExpired(node)) {
        removeInternal(node);
        return "";
    }
    moveToHead(node);
    node->timestamp = std::chrono::steady_clock::now(); // Reset TTL on access
    return node->value;
}

void LRUCache::put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = cache.find(key);
    if (it != cache.end()) {
        Node* node = it->second;
        if (isExpired(node)) {
             removeInternal(node);
             // Fall through
        } else {
            node->value = value;
            node->timestamp = std::chrono::steady_clock::now(); // Update timestamp
            moveToHead(node);
            return;
        }
    }

    if (cache.size() >= capacity) {
        Node* tailNode = popTail();
        if (tailNode != nullptr) {
            cache.erase(tailNode->key);
            delete tailNode;
        }
    }

    // Uses Node constructor from node.cpp (via node.h)
    Node* newNode = new Node(key, value);
    cache[key] = newNode;
    addNodeToHead(newNode);
}

void LRUCache::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = cache.find(key);
    if (it != cache.end()) {
        removeInternal(it->second);
    }
}

void LRUCache::print() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mtx));
    Node* current = head->next;
    std::cout << "Cache State (Head -> Tail): [ ";
    while (current != tail) {
        std::cout << "(" << current->key << ": " << current->value << ") ";
        current = current->next;
    }
    std::cout << "]" << std::endl;
}