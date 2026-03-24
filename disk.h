#pragma once
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>

// ============================================================
//  Constants
// ============================================================
const int BLOCK_SIZE       = 1024;          // 1 KB per block
const int DISK_SIZE_BLOCKS = 4096;          // 4 MB total
const int MAX_FILES        = 1024;
const int INODE_ARRAY_BLOCKS = 1050;

// Superblock occupies physical blocks 0-4
const int SUPERBLOCK_START = 0;
const int SUPERBLOCK_BLOCKS = 6;  // 6 blocks = 6 KB to fit SuperBlock

// i-node table starts right after superblock
const int INODE_TABLE_START = SUPERBLOCK_BLOCKS;  // block 5

// Data blocks start after i-node table
const int DATA_BLOCKS_START = INODE_TABLE_START + INODE_ARRAY_BLOCKS; // block 1055

// i-node structure
const int DIRECT_BLOCKS    = 10;
const int INODE_SIZE        = 40;           // must match sizeof(INode)

// File types
const int TYPE_FREE    = 0;
const int TYPE_FILE    = 1;
const int TYPE_DIR     = 2;
const int TYPE_SYMLINK = 3;  // soft link — data contains target path

// Open modes (for open() call)
const int ACCESS_READ       = 0;
const int ACCESS_WRITE      = 1;
const int ACCESS_READWRITE  = 2;

// Permission bits (stored in INode::permissions)
// Format: tens digit = owner, units digit = others
// 0=none, 1=read, 2=write, 3=read+write
const int PERM_NONE         = 0;
const int PERM_READ         = 1;
const int PERM_WRITE        = 2;
const int PERM_ALL          = 3;

// Helper macros
// permissions = owner*10 + others  (e.g. 31 = owner:all, others:read)
inline int perm_owner(uint8_t p)  { return p / 10; }
inline int perm_others(uint8_t p) { return p % 10; }
inline bool can_read (uint8_t p, bool is_owner) {
    int bits = is_owner ? perm_owner(p) : perm_others(p);
    return bits == PERM_READ || bits == PERM_ALL;
}
inline bool can_write(uint8_t p, bool is_owner) {
    int bits = is_owner ? perm_owner(p) : perm_others(p);
    return bits == PERM_WRITE || bits == PERM_ALL;
}

// ============================================================
//  i-node  (exactly 64 bytes, as required by the assignment)
//  4 + 4 + 20 + 2 + 2 + 32 = 64
// ============================================================
#pragma pack(push, 1)
struct INode {
    uint8_t  type;                      // TYPE_FREE / TYPE_FILE / TYPE_DIR / TYPE_SYMLINK
    uint8_t  owner;                     // user id (0 = root)
    uint8_t  permissions;               // ACCESS_READ / WRITE / READWRITE
    uint8_t  link_count;                // number of hard links pointing to this inode
    uint32_t size;                      // file size in bytes
    uint16_t direct[DIRECT_BLOCKS];     // direct block numbers (2 bytes each)
    uint16_t single_indirect;           // single-indirect block
    uint16_t double_indirect;           // double-indirect block
    uint8_t  _pad2[8];                  // reserved for future use
};
#pragma pack(pop)
static_assert(sizeof(INode) == 40, "INode must be exactly 40 bytes");

// ============================================================
//  Superblock  (fits in blocks 0-4 = 5 KB)
// ============================================================
#pragma pack(push, 1)
struct SuperBlock {
    uint32_t fs_size;                   // total blocks in filesystem
    // Blocks control
    uint32_t free_blocks_count;
    uint8_t  block_bitmap[4 * BLOCK_SIZE]; // 4 KB bitmap → 32768 bits
    uint32_t next_free_block;
    // i-node control
    uint32_t inode_count;               // total i-nodes
    uint32_t free_inodes_count;
    uint8_t  inode_bitmap[BLOCK_SIZE];  // 1 KB bitmap → 8192 bits (> MAX_FILES)
    uint32_t next_free_inode;
    uint8_t  modified;                  // superblock dirty flag
};
#pragma pack(pop)

// ============================================================
//  Disk class  –  raw block read / write
// ============================================================
class Disk {
public:
    static const char* DISK_FILE;

    // Initialise a brand-new disk (called only once)
    static bool init();

    // Check whether FILE_SYS already exists
    static bool exists();

    // Read / write a single 1 KB block
    static bool read_block(int block_num, void* buf);
    static bool write_block(int block_num, const void* buf);

    // Superblock helpers
    static bool read_superblock(SuperBlock& sb);
    static bool write_superblock(const SuperBlock& sb);

    // i-node helpers
    static bool read_inode(int inode_num, INode& inode);
    static bool write_inode(int inode_num, const INode& inode);

    // Block allocation / freeing
    static int  alloc_block(SuperBlock& sb);   // returns block number, -1 on failure
    static bool free_block(SuperBlock& sb, int block_num);

    // i-node allocation / freeing
    static int  alloc_inode(SuperBlock& sb);   // returns i-node number, -1 on failure
    static bool free_inode(SuperBlock& sb, int inode_num);

private:
    static std::fstream open_disk();
};