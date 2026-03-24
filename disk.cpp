// ============================================================
//  Layer 4: Simulated Disk
//  All block I/O now goes through the BlockCache.
// ============================================================
#include "disk.h"
#include "cache.h"
#include "monitor.h"

const char* Disk::DISK_FILE = "FILE_SYS";

// ----------------------------------------------------------------
bool Disk::exists() {
    std::ifstream f(DISK_FILE);
    return f.good();
}

// ----------------------------------------------------------------
std::fstream Disk::open_disk() {
    std::fstream f(DISK_FILE, std::ios::in | std::ios::out | std::ios::binary);
    return f;
}

// ----------------------------------------------------------------
bool Disk::init() {
    std::ofstream f(DISK_FILE, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    char block[BLOCK_SIZE] = {};
    for (int i = 0; i < DISK_SIZE_BLOCKS; i++)
        f.write(block, BLOCK_SIZE);
    f.close();

    SuperBlock sb = {};
    sb.fs_size           = DISK_SIZE_BLOCKS;
    sb.inode_count       = MAX_FILES;
    sb.free_inodes_count = MAX_FILES;
    sb.next_free_inode   = 0;
    int data_blocks      = DISK_SIZE_BLOCKS - DATA_BLOCKS_START;
    sb.free_blocks_count = data_blocks;
    sb.next_free_block   = DATA_BLOCKS_START;
    for (int i = 0; i < DATA_BLOCKS_START; i++)
        sb.block_bitmap[i / 8] |= (1 << (i % 8));
    sb.modified = 0;
    return write_superblock(sb);
}

// ----------------------------------------------------------------
//  read_block / write_block  –  go through cache
// ----------------------------------------------------------------
bool Disk::read_block(int block_num, void* buf) {
    Monitor::log(5, "Disk", "b_read(block=" + std::to_string(block_num) + ")");
    return g_cache.read(block_num, buf);
}

bool Disk::write_block(int block_num, const void* buf) {
    Monitor::log(5, "Disk", "b_write(block=" + std::to_string(block_num) + ")");
    return g_cache.write(block_num, buf);
}

// ----------------------------------------------------------------
//  Superblock: bypass cache (too large for one block slot)
// ----------------------------------------------------------------
bool Disk::read_superblock(SuperBlock& sb) {
    auto f = open_disk();
    if (!f) return false;
    f.seekg(0);
    f.read((char*)&sb, sizeof(SuperBlock));
    return f.good();
}

bool Disk::write_superblock(const SuperBlock& sb) {
    auto f = open_disk();
    if (!f) return false;
    f.seekp(0);
    f.write((const char*)&sb, sizeof(SuperBlock));
    return f.good();
}

// ----------------------------------------------------------------
//  i-node helpers  (use cache via read_block / write_block)
// ----------------------------------------------------------------
bool Disk::read_inode(int inode_num, INode& inode) {
    Monitor::log(5, "Disk", "i_read(inode=" + std::to_string(inode_num) + ")");
    // Each i-node occupies one complete block (as per assignment spec)
    int block = INODE_TABLE_START + inode_num;
    char buf[BLOCK_SIZE];
    if (!read_block(block, buf)) return false;
    memcpy(&inode, buf, INODE_SIZE);
    return true;
}

bool Disk::write_inode(int inode_num, const INode& inode) {
    Monitor::log(5, "Disk", "i_write(inode=" + std::to_string(inode_num) + ")");
    // Each i-node occupies one complete block (as per assignment spec)
    int block = INODE_TABLE_START + inode_num;
    char buf[BLOCK_SIZE] = {};
    memcpy(buf, &inode, INODE_SIZE);
    return write_block(block, buf);
}

// ----------------------------------------------------------------
int Disk::alloc_block(SuperBlock& sb) {
    if (sb.free_blocks_count == 0) return -1;
    for (int i = DATA_BLOCKS_START; i < DISK_SIZE_BLOCKS; i++) {
        int byte = i / 8, bit = i % 8;
        if (!(sb.block_bitmap[byte] & (1 << bit))) {
            sb.block_bitmap[byte] |= (1 << bit);
            sb.free_blocks_count--;
            sb.next_free_block = i + 1;
            // Zero the new block through cache
            char zeros[BLOCK_SIZE] = {};
            write_block(i, zeros);
            return i;
        }
    }
    return -1;
}

bool Disk::free_block(SuperBlock& sb, int block_num) {
    if (block_num < DATA_BLOCKS_START || block_num >= DISK_SIZE_BLOCKS) return false;
    int byte = block_num / 8, bit = block_num % 8;
    if (!(sb.block_bitmap[byte] & (1 << bit))) return false;
    sb.block_bitmap[byte] &= ~(1 << bit);
    sb.free_blocks_count++;
    return true;
}

int Disk::alloc_inode(SuperBlock& sb) {
    if (sb.free_inodes_count == 0) return -1;
    for (int i = 0; i < (int)sb.inode_count; i++) {
        int byte = i / 8, bit = i % 8;
        if (!(sb.inode_bitmap[byte] & (1 << bit))) {
            sb.inode_bitmap[byte] |= (1 << bit);
            sb.free_inodes_count--;
            return i;
        }
    }
    return -1;
}

bool Disk::free_inode(SuperBlock& sb, int inode_num) {
    if (inode_num < 0 || inode_num >= (int)sb.inode_count) return false;
    int byte = inode_num / 8, bit = inode_num % 8;
    sb.inode_bitmap[byte] &= ~(1 << bit);
    sb.free_inodes_count++;
    return true;
}