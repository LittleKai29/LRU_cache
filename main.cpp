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
        //cout << "Removing node with key: " << node->key << endl;
    
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
    LRUCache(int capacity){
        this->capacity = capacity;
        head = new Node("", "");
        tail = new Node("", "");
        head->next = tail;
        tail->prev = head;
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
            if (cache.size() == capacity) {
                Node* removedNode = removeTail();
                cache.erase(removedNode->key);
                delete removedNode;
            }
            Node* newNode = new Node(key,value);
            cache[key] = newNode;
            moveToHead(newNode);
        }
    }
};

int main() {
    LRUCache lru(2);
    lru.put("name", "Kai");
    lru.put("lang", "C++");
    cout << "Get name: " << lru.get("name") << endl; // Should print Kai
    lru.put("editor", "VSCode"); // "lang" gets evicted
    cout << "Get lang: " << lru.get("lang") << endl; // Should print not found
    return 0;
}