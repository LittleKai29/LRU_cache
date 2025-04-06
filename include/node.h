// include/node.h (Header-Only Version)
#ifndef NODE_H
#define NODE_H

#include <string>
#include <chrono>
#include <utility> // Needed for std::move

// Node structure used by LRUCache
struct Node {
    std::string key;
    std::string value;
    Node* prev;
    Node* next;
    std::chrono::steady_clock::time_point timestamp;

    // Constructor DEFINED inline within the struct
    Node(std::string k, std::string v)
        : key(std::move(k)),
          value(std::move(v)),
          prev(nullptr),
          next(nullptr),
          timestamp(std::chrono::steady_clock::now())
    {} // Empty body is fine

    // Prevent copying/assignment
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
};

#endif // NODE_H