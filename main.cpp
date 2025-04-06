#include "lru_cache.h" // Include the cache header (which includes node.h)

#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <string>
#include <functional> // for std::ref
#include <mutex>
#include <stdexcept> // For std::exception

using namespace std;

// Global mutex for protecting cout
std::mutex cout_mutex;

// Worker function for testing cache concurrently
void accessCache(LRUCache& cache, string key, string value, bool isPut) {
    // ... (Implementation remains exactly the same as before) ...
    try {
        if (isPut) {
            cache.put(key, value);
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
    // ... (Implementation remains exactly the same as before) ...
    const int ttl_seconds = 3;
    const int cache_capacity = 3;
    LRUCache cache(cache_capacity, ttl_seconds);

    cout << "--- Test Scenario ---" << endl;
    cout << "Cache Capacity: " << cache_capacity << ", TTL: " << ttl_seconds << "s" << endl;

    cout << "\n[Phase 1] Initial Puts (A, B, C)" << endl;
    vector<thread> workers;
    workers.emplace_back(accessCache, std::ref(cache), "A", "Apple", true);
    workers.emplace_back(accessCache, std::ref(cache), "B", "Banana", true);
    workers.emplace_back(accessCache, std::ref(cache), "C", "Cherry", true);
    for (auto& t : workers) t.join();
    workers.clear();
    cout << "Cache after initial puts: "; cache.print();

    cout << "\n[Phase 2] Putting D (should evict least recently used)" << endl;
    thread putD(accessCache, std::ref(cache), "D", "Date", true);
    putD.join();
    cout << "Cache after putting D: "; cache.print();

    cout << "\n[Phase 3] Access B & C (making them most recent), wait 1s" << endl;
    this_thread::sleep_for(std::chrono::seconds(1));
    workers.emplace_back(accessCache, std::ref(cache), "B", "", false);
    workers.emplace_back(accessCache, std::ref(cache), "C", "", false);
    for (auto& t : workers) t.join();
    workers.clear();
    cout << "Cache after accessing B & C: "; cache.print();

    int wait_time = ttl_seconds;
    cout << "\n[Phase 4] Waiting for TTL (" << wait_time << "s) to potentially expire older items..." << endl;
    this_thread::sleep_for(std::chrono::seconds(wait_time));

    cout << "\n[Phase 5] Get operations after waiting" << endl;
    workers.emplace_back(accessCache, std::ref(cache), "A", "", false);
    workers.emplace_back(accessCache, std::ref(cache), "B", "", false);
    workers.emplace_back(accessCache, std::ref(cache), "C", "", false);
    workers.emplace_back(accessCache, std::ref(cache), "D", "", false);
    for (auto& t : workers) t.join();
    workers.clear();

    cout << "\n[Phase 6] Final Cache State:" << endl;
    cache.print();

     cout << "\n[Phase 7] Test explicit remove" << endl;
     cache.remove("B");
     cout << "Cache after removing B: "; cache.print();

    cout << "\n--- Test Finished ---" << endl;

    return 0;
}