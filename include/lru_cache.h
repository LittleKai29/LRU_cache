#ifndef LRU_CACHE_H
#define LRU_CACHE_H

#include "node.h" // Include the separate Node definition

#include <string>
#include <unordered_map>
#include <mutex>
#include <cstddef>
// #include <chrono> // No longer needed directly here, Node handles timestamp

// Class definition for the LRU Cache with TTL
class LRUCache {
private:
    std::size_t capacity;
    std::unordered_map<std::string, Node*> cache; // Uses Node from node.h
    Node* head; // Dummy head (uses Node from node.h)
    Node* tail; // Dummy tail (uses Node from node.h)
    std::mutex mtx; // Mutex protecting cache data structures
    int ttl_seconds; // Time-to-live in seconds

    // --- Helper methods (private implementation details) ---
    void addNodeToHead(Node* node);
    void removeNodeFromList(Node* node);
    void moveToHead(Node* node);
    Node* popTail();
    void removeInternal(Node* node); // Assumes lock is held
    bool isExpired(const Node* node) const;

public:
    // Constructor and Destructor
    LRUCache(std::size_t cap, int ttl);
    virtual ~LRUCache();

    // Disable copy and assignment
    LRUCache(const LRUCache&) = delete;
    LRUCache& operator=(const LRUCache&) = delete;

    // --- Public API ---
    std::string get(const std::string& key);
    void put(const std::string& key, const std::string& value);
    void remove(const std::string& key);
    void print() const;
};

#endif // LRU_CACHE_H