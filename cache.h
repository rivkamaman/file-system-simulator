#pragma once
// ============================================================
//  Block Cache  -  LRU with Doubly Linked List + HashMap
//
//  - 6-slot buffer (CACHE_SIZE)
//  - O(1) lookup via unordered_map<block_num, Node*>
//  - O(1) eviction via doubly linked list (tail = LRU)
//  - Dirty-bit: modified blocks flushed to disk on eviction
// ============================================================
#include "disk.h"
#include <unordered_map>

const int CACHE_SIZE = 6;

// ---- Doubly linked list node --------------------------------
struct Node {
    int   block_num;
    bool  dirty;
    char  data[BLOCK_SIZE];
    Node* prev;
    Node* next;

    Node() : block_num(-1), dirty(false), prev(nullptr), next(nullptr) {
        memset(data, 0, BLOCK_SIZE);
    }
};

// ============================================================
class BlockCache {
public:
    BlockCache();
    ~BlockCache();

    // Read a block: returns data from cache (loads from disk on miss)
    bool read(int block_num, void* buf);

    // Write a block: updates cache, marks dirty (no disk write yet)
    bool write(int block_num, const void* buf);

    // Flush all dirty blocks to disk
    void flush();

    // Print cache state
    void print_status() const;

private:
    // Doubly linked list: head = MRU, tail = LRU
    Node* head;   // dummy head sentinel
    Node* tail;   // dummy tail sentinel
    int   size;   // current number of cached blocks

    // HashMap: block_num → Node*
    std::unordered_map<int, Node*> map;

    // ---- List operations ----
    void remove_node(Node* node);          // detach from list
    void insert_at_head(Node* node);       // insert after head sentinel
    void move_to_head(Node* node);         // mark as most recently used

    // ---- Disk I/O ----
    void write_to_disk(Node* node);        // flush one dirty node
    void load_from_disk(Node* node, int block_num); // read block into node
};

extern BlockCache g_cache;