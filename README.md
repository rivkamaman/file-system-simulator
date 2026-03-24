# File System Simulator

---

## Overview

This project implements a UNIX-based file system simulator in C++. The simulator models a complete file system on top of a single binary file (FILE_SYS) that acts as the virtual disk. It supports multiple users, persistent storage across sessions, and all standard file operations such as create, read, write, seek, copy, and import. The system is built in four layers — user interface, user system calls, low-level file system, and simulated disk — where each layer uses only the services of the layer below it. Block I/O is handled through a 3-slot LRU cache with a dirty-bit mechanism, implemented using a doubly linked list and a hashmap for O(1) performance.

---

## How to Compile and Run

```bash
make
./fs_simulator
```

To run directly with make:
```bash
make run
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
| `monitor.h / monitor.cpp` | – | Monitor mode – traces all function calls across layers |
| `main.cpp` | Layer 1 | User interface – reads commands from stdin |

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

chmod <mode> <file>          Change file permissions (e.g. chmod 31 file.txt)

cache_status                 Show current cache state
monitor                      Enable monitor mode (trace all layer calls)
no_monitor                   Disable monitor mode
```

---

## Block Cache

- **Size:** 6 slots 
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

## File Permissions (chmod)

Each file and directory has a permission mode with two digits:
- Tens digit = owner permissions
- Units digit = others permissions
- 0=none, 1=read, 2=write, 3=read+write

```
chmod 30 file.txt    ← owner: all,  others: none
chmod 31 file.txt    ← owner: all,  others: read only
chmod 33 file.txt    ← owner: all,  others: all
```

To access another user's file, use the full path:
```
open /user1/shared.txt 0
```

---

## Monitor Mode

Activate with `monitor` to trace all function calls across layers in real time:

```
monitor
read 0 5
    [FileSystem] Read(fd=0, bytes=5)
      [LowFS] f_read(offset=0, len=5)
        [Cache] READ block=1058 -> HIT (dirty=N)
          [Disk] b_read(block=1058)
hello
no_monitor
```

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

---

## Persistence

The filesystem is stored in `FILE_SYS`. On first run it is initialized automatically. All subsequent runs load the existing state — data is never lost between sessions.
