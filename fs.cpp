// ============================================================
//  Layer 2: User System Calls
//  Uses Layer 3 (LowFS) for all disk operations.
// ============================================================
#include "fs.h"
#include "cache.h"
#include <iostream>
#include <fstream>
#include <vector>

FileSystem::FileSystem() : current_user(-1), logged_in(false) {}

// ============================================================
//  Private helpers
// ============================================================
std::string FileSystem::abs_path(const std::string& name) const {
    if (!name.empty() && name[0] == '/') return name;
    return "/user" + std::to_string(current_user) + "/" + name;
}

int FileSystem::alloc_fd() {
    for (int i = 0; i < MAX_OPEN_FILES; i++)
        if (!open_table[i].in_use) return i;
    return -1;
}

bool FileSystem::valid_fd(int fd) const {
    return fd >= 0 && fd < MAX_OPEN_FILES && open_table[fd].in_use;
}

// ============================================================
//  Session management
// ============================================================
int FileSystem::login(int user_id) {
    if (logged_in) { std::cerr << "Already logged in\n"; return -1; }
    current_user = user_id;
    logged_in    = true;

    SuperBlock sb;
    Disk::read_superblock(sb);

    // Ensure root inode (inode 0) exists
    INode root;
    Disk::read_inode(0, root);
    if (root.type == TYPE_FREE) {
        sb.inode_bitmap[0] |= 1;
        sb.free_inodes_count--;
        root = {};
        root.type  = TYPE_DIR;
        root.owner = 0;
        Disk::write_inode(0, root);
        Disk::write_superblock(sb);
        Disk::read_superblock(sb); // re-read updated sb
    }

    // Ensure /userX home directory exists
    std::string home = "user" + std::to_string(user_id);
    if (LowFS::find_in_dir(0, home) == -1) {
        int ni = LowFS::create_inode(sb, TYPE_DIR, user_id, ACCESS_READWRITE);
        if (ni < 0) { std::cerr << "login: no free i-nodes\n"; return -1; }
        Disk::write_superblock(sb);
        LowFS::add_to_dir(0, ni, home);
    }

    std::cout << "Logged in as user" << user_id << "\n";
    return 0;
}

int FileSystem::logout() {
    if (!logged_in) { std::cerr << "Not logged in\n"; return -1; }
    for (int i = 0; i < MAX_OPEN_FILES; i++)
        if (open_table[i].in_use) close(i);
    // Flush all dirty cache blocks to disk before ending session
    g_cache.flush();
    logged_in    = false;
    current_user = -1;
    std::cout << "Logged out\n";
    return 0;
}

// ============================================================
//  Directory commands
// ============================================================
int FileSystem::mkdir(const std::string& dir_name) {
    if (!logged_in) { std::cerr << "Not logged in\n"; return 0; }

    std::string path = abs_path(dir_name);
    int parent; std::string bname;
    if (LowFS::resolve_path(path, &parent, &bname) != -1) {
        std::cerr << "mkdir: already exists\n"; return 0;
    }

    SuperBlock sb;
    Disk::read_superblock(sb);
    int ni = LowFS::create_inode(sb, TYPE_DIR, current_user, ACCESS_READWRITE);
    if (ni < 0) { std::cerr << "mkdir: no free i-nodes\n"; return 0; }
    Disk::write_superblock(sb);
    LowFS::add_to_dir(parent, ni, bname);

    std::cout << "Directory created: " << bname << "\n";
    return 1;
}

int FileSystem::rmdir(const std::string& dir_name) {
    if (!logged_in) { std::cerr << "Not logged in\n"; return 0; }

    std::string path = abs_path(dir_name);
    int parent; std::string bname;
    int inode_num = LowFS::resolve_path(path, &parent, &bname);
    if (inode_num == -1) { std::cerr << "rmdir: not found\n"; return 0; }

    INode nd; Disk::read_inode(inode_num, nd);
    if (nd.type != TYPE_DIR)  { std::cerr << "rmdir: not a directory\n"; return 0; }
    if (nd.size  > 0)         { std::cerr << "rmdir: directory not empty\n"; return 0; }

    SuperBlock sb; Disk::read_superblock(sb);
    LowFS::free_inode_data(sb, nd, inode_num);
    LowFS::remove_from_dir(parent, bname);
    Disk::write_superblock(sb);

    std::cout << "Directory removed: " << bname << "\n";
    return 1;
}

