#include <iostream>
#include <unordered_map>
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

    void print() {
        Node* current = head->next;
        while (current != tail) {
            cout << current->key << ": " << current->value << " <- ";
            current = current->next;
        }
        cout << "END" << endl;
    }
};

int main() {
    // Create a cache with capacity 3
    LRUCache cache(3);

    // Test 1: Put some values
    cache.put("A", "Apple");
    cache.put("B", "Banana");
    cache.put("C", "Cherry");

    // Print cache state
    cout << "Cache after 3 puts:" << endl;
    cache.print();  // Expected: A, B, C

    // Test 2: Get values
    cout << "Get A: " << cache.get("A") << endl;  // Expected: Apple
    cache.print();  // Expected: A, B, C (A is now at the front)

    // Test 3: Put a new value and evict the least recently used (B)
    cache.put("D", "Date");
    cout << "Cache after putting D:" << endl;
    cache.print();  // Expected: A, D, C

    // Test 4: Access an evicted item (should return empty)
    cout << "Get B: " << cache.get("B") << endl;  // Expected: ""

    // Test 5: Put a value that already exists (update the value)
    cache.put("A", "Apricot");
    cout << "Cache after updating A:" << endl;
    cache.print();  // Expected: A, D, C (A's value is updated to Apricot)

    return 0;
}
