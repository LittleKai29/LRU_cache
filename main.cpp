#include <iostream>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <vector>
#include <chrono>
using namespace std;

struct Node {
    string key, value;
    Node* prev;
    Node* next;
    Node(string k, string v) : key(k), value(v), prev(nullptr), next(nullptr) {}
};

class LRUCache {
private:
    int capacity;
    unordered_map<string, Node*> cache;
    Node* head;
    Node* tail;
    std::mutex mtx;

    void moveToHead(Node* node) {
        removeNode(node);
        node->next = head->next;
        node->prev = head;
        head->next->prev = node;
        head->next = node;
    }

    void removeNode(Node* node) {
        if (node == nullptr || node->prev == nullptr || node->next == nullptr) {
            //cerr << "Error: Invalid node or node structure." << endl;
            return;
        }
        // Safe removal logic
        node->prev->next = node->next;
        node->next->prev = node->prev;
    }

    Node* removeTail(){
        Node* lastNode = tail->prev;
        removeNode(lastNode);
        return lastNode;
    }

public:
    LRUCache(int capacity) : capacity(capacity){
        head = new Node("", "");
        tail = new Node("", "");
        head->next = tail;
        tail->prev = head;
    }

    ~LRUCache() {
        Node* current = head->next;
        while (current != tail) {
            Node* toDelete = current;
            current = current->next;
            delete toDelete;
        }
        delete head;
        delete tail;
    }


    string get(const string& key){
        //I need check the key exist then update LRU then return the value of the key
        //Else do nothing ?
        std::lock_guard<std::mutex> lock(mtx);
        auto it = cache.find(key);
        if (it != cache.end()) {
            Node* currentNode = it->second;
            moveToHead(currentNode);
            return currentNode->value;
        }
        return "";
    }
    
    void put(string key, string value){
        //Yeah it's the use of unordered_map here, I have to check is the key exist
        //If it exist I have to perform update
        //Else I have to add it too the first
        std::lock_guard<std::mutex> lock(mtx);
        auto it = cache.find(key);
        if (it != cache.end()) {
            // Update value and move to head
            Node* node = it->second;
            node->value = value;
            moveToHead(node);
        }
        else {
            // If the cache is full, remove the least recently used item
            if (cache.size() == capacity) {
                Node* removedNode = removeTail();
                cache.erase(removedNode->key); // Remove the key from the cache map
                delete removedNode; // Free memory
            }
            // Insert the new node at the head
            Node* newNode = new Node(key, value);
            cache[key] = newNode;
            moveToHead(newNode);
        }
    }

    void remove(const string& key) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = cache.find(key);
        if (it != cache.end()) {
            Node* node = it->second;
            removeNode(node);
            cache.erase(it);
            delete node;
        }
    }

    void print() {
        std::lock_guard<std::mutex> lock(mtx);
        Node* current = head->next;
        while (current != tail) {
            cout << current->key << ": " << current->value << " <- ";
            current = current->next;
        }
        cout << "END" << endl;
    }
};

std::mutex cout_mutex;

void accessCache(LRUCache& cache, string key, string value, bool isPut) {
    if (isPut) {
        cache.put(key, value); // Put operation
    } else {
        string val = cache.get(key); // Get operation
        {
            std::lock_guard<std::mutex> lock(cout_mutex); // Lock to print output safely
            cout << "Thread get " << key << ": " << val << endl;
        }
    }
}


int main() {
    LRUCache cache(3);
    
    // Creating multiple threads to put values in cache
    vector<thread> threads;
    
    // Simulate putting data
    threads.push_back(thread(accessCache, std::ref(cache), "A", "Apple", true));
    threads.push_back(thread(accessCache, std::ref(cache), "B", "Banana", true));
    threads.push_back(thread(accessCache, std::ref(cache), "C", "Cherry", true));
    threads.push_back(thread(accessCache, std::ref(cache), "D", "Date", true));  // This should evict "A"
    threads.push_back(thread(accessCache, std::ref(cache), "E", "Elderberry", true)); // This should evict "B"
    
    // Simulate getting data
    threads.push_back(thread(accessCache, std::ref(cache), "A", "", false)); // Should print "" since "A" was evicted
    threads.push_back(thread(accessCache, std::ref(cache), "B", "", false)); // Should print "" since "B" was evicted
    threads.push_back(thread(accessCache, std::ref(cache), "C", "", false)); // Should print "Cherry"
    threads.push_back(thread(accessCache, std::ref(cache), "D", "", false)); // Should print "Date"
    
    for (auto& t : threads) {
        t.join(); // Wait for all threads to finish
    }
    
    // Final state of cache
    cout << "Final Cache State: ";
    cache.print();
}
