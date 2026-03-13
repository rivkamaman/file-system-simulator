#pragma once
// ============================================================
//  Layer 3: Low-Level Simulated File-System Calls
//
//  Uses services of Layer 4 (Disk).
//  Provides: inode-based file I/O, directory management,
//            path resolution.
// ============================================================
#include "disk.h"
#include <string>
#include <vector>

// ---- Directory entry (stored in a dir's data blocks) -------
#pragma pack(push, 1)
struct DirEntry {
    uint16_t inode_num;   // 0 = empty slot
    char     name[30];    // null-terminated filename
};
#pragma pack(pop)

const int ENTRIES_PER_BLOCK = BLOCK_SIZE / sizeof(DirEntry);

// ============================================================
//  LowFS  –  low-level file-system operations
//
//  All functions operate directly on inode numbers and
//  physical blocks; they know nothing about users or FDs.
// ============================================================
class LowFS {
public:
    // ---- Directory operations ----

    // Search directory dir_inode for an entry named `name`.
    // Returns child inode number, or -1 if not found.
    static int  find_in_dir(int dir_inode, const std::string& name);

    // Add child_inode as `name` inside directory dir_inode.
    // Returns 0 on success, -1 on failure.
    static int  add_to_dir(int dir_inode, int child_inode,
                            const std::string& name);

    // Remove the entry named `name` from directory dir_inode.
    // Returns 0 on success, -1 if not found.
    static int  remove_from_dir(int dir_inode, const std::string& name);

    // ---- Path resolution ----

    // Walk an absolute path (e.g. "/user1/dir/file").
    // Returns inode number of the final component, or -1.
    // Optionally writes the parent inode and base name to the
    // provided pointers (useful for create/mkdir).
    static int  resolve_path(const std::string& path,
                              int*         parent_inode = nullptr,
                              std::string* base_name    = nullptr);

    // ---- Block-level file I/O ----

    // Write `len` bytes from `data` into `inode` at byte `offset`,
    // allocating new blocks as needed.
    // Returns number of bytes written.
    static int  inode_write(SuperBlock& sb, INode& inode, int inode_num,
                             int offset, const char* data, int len);

    // Read up to `len` bytes from `inode` at byte `offset` into `buf`.
    // Returns number of bytes actually read.
    static int  inode_read(const INode& inode, int offset,
                            char* buf, int len);

    // ---- inode lifecycle helpers ----

    // Allocate a new inode of given type/owner, write it to disk,
    // and mark it used in the superblock (which is written back).
    // Returns inode number, or -1 on failure.
    static int  create_inode(SuperBlock& sb, uint8_t type,
                              uint8_t owner, uint8_t permissions);

    // Free all data blocks belonging to inode and the inode itself.
    static void free_inode_data(SuperBlock& sb, INode& inode, int inode_num);

private:
    // Translate a logical block index within a file to a physical block.
    // If allocate==true, missing blocks are created.
    // Returns physical block number, 0 if not allocated, -1 on error.
    static int  get_or_alloc_block(SuperBlock& sb, INode& inode,
                                    int inode_num, int logical,
                                    bool allocate);
};