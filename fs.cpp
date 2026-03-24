// ============================================================
//  Layer 2: User System Calls
//  Uses Layer 3 (LowFS) for all disk operations.
// ============================================================
#include "fs.h"
#include "cache.h"
#include "monitor.h"
#include <iostream>
#include <fstream>
#include <vector>

FileSystem::FileSystem() : current_user(-1), logged_in(false), current_dir("/") {}

// ============================================================
//  Private helpers
// ============================================================
std::string FileSystem::abs_path(const std::string& name) const {
    if (!name.empty() && name[0] == '/') return name;  // already absolute
    if (current_dir == "/") return "/" + name;
    return current_dir + "/" + name;
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
    Monitor::log(2, "FileSystem", "login(user=" + std::to_string(user_id) + ")");
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
        root.type        = TYPE_DIR;
        root.owner       = 0;
        root.permissions = 33;  // owner:all, others:all (root is world-writable)
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

    current_dir = "/user" + std::to_string(user_id);
    std::cout << "Logged in as user" << user_id << " (cwd=" << current_dir << ")\n";
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
    current_dir  = "/";
    std::cout << "Logged out\n";
    return 0;
}

// ============================================================
//  Directory commands
// ============================================================
int FileSystem::mkdir(const std::string& dir_name) {
    Monitor::log(2, "FileSystem", "MakeDir(" + dir_name + ")");
    if (!logged_in) { std::cerr << "Not logged in\n"; return 0; }

    std::string path = abs_path(dir_name);
    int parent; std::string bname;
    if (LowFS::resolve_path(path, &parent, &bname) != -1) {
        std::cerr << "mkdir: already exists\n"; return 0;
    }

    SuperBlock sb;
    Disk::read_superblock(sb);
    int ni = LowFS::create_inode(sb, TYPE_DIR, current_user, 30);  // owner:all, others:none
    if (ni < 0) { std::cerr << "mkdir: no free i-nodes\n"; return 0; }
    Disk::write_superblock(sb);
    LowFS::add_to_dir(parent, ni, bname);

    std::cout << "Directory created: " << bname << "\n";
    return 1;
}

int FileSystem::rmdir(const std::string& dir_name) {
    Monitor::log(2, "FileSystem", "RmDir(" + dir_name + ")");
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

    std::cout << current_dir << ":\n";
    int dir_inode = LowFS::resolve_path(current_dir);
    if (dir_inode == -1) { std::cerr << "ls: directory not found\n"; return 0; }

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
    Monitor::log(2, "FileSystem", "create(" + file_name + ")");
    if (!logged_in) { std::cerr << "Not logged in\n"; return -1; }

    std::string path = abs_path(file_name);
    int parent; std::string bname;
    if (LowFS::resolve_path(path, &parent, &bname) != -1) {
        std::cerr << "create: file already exists\n"; return -1;
    }

    SuperBlock sb; Disk::read_superblock(sb);
    int ni = LowFS::create_inode(sb, TYPE_FILE, current_user, 30);  // owner:all, others:none
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
    Monitor::log(2, "FileSystem", "Open(" + file_name + ", mode=" + std::to_string(mode) + ")");
    if (!logged_in) { std::cerr << "Not logged in\n"; return -1; }

    std::string path = abs_path(file_name);
    int inode_num = LowFS::resolve_path(path);
    if (inode_num == -1) { std::cerr << "open: file not found\n"; return -1; }

    // Follow symlink if needed
    INode f; Disk::read_inode(inode_num, f);
    if (f.type == TYPE_SYMLINK) {
        char target[BLOCK_SIZE] = {};
        LowFS::inode_read(f, 0, target, f.size);
        inode_num = LowFS::resolve_path(std::string(target));
        if (inode_num == -1) { std::cerr << "open: broken symlink\n"; return -1; }
        Disk::read_inode(inode_num, f);
    }
    bool is_owner = (f.owner == current_user);
    if ((mode == ACCESS_READ || mode == ACCESS_READWRITE) && !can_read(f.permissions, is_owner)) {
        std::cerr << "open: permission denied (no read access)\n"; return -1;
    }
    if ((mode == ACCESS_WRITE || mode == ACCESS_READWRITE) && !can_write(f.permissions, is_owner)) {
        std::cerr << "open: permission denied (no write access)\n"; return -1;
    }

    int fd = alloc_fd();
    if (fd < 0) { std::cerr << "open: no free file descriptors\n"; return -1; }
    open_table[fd] = { true, inode_num, 0, mode };

    std::cout << "Opened fd=" << fd << "\n";
    return fd;
}

int FileSystem::close(int fd) {
    Monitor::log(2, "FileSystem", "Close(fd=" + std::to_string(fd) + ")");
    if (!valid_fd(fd)) { std::cerr << "close: invalid fd\n"; return 0; }
    open_table[fd] = {};
    std::cout << "Closed fd=" << fd << "\n";
    return 1;
}