int FileSystem::ls() {
    if (!logged_in) { std::cerr << "Not logged in\n"; return 0; }

    std::string home_path = "/user" + std::to_string(current_user);
    int dir_inode = LowFS::resolve_path(home_path);
    if (dir_inode == -1) { std::cerr << "ls: home dir not found\n"; return 0; }

    INode dir; Disk::read_inode(dir_inode, dir);
    int blocks_used = (dir.size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    char buf[BLOCK_SIZE];

    std::cout << "owner\tsize\tinode\tname\n";
    for (int b = 0; b < blocks_used && b < DIRECT_BLOCKS; b++) {
        if (dir.direct[b] == 0) continue;
        Disk::read_block(dir.direct[b], buf);
        DirEntry* entries = (DirEntry*)buf;
        for (int i = 0; i < ENTRIES_PER_BLOCK; i++) {
            if (entries[i].inode_num != 0) {
                INode child; Disk::read_inode(entries[i].inode_num, child);
                std::cout << (int)child.owner << "\t"
                          << child.size << "\t"
                          << entries[i].inode_num << "\t"
                          << entries[i].name << "\n";
            }
        }
    }
    return 1;
}

// ============================================================
//  File commands
// ============================================================
int FileSystem::create(const std::string& file_name) {
    if (!logged_in) { std::cerr << "Not logged in\n"; return -1; }

    std::string path = abs_path(file_name);
    int parent; std::string bname;
    if (LowFS::resolve_path(path, &parent, &bname) != -1) {
        std::cerr << "create: file already exists\n"; return -1;
    }

    SuperBlock sb; Disk::read_superblock(sb);
    int ni = LowFS::create_inode(sb, TYPE_FILE, current_user, ACCESS_READWRITE);
    if (ni < 0) { std::cerr << "create: no free i-nodes\n"; return -1; }
    Disk::write_superblock(sb);
    LowFS::add_to_dir(parent, ni, bname);

    int fd = alloc_fd();
    if (fd < 0) { std::cerr << "create: no free file descriptors\n"; return -1; }
    open_table[fd] = { true, ni, 0, ACCESS_READWRITE };

    std::cout << "File created, fd=" << fd << "\n";
    return fd;
}

int FileSystem::open(const std::string& file_name, int mode) {
    if (!logged_in) { std::cerr << "Not logged in\n"; return -1; }

    std::string path = abs_path(file_name);
    int inode_num = LowFS::resolve_path(path);
    if (inode_num == -1) { std::cerr << "open: file not found\n"; return -1; }

    int fd = alloc_fd();
    if (fd < 0) { std::cerr << "open: no free file descriptors\n"; return -1; }
    open_table[fd] = { true, inode_num, 0, mode };

    std::cout << "Opened fd=" << fd << "\n";
    return fd;
}

int FileSystem::close(int fd) {
    if (!valid_fd(fd)) { std::cerr << "close: invalid fd\n"; return 0; }
    open_table[fd] = {};
    std::cout << "Closed fd=" << fd << "\n";
    return 1;
}

int FileSystem::read(int fd, int num_bytes) {
    if (!valid_fd(fd))  { std::cerr << "read: invalid fd\n"; return -1; }
    if (open_table[fd].mode == ACCESS_WRITE) {
        std::cerr << "read: file opened write-only\n"; return -1;
    }

    INode f; Disk::read_inode(open_table[fd].inode_num, f);
    std::vector<char> buf(num_bytes + 1, 0);
    int got = LowFS::inode_read(f, open_table[fd].position, buf.data(), num_bytes);
    buf[got] = '\0';

    std::cout.write(buf.data(), got);
    std::cout << "\n";
    open_table[fd].position += got;
    std::cout << "Read " << got << " bytes\n";
    return got;
}

int FileSystem::write(int fd, const std::string& data) {
    if (!valid_fd(fd)) { std::cerr << "write: invalid fd\n"; return -1; }
    if (open_table[fd].mode == ACCESS_READ) {
        std::cerr << "write: file opened read-only\n"; return -1;
    }

    SuperBlock sb; Disk::read_superblock(sb);
    INode f; Disk::read_inode(open_table[fd].inode_num, f);

    int offset = f.size; // always append to end
    int written = LowFS::inode_write(sb, f, open_table[fd].inode_num,
                                     offset, data.c_str(), (int)data.size());
    Disk::write_superblock(sb);
    open_table[fd].position = f.size;

    std::cout << "Wrote " << written << " bytes\n";
    return written;
}

int FileSystem::seek(int fd, int location) {
    if (!valid_fd(fd)) { std::cerr << "seek: invalid fd\n"; return -1; }

    INode f; Disk::read_inode(open_table[fd].inode_num, f);
    if (location > (int)f.size) {
        std::cerr << "seek: location past end of file\n"; return -1;
    }
    open_table[fd].position = location;
    std::cout << "Seeked to " << location << "\n";
    return 0;
}

int FileSystem::rm(const std::string& file_name) {
    if (!logged_in) { std::cerr << "Not logged in\n"; return 0; }

    std::string path = abs_path(file_name);
    int parent; std::string bname;
    int inode_num = LowFS::resolve_path(path, &parent, &bname);
    if (inode_num == -1) { std::cerr << "rm: file not found\n"; return 0; }

    INode f; Disk::read_inode(inode_num, f);
    if (f.type != TYPE_FILE) { std::cerr << "rm: not a file\n"; return 0; }

    SuperBlock sb; Disk::read_superblock(sb);
    LowFS::free_inode_data(sb, f, inode_num);
    LowFS::remove_from_dir(parent, bname);
    Disk::write_superblock(sb);

    std::cout << "Removed: " << bname << "\n";
    return 1;
}

int FileSystem::copy(const std::string& src, const std::string& dst) {
    if (!logged_in) { std::cerr << "Not logged in\n"; return 0; }

    int src_inode = LowFS::resolve_path(abs_path(src));
    if (src_inode == -1) { std::cerr << "copy: source not found\n"; return 0; }

    int dst_dir = LowFS::resolve_path(abs_path(dst));
    if (dst_dir == -1) { std::cerr << "copy: destination dir not found\n"; return 0; }

    INode sf; Disk::read_inode(src_inode, sf);
    if (sf.type != TYPE_FILE) { std::cerr << "copy: source must be a file\n"; return 0; }

    // Extract base name from src path
    std::string bname = src;
    auto pos = src.rfind('/');
    if (pos != std::string::npos) bname = src.substr(pos + 1);

    // Read source data
    std::vector<char> buf(sf.size);
    LowFS::inode_read(sf, 0, buf.data(), sf.size);

    // Create destination file
    SuperBlock sb; Disk::read_superblock(sb);
    int ni = LowFS::create_inode(sb, TYPE_FILE, current_user, sf.permissions);
    if (ni < 0) { std::cerr << "copy: no free i-nodes\n"; return 0; }
    Disk::write_superblock(sb);
    LowFS::add_to_dir(dst_dir, ni, bname);

    // Write data into destination
    Disk::read_superblock(sb);
    INode nf; Disk::read_inode(ni, nf);
    LowFS::inode_write(sb, nf, ni, 0, buf.data(), sf.size);
    Disk::write_superblock(sb);

    std::cout << "Copied " << src << " -> " << dst << "/" << bname << "\n";
    return 1;
}

int FileSystem::import_file(const std::string& unix_path,
                             const std::string& sim_path) {
    if (!logged_in) { std::cerr << "Not logged in\n"; return 0; }

    std::ifstream f(unix_path, std::ios::binary);
    if (!f) { std::cerr << "import: cannot open " << unix_path << "\n"; return 0; }

    std::string data((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    int fd = create(sim_path);
    if (fd < 0) return 0;

    int written = write(fd, data);
    close(fd);
    std::cout << "Imported " << unix_path << " (" << written << " bytes)\n";
    return 1;
}