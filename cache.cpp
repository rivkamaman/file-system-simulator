// ============================================================
//  Block Cache  -  LRU with Doubly Linked List + HashMap
//
//  List layout:
//    head <-> [MRU] <-> ... <-> [LRU] <-> tail
//
//  On every access: move touched node to head  (O(1))
//  On eviction:     remove tail->prev          (O(1))
//  On lookup:       map[block_num]             (O(1))
// ============================================================
#include "cache.h"
#include <iostream>
#include <fstream>

BlockCache g_cache;

// ============================================================
//  Constructor / Destructor
// ============================================================
BlockCache::BlockCache() : size(0) {
    // Create dummy sentinel nodes — they never hold real data
    head = new Node();
    tail = new Node();
    head->next = tail;
    tail->prev = head;
}

BlockCache::~BlockCache() {
    flush();
    // Free all real nodes
    Node* cur = head->next;
    while (cur != tail) {
        Node* next = cur->next;
        delete cur;
        cur = next;
    }
    delete head;
    delete tail;
}

// ============================================================
//  List operations
// ============================================================

// Detach a node from wherever it is in the list
void BlockCache::remove_node(Node* node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->prev = nullptr;
    node->next = nullptr;
}

// Insert a node right after the head sentinel (= MRU position)
void BlockCache::insert_at_head(Node* node) {
    node->next       = head->next;
    node->prev       = head;
    head->next->prev = node;
    head->next       = node;
}

// Move an existing node to the MRU position
void BlockCache::move_to_head(Node* node) {
    remove_node(node);
    insert_at_head(node);
}

// ============================================================
//  Disk I/O helpers
// ============================================================
void BlockCache::write_to_disk(Node* node) {
    if (!node->dirty || node->block_num < 0) return;
    std::fstream f(Disk::DISK_FILE,
                   std::ios::in | std::ios::out | std::ios::binary);
    if (!f) return;
    f.seekp((long long)node->block_num * BLOCK_SIZE);
    f.write(node->data, BLOCK_SIZE);
    node->dirty = false;
}

void BlockCache::load_from_disk(Node* node, int block_num) {
    std::fstream f(Disk::DISK_FILE,
                   std::ios::in | std::ios::out | std::ios::binary);
    if (!f) return;
    f.seekg((long long)block_num * BLOCK_SIZE);
    f.read(node->data, BLOCK_SIZE);
    node->block_num = block_num;
    node->dirty     = false;
}

// ============================================================
//  read
// ============================================================
bool BlockCache::read(int block_num, void* buf) {
    auto it = map.find(block_num);

    if (it != map.end()) {
        // ---- Cache HIT ----
        Node* node = it->second;
        move_to_head(node);          // mark as most recently used
        memcpy(buf, node->data, BLOCK_SIZE);
        return true;
    }

    // ---- Cache MISS ----
    Node* node;

    if (size < CACHE_SIZE) {
        // Cache not full yet — allocate a new node
        node = new Node();
        size++;
    } else {
        // Cache full — evict the LRU node (tail->prev)
        node = tail->prev;
        map.erase(node->block_num);  // remove from hashmap
        remove_node(node);           // detach from list
        if (node->dirty)
            write_to_disk(node);     // flush to disk before reuse
    }

    // Load the requested block from disk into this node
    load_from_disk(node, block_num);
    insert_at_head(node);
    map[block_num] = node;

    memcpy(buf, node->data, BLOCK_SIZE);
    return true;
}

// ============================================================
//  write
// ============================================================
bool BlockCache::write(int block_num, const void* buf) {
    auto it = map.find(block_num);

    if (it != map.end()) {
        // ---- Cache HIT ---- update in place
        Node* node = it->second;
        memcpy(node->data, buf, BLOCK_SIZE);
        node->dirty = true;
        move_to_head(node);
        return true;
    }

    // ---- Cache MISS ----
    Node* node;

    if (size < CACHE_SIZE) {
        node = new Node();
        size++;
    } else {
        // Evict LRU
        node = tail->prev;
        map.erase(node->block_num);
        remove_node(node);
        if (node->dirty)
            write_to_disk(node);
    }

    // Write new data into node — mark dirty (no disk write yet)
    node->block_num = block_num;
    memcpy(node->data, buf, BLOCK_SIZE);
    node->dirty = true;

    insert_at_head(node);
    map[block_num] = node;
    return true;
}

// ============================================================
//  flush: write all dirty blocks to disk
// ============================================================
void BlockCache::flush() {
    Node* cur = head->next;
    while (cur != tail) {
        if (cur->dirty) write_to_disk(cur);
        cur = cur->next;
    }
}

// ============================================================
//  print_status
// ============================================================
void BlockCache::print_status() const {
    std::cout << "Cache (head=MRU, tail=LRU):\n";
    std::cout << "┌──────┬────────┬───────┐\n";
    std::cout << "│ Pos  │ Block  │ Dirty │\n";
    std::cout << "├──────┼────────┼───────┤\n";
    int pos = 0;
    Node* cur = head->next;
    while (cur != tail) {
        std::cout << "│  " << pos << "   │"
                  << "  " << cur->block_num
                  << (cur->block_num < 10    ? "     " :
                      cur->block_num < 100   ? "    "  :
                      cur->block_num < 1000  ? "   "   : "  ")
                  << "│   " << (cur->dirty ? "Y" : "N") << "   │\n";
        cur = cur->next;
        pos++;
    }
    // Fill empty slots
    for (; pos < CACHE_SIZE; pos++)
        std::cout << "│  " << pos << "   │  empty  │   -   │\n";
    std::cout << "└──────┴────────┴───────┘\n";
    std::cout << "(" << size << "/" << CACHE_SIZE << " slots used)\n";
}