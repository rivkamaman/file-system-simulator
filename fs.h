#pragma once
// ============================================================
//  Layer 2: User System Calls
//
//  Uses services of Layer 3 (LowFS).
//  Manages sessions, the open-file table, and exposes the
//  named commands that the shell calls.
// ============================================================
#include "lowfs.h"

// ---- Open-file table entry ---------------------------------
struct OpenFileEntry {
    bool in_use    = false;
    int  inode_num = -1;
    int  position  = 0;   // current byte offset
    int  mode      = -1;  // ACCESS_READ / WRITE / READWRITE
};

const int MAX_OPEN_FILES = 16;

// ============================================================
//  FileSystem  -  user-level system calls
// ============================================================
class FileSystem {
public:
    FileSystem();

    // ---- Session management ----
    int login(int user_id);   // 0 = ok, -1 = error
    int logout();

    // ---- Directory commands ----
    int mkdir(const std::string& dir_name);
    int rmdir(const std::string& dir_name);
    int ls();

    // ---- File commands ----
    int create(const std::string& file_name);           // returns fd
    int open  (const std::string& file_name, int mode); // returns fd
    int close (int fd);
    int read  (int fd, int num_bytes);   // prints to stdout, returns bytes read
    int write (int fd, const std::string& data);  // returns bytes written
    int seek  (int fd, int location);    // returns -1 if location > file size
    int rm    (const std::string& file_name);
    int copy  (const std::string& src, const std::string& dst);
    int import_file(const std::string& unix_path, const std::string& sim_path);
    int chmod(int mode, const std::string& file_name);
    int cd   (const std::string& dir_name);  // change current directory
    std::string pwd() const;                  // print current directory
    int ln   (const std::string& target, const std::string& link_name, bool soft); // create link

private:
    int           current_user;
    bool          logged_in;
    std::string   current_dir;   // current working directory (full absolute path)
    OpenFileEntry open_table[MAX_OPEN_FILES];

    // Build an absolute path from a user-relative name
    std::string abs_path(const std::string& name) const;

    // Open-file table helpers
    int  alloc_fd();
    bool valid_fd(int fd) const;
};