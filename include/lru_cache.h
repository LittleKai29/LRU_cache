#ifndef LRU_CACHE_H
#define LRU_CACHE_H

#include "node.h"
#include <string>
#include <unordered_map>
#include <mutex>
#include <cstddef>
#include <fstream> // Include for std::ofstream
#include <optional> // Include for optional return values

class LRUCache {
private:
    std::size_t capacity;
    std::unordered_map<std::string, Node*> cache;
    Node* head;
    Node* tail;
    mutable std::mutex mtx; // Made mutable for locking in const print()
    int ttl_seconds;

    // --- WAL Member ---
    std::ofstream* wal_stream_ = nullptr; // Pointer to the WAL output stream (optional)

    // --- Internal methods (assume lock is held by caller) ---
    void addNodeToHead(Node* node);
    void removeNodeFromList(Node* node);
    void moveToHead(Node* node);
    Node* popTail();
    void removeInternal(Node* node); // Removes from map/list and deletes node
    bool isExpired(const Node* node) const;

    // --- Internal logging helper (assumes lock is held) ---
    bool writeLogEntry(const std::string& entry);

    // --- Internal sync methods (now return bool for WAL success) ---
    // is_recovery flag prevents writing WAL during recovery phase
    std::optional<std::string> get_sync(const std::string& key); // Return optional string
    bool put_sync(const std::string& key, const std::string& value, bool is_recovery = false);
    bool remove_sync(const std::string& key, bool is_recovery = false);


public:
    // Constructor doesn't handle WAL stream directly anymore
    LRUCache(std::size_t cap, int ttl);
    virtual ~LRUCache();

    // --- Method to attach WAL stream after construction ---
    void setWalStream(std::ofstream* stream);

    // --- Public API (will call internal sync methods) ---
    // These might change slightly if we want to expose WAL failure
    std::optional<std::string> get(const std::string& key);
    bool put(const std::string& key, const std::string& value);
    bool remove(const std::string& key);

    // --- Recovery Method ---
    // Static method to load state from WAL into a cache instance
    static bool loadFromWAL(const std::string& wal_filename, LRUCache& cache_instance);

    // --- Other Methods ---
    void print() const;

    // Disable copy/assignment
    LRUCache(const LRUCache&) = delete;
    LRUCache& operator=(const LRUCache&) = delete;
};

#endif // LRU_CACHE_H