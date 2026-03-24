#include "lowfs.h"
#include "monitor.h"
#include <cstring>
#include <algorithm>

// ============================================================
//  Directory operations
// ============================================================

int LowFS::find_in_dir(int dir_inode_num, const std::string& name) {
    Monitor::log(3, "LowFS", "find_in_dir(inode=" + std::to_string(dir_inode_num) + ", name=" + name + ")");
    INode dir;
    Disk::read_inode(dir_inode_num, dir);
    if (dir.type != TYPE_DIR) return -1;

    int blocks_used = (dir.size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    char buf[BLOCK_SIZE];

    for (int b = 0; b < blocks_used && b < DIRECT_BLOCKS; b++) {
        if (dir.direct[b] == 0) continue;
        Disk::read_block(dir.direct[b], buf);
        DirEntry* entries = (DirEntry*)buf;
        for (int i = 0; i < ENTRIES_PER_BLOCK; i++) {
            if (entries[i].inode_num != 0 && name == entries[i].name)
                return entries[i].inode_num;
        }
    }
    return -1;
}

int LowFS::add_to_dir(int dir_inode_num, int child_inode,
                       const std::string& name) {
    SuperBlock sb;
    Disk::read_superblock(sb);
    INode dir;
    Disk::read_inode(dir_inode_num, dir);

    char buf[BLOCK_SIZE];
    int blocks_used = (dir.size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // Search existing blocks for a free slot
    for (int b = 0; b < blocks_used && b < DIRECT_BLOCKS; b++) {
        if (dir.direct[b] == 0) continue;
        Disk::read_block(dir.direct[b], buf);
        DirEntry* entries = (DirEntry*)buf;
        for (int i = 0; i < ENTRIES_PER_BLOCK; i++) {
            if (entries[i].inode_num == 0) {
                entries[i].inode_num = child_inode;
                strncpy(entries[i].name, name.c_str(), 29);
                entries[i].name[29] = '\0';
                Disk::write_block(dir.direct[b], buf);
                Disk::write_inode(dir_inode_num, dir);
                return 0;
            }
        }
    }

    // Need a new data block for this directory
    if (blocks_used >= DIRECT_BLOCKS) return -1;
    int new_block = Disk::alloc_block(sb);
    if (new_block < 0) return -1;

    dir.direct[blocks_used] = new_block;
    dir.size += BLOCK_SIZE;

    memset(buf, 0, BLOCK_SIZE);
    DirEntry* entries = (DirEntry*)buf;
    entries[0].inode_num = child_inode;
    strncpy(entries[0].name, name.c_str(), 29);
    entries[0].name[29] = '\0';

    Disk::write_block(new_block, buf);
    Disk::write_inode(dir_inode_num, dir);
    Disk::write_superblock(sb);
    return 0;
}

int LowFS::remove_from_dir(int dir_inode_num, const std::string& name) {
    INode dir;
    Disk::read_inode(dir_inode_num, dir);

    char buf[BLOCK_SIZE];
    int blocks_used = (dir.size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    for (int b = 0; b < blocks_used && b < DIRECT_BLOCKS; b++) {
        if (dir.direct[b] == 0) continue;
        Disk::read_block(dir.direct[b], buf);
        DirEntry* entries = (DirEntry*)buf;
        for (int i = 0; i < ENTRIES_PER_BLOCK; i++) {
            if (entries[i].inode_num != 0 && name == entries[i].name) {
                entries[i].inode_num = 0;
                memset(entries[i].name, 0, 30);
                Disk::write_block(dir.direct[b], buf);
                return 0;
            }
        }
    }
    return -1;
}

// ============================================================
//  Path resolution
// ============================================================

int LowFS::resolve_path(const std::string& path, int* parent_inode,
                         std::string* base_name) {
    // Tokenize on '/'
    std::vector<std::string> parts;
    std::string cur;
    for (char c : path) {
        if (c == '/') {
            if (!cur.empty()) { parts.push_back(cur); cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) parts.push_back(cur);

    int cur_inode = 0; // root is always inode 0
    int par_inode = -1;

    for (int i = 0; i < (int)parts.size(); i++) {
        par_inode = cur_inode;
        int next = find_in_dir(cur_inode, parts[i]);
        if (next == -1) {
            if (parent_inode) *parent_inode = par_inode;
            if (base_name)    *base_name    = parts[i];
            return -1;
        }

        // Follow symlink (but not on last component — caller may want the link itself)
        INode nd; Disk::read_inode(next, nd);
        if (nd.type == TYPE_SYMLINK && i < (int)parts.size() - 1) {
            // Read the target path stored in the symlink data
            char target[BLOCK_SIZE] = {};
            inode_read(nd, 0, target, nd.size);
            // Recursively resolve the symlink target
            next = resolve_path(std::string(target));
            if (next == -1) return -1;
        }

        cur_inode = next;
    }

    if (parent_inode) *parent_inode = par_inode;
    if (base_name && !parts.empty()) *base_name = parts.back();
    return cur_inode;
}

// ============================================================
//  Block-level file I/O
// ============================================================

int LowFS::get_or_alloc_block(SuperBlock& sb, INode& inode, int inode_num,
                                int logical, bool allocate) {
    // --- Direct blocks ---
    if (logical < DIRECT_BLOCKS) {
        if (inode.direct[logical] == 0 && allocate) {
            int b = Disk::alloc_block(sb);
            if (b < 0) return -1;
            inode.direct[logical] = b;
            if (inode_num >= 0) Disk::write_inode(inode_num, inode);
        }
        return inode.direct[logical];
    }

    const int per_block = BLOCK_SIZE / 2; // 512 pointers per block (2-byte each)
    logical -= DIRECT_BLOCKS;

    // --- Single indirect ---
    if (logical < per_block) {
        if (inode.single_indirect == 0 && allocate) {
            int b = Disk::alloc_block(sb);
            if (b < 0) return -1;
            inode.single_indirect = b;
            if (inode_num >= 0) Disk::write_inode(inode_num, inode);
        }
        if (inode.single_indirect == 0) return 0;

        char buf[BLOCK_SIZE];
        Disk::read_block(inode.single_indirect, buf);
        uint16_t* ptrs = (uint16_t*)buf;
        if (ptrs[logical] == 0 && allocate) {
            int b = Disk::alloc_block(sb);
            if (b < 0) return -1;
            ptrs[logical] = b;
            Disk::write_block(inode.single_indirect, buf);
        }
        return ptrs[logical];
    }

    // --- Double indirect ---
    logical -= per_block;
    if (logical < per_block * per_block) {
        if (inode.double_indirect == 0 && allocate) {
            int b = Disk::alloc_block(sb);
            if (b < 0) return -1;
            inode.double_indirect = b;
            if (inode_num >= 0) Disk::write_inode(inode_num, inode);
        }
        if (inode.double_indirect == 0) return 0;

        int l1 = logical / per_block;
        int l2 = logical % per_block;

        char buf[BLOCK_SIZE];
        Disk::read_block(inode.double_indirect, buf);
        uint16_t* l1ptrs = (uint16_t*)buf;
        if (l1ptrs[l1] == 0 && allocate) {
            int b = Disk::alloc_block(sb);
            if (b < 0) return -1;
            l1ptrs[l1] = b;
            Disk::write_block(inode.double_indirect, buf);
        }
        if (l1ptrs[l1] == 0) return 0;

        char buf2[BLOCK_SIZE];
        Disk::read_block(l1ptrs[l1], buf2);
        uint16_t* l2ptrs = (uint16_t*)buf2;
        if (l2ptrs[l2] == 0 && allocate) {
            int b = Disk::alloc_block(sb);
            if (b < 0) return -1;
            l2ptrs[l2] = b;
            Disk::write_block(l1ptrs[l1], buf2);
        }
        return l2ptrs[l2];
    }

    return -1; // file too large
}

int LowFS::inode_write(SuperBlock& sb, INode& inode, int inode_num,
                        int offset, const char* data, int len) {
    Monitor::log(3, "LowFS", "f_write(inode=" + std::to_string(inode_num) + ", offset=" + std::to_string(offset) + ", len=" + std::to_string(len) + ")");
    int written = 0;
    while (written < len) {
        int logical = (offset + written) / BLOCK_SIZE;
        int boff    = (offset + written) % BLOCK_SIZE;
        int chunk   = std::min(len - written, BLOCK_SIZE - boff);

        int phys = get_or_alloc_block(sb, inode, inode_num, logical, true);
        if (phys <= 0) break;

        char buf[BLOCK_SIZE];
        Disk::read_block(phys, buf);
        memcpy(buf + boff, data + written, chunk);
        Disk::write_block(phys, buf);
        written += chunk;
    }

    // Update file size if we extended it
    if (offset + written > (int)inode.size) {
        inode.size = offset + written;
        Disk::write_inode(inode_num, inode);
    }
    return written;
}

int LowFS::inode_read(const INode& inode, int offset, char* buf, int len) {
    Monitor::log(3, "LowFS", "f_read(offset=" + std::to_string(offset) + ", len=" + std::to_string(len) + ")");
    int avail = (int)inode.size - offset;
    if (avail <= 0) return 0;
    len = std::min(len, avail);

    // Use a mutable copy for get_or_alloc_block (read-only: allocate=false)
    INode mutable_inode = inode;
    SuperBlock sb;
    Disk::read_superblock(sb);

    int read_bytes = 0;
    while (read_bytes < len) {
        int logical = (offset + read_bytes) / BLOCK_SIZE;
        int boff    = (offset + read_bytes) % BLOCK_SIZE;
        int chunk   = std::min(len - read_bytes, BLOCK_SIZE - boff);

        int phys = get_or_alloc_block(sb, mutable_inode, -1, logical, false);
        if (phys <= 0) break;

        char blk[BLOCK_SIZE];
        Disk::read_block(phys, blk);
        memcpy(buf + read_bytes, blk + boff, chunk);
        read_bytes += chunk;
    }
    return read_bytes;
}

// ============================================================
//  inode lifecycle
// ============================================================

int LowFS::create_inode(SuperBlock& sb, uint8_t type,
                         uint8_t owner, uint8_t permissions) {
    Monitor::log(3, "LowFS", "f_create / d_create (type=" + std::to_string(type) + ", owner=" + std::to_string(owner) + ")");
    int ni = Disk::alloc_inode(sb);
    if (ni < 0) return -1;

    INode nd = {};
    nd.type        = type;
    nd.owner       = owner;
    nd.permissions = permissions;
    nd.link_count  = 1;
    nd.size        = 0;
    Disk::write_inode(ni, nd);
    return ni;
}

void LowFS::free_inode_data(SuperBlock& sb, INode& inode, int inode_num) {
    Monitor::log(3, "LowFS", "f_delete(inode=" + std::to_string(inode_num) + ")");
    // Decrement hard link count — only free data when no links remain
    if (inode.link_count > 1) {
        inode.link_count--;
        Disk::write_inode(inode_num, inode);
        return;
    }
    // Free all direct blocks
    for (int i = 0; i < DIRECT_BLOCKS; i++) {
        if (inode.direct[i]) {
            Disk::free_block(sb, inode.direct[i]);
            inode.direct[i] = 0;
        }
    }

    // Free single-indirect blocks
    if (inode.single_indirect) {
        char buf[BLOCK_SIZE];
        Disk::read_block(inode.single_indirect, buf);
        uint16_t* ptrs = (uint16_t*)buf;
        for (int i = 0; i < BLOCK_SIZE / 2; i++)
            if (ptrs[i]) Disk::free_block(sb, ptrs[i]);
        Disk::free_block(sb, inode.single_indirect);
        inode.single_indirect = 0;
    }

    // Free double-indirect blocks
    if (inode.double_indirect) {
        char buf[BLOCK_SIZE];
        Disk::read_block(inode.double_indirect, buf);
        uint16_t* l1ptrs = (uint16_t*)buf;
        for (int i = 0; i < BLOCK_SIZE / 2; i++) {
            if (l1ptrs[i]) {
                char buf2[BLOCK_SIZE];
                Disk::read_block(l1ptrs[i], buf2);
                uint16_t* l2ptrs = (uint16_t*)buf2;
                for (int j = 0; j < BLOCK_SIZE / 2; j++)
                    if (l2ptrs[j]) Disk::free_block(sb, l2ptrs[j]);
                Disk::free_block(sb, l1ptrs[i]);
            }
        }
        Disk::free_block(sb, inode.double_indirect);
        inode.double_indirect = 0;
    }

    // Free the inode itself
    Disk::free_inode(sb, inode_num);
    inode.type = TYPE_FREE;
    Disk::write_inode(inode_num, inode);
}