int FileSystem::read(int fd, int num_bytes) {
    Monitor::log(2, "FileSystem", "Read(fd=" + std::to_string(fd) + ", bytes=" + std::to_string(num_bytes) + ")");
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
    Monitor::log(2, "FileSystem", "Write(fd=" + std::to_string(fd) + ", bytes=" + std::to_string(data.size()) + ")");
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
    Monitor::log(2, "FileSystem", "Seek(fd=" + std::to_string(fd) + ", loc=" + std::to_string(location) + ")");
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
    Monitor::log(2, "FileSystem", "RmFile(" + file_name + ")");
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

int FileSystem::chmod(int mode, const std::string& file_name) {
    Monitor::log(2, "FileSystem", "ChMod(mode=" + std::to_string(mode) + ", file=" + file_name + ")");
    if (!logged_in) { std::cerr << "Not logged in\n"; return 0; }

    // Validate mode: tens digit and units digit must be 0-3
    int owner_perm  = mode / 10;
    int others_perm = mode % 10;
    if (owner_perm > 3 || others_perm > 3) {
        std::cerr << "chmod: invalid mode (use 0-3 for each digit, e.g. 31)\n"; return 0;
    }

    std::string path = abs_path(file_name);
    int inode_num = LowFS::resolve_path(path);
    if (inode_num == -1) { std::cerr << "chmod: file not found\n"; return 0; }

    INode f; Disk::read_inode(inode_num, f);
    if (f.owner != current_user) {
        std::cerr << "chmod: permission denied (not owner)\n"; return 0;
    }

    f.permissions = mode;
    Disk::write_inode(inode_num, f);
    std::cout << "Mode changed to " << mode << " for " << file_name << "\n";
    return 1;
}

// ============================================================
//  cd - change current directory
// ============================================================
int FileSystem::cd(const std::string& dir_name) {
    Monitor::log(2, "FileSystem", "ChDir(" + dir_name + ")");
    if (!logged_in) { std::cerr << "Not logged in\n"; return 0; }

    // Build target path
    std::string target;
    if (dir_name == "..") {
        // Go up one level — but not above user's home
        std::string home = "/user" + std::to_string(current_user);
        if (current_dir == home) {
            std::cerr << "cd: already at home directory\n"; return 0;
        }
        // Strip last component
        size_t pos = current_dir.rfind('/');
        target = (pos == 0) ? "/" : current_dir.substr(0, pos);
    } else {
        target = abs_path(dir_name);
    }

    // Verify target exists and is a directory
    int inode_num = LowFS::resolve_path(target);
    if (inode_num == -1) { std::cerr << "cd: directory not found\n"; return 0; }

    INode nd; Disk::read_inode(inode_num, nd);
    if (nd.type != TYPE_DIR) { std::cerr << "cd: not a directory\n"; return 0; }

    // Check read permission
    bool is_owner = (nd.owner == current_user);
    if (!can_read(nd.permissions, is_owner)) {
        std::cerr << "cd: permission denied\n"; return 0;
    }

    current_dir = target;
    std::cout << "cwd: " << current_dir << "\n";
    return 1;
}

// ============================================================
//  pwd - print current directory
// ============================================================
std::string FileSystem::pwd() const {
    std::cout << current_dir << "\n";
    return current_dir;
}

// ============================================================
//  ln - create hard or soft link
//  soft=false → hard link (same inode, increments link_count)
//  soft=true  → soft link (new inode, stores target path)
// ============================================================
int FileSystem::ln(const std::string& target, const std::string& link_name, bool soft) {
    Monitor::log(2, "FileSystem", std::string(soft ? "soft" : "hard") + "_link(" + target + " -> " + link_name + ")");
    if (!logged_in) { std::cerr << "Not logged in\n"; return 0; }

    std::string target_path   = abs_path(target);
    std::string link_path     = abs_path(link_name);

    // Resolve target
    int target_inode = LowFS::resolve_path(target_path);
    if (target_inode == -1) { std::cerr << "ln: target not found\n"; return 0; }

    // Link must not already exist
    int parent; std::string bname;
    if (LowFS::resolve_path(link_path, &parent, &bname) != -1) {
        std::cerr << "ln: link already exists\n"; return 0;
    }

    if (!soft) {
        // ---- Hard link ----
        INode t; Disk::read_inode(target_inode, t);
        if (t.type == TYPE_DIR) {
            std::cerr << "ln: cannot hard-link a directory\n"; return 0;
        }
        // Add new directory entry pointing to same inode
        LowFS::add_to_dir(parent, target_inode, bname);
        // Increment link count
        t.link_count++;
        Disk::write_inode(target_inode, t);
        std::cout << "Hard link created: " << bname << " -> inode " << target_inode << "\n";

    } else {
        // ---- Soft link ----
        SuperBlock sb; Disk::read_superblock(sb);
        int ni = LowFS::create_inode(sb, TYPE_SYMLINK, current_user, 33);
        if (ni < 0) { std::cerr << "ln: no free i-nodes\n"; return 0; }
        Disk::write_superblock(sb);

        // Store target path as the symlink's data
        INode lnk; Disk::read_inode(ni, lnk);
        Disk::read_superblock(sb);
        LowFS::inode_write(sb, lnk, ni, 0,
                           target_path.c_str(), (int)target_path.size());
        Disk::write_superblock(sb);

        LowFS::add_to_dir(parent, ni, bname);
        std::cout << "Soft link created: " << bname << " -> " << target_path << "\n";
    }
    return 1;
}