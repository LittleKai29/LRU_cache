// main.cpp
#include "lru_cache.h"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <string>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <cstddef> // Include for std::size_t

using namespace std;

// ... (cout_mutex and accessCache function remain the same) ...
std::mutex cout_mutex;
void accessCache(LRUCache& cache, string key, string value, bool isPut) { /* ... */ }


int main() {
    const int ttl_seconds = 3;
    // Change the type here:
    const std::size_t cache_capacity = 3; // Use unsigned size type

    LRUCache cache(cache_capacity, ttl_seconds);

    cout << "--- Test Scenario ---" << endl;
    // Use %zu or similar for printing size_t if needed, but capacity is usually small
    cout << "Cache Capacity: " << cache_capacity << ", TTL: " << ttl_seconds << "s" << endl;

    // ... (rest of main function remains the same) ...

    return 0;
}