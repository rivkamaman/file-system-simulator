# File System Simulator
Operating Systems – Assignment 5

---

## How to Compile and Run

```bash
make
./fs_simulator
```

To run the automated tests:
```bash
chmod +x test.sh
./test.sh
```

To clean build files:
```bash
make clean
```

---

## File Structure

| File | Layer | Description |
|------|-------|-------------|
| `disk.h / disk.cpp` | Layer 4 | Simulated disk – raw block I/O, superblock, bitmap, i-node table |
| `cache.h / cache.cpp` | Layer 4 | Block cache – LRU with doubly linked list + hashmap, dirty-bit |
| `lowfs.h / lowfs.cpp` | Layer 3 | Low-level FS – directory management, path resolution, block-level file I/O |
| `fs.h / fs.cpp` | Layer 2 | User system calls – session management, open-file table, commands |
| `main.cpp` | Layer 1 | User interface – reads commands from stdin |
| `test.sh` | – | Automated test suite (31 tests) |

Each layer uses **only** the services of the layer directly below it.

---

## Disk Layout (FILE_SYS)

```
Block 0-5   : Superblock (fs size, block bitmap, inode bitmap)
Block 6-1055: I-node table (1050 blocks, one block per i-node)
Block 1056+ : Data blocks
```

**Key design decisions:**
- Block size: 1 KB
- Block addresses: 2 bytes
- Max files: 1024
- Each i-node occupies one complete block (as required)
- i-node has 10 direct blocks, 1 single indirect, 1 double indirect

---

## Supported Commands

```
login <user-id>              Start a user session
logout                       End the session (flushes cache to disk)

mkdir <dir-name>             Create a directory
rmdir <dir-name>             Remove an empty directory
ls                           List files in home directory

create <file-name>           Create a file, returns fd
open <file-name> <mode>      Open a file (0=read, 1=write, 2=read-write)
close <fd>                   Close a file descriptor
read <fd> <num-bytes>        Read bytes from file
write <fd> <string>          Write string to end of file
seek <fd> <location>         Move file position to byte offset
rm <file-name>               Delete a file

copy <src> <dst-dir>         Copy a file to another directory
import <unix-path> <path>    Import a file from the real filesystem

cache_status                 Show current cache state
```

---

## Block Cache

- **Size:** 3 slots (as required by assignment)
- **Algorithm:** LRU (Least Recently Used)
- **Data structures:** Doubly linked list + HashMap
  - Head = Most Recently Used
  - Tail = Least Recently Used (evicted first)
  - HashMap for O(1) lookup by block number
- **Dirty-bit:** Modified blocks are written to disk only when evicted or on `logout`

```
HEAD ↔ [MRU block] ↔ [block] ↔ [LRU block] ↔ TAIL
```

Use `cache_status` to see which blocks are currently cached and whether they are dirty.

---

## Directory Tree Structure

```
/root
  /user1
    /subdir
      file.txt
    file2.txt
  /user2
    ...
```

- Root contains only user directories
- Each user has up to two levels of subdirectories
- Files in the lower level only

---

## Persistence

The filesystem is stored in `FILE_SYS`. On first run it is initialized automatically. All subsequent runs load the existing state — data is never lost between sessions.
