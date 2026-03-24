#include "fs.h"
#include "cache.h"
#include "monitor.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <string>

// ---- simple tokenizer ----
static std::vector<std::string> tokenize(const std::string& line) {
    std::istringstream ss(line);
    std::vector<std::string> tokens;
    std::string tok;
    // Read first token normally, then rest (for "write" we want raw remainder)
    while (ss >> tok) tokens.push_back(tok);
    return tokens;
}

// For 'write' we need everything after "write <fd> " as one string
static std::string rest_after(const std::string& line, int skip_words) {
    std::istringstream ss(line);
    std::string tok;
    for (int i = 0; i < skip_words; i++) ss >> tok;
    std::string remainder;
    std::getline(ss, remainder);
    // strip leading space
    if (!remainder.empty() && remainder[0] == ' ') remainder = remainder.substr(1);
    return remainder;
}

int main() {
    FileSystem fs;

    // Initialize disk if not present
    if (!Disk::exists()) {
        std::cout << "Initializing new file system...\n";
        if (!Disk::init()) {
            std::cerr << "Failed to initialize FILE_SYS\n";
            return 1;
        }
        std::cout << "File system initialized.\n";
    } else {
        std::cout << "File system loaded from FILE_SYS.\n";
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        auto tok = tokenize(line);
        if (tok.empty()) continue;

        const std::string& cmd = tok[0];

        if (cmd == "login") {
            if (tok.size() < 2) { std::cerr << "Usage: login <id>\n"; continue; }
            fs.login(std::stoi(tok[1]));

        } else if (cmd == "logout") {
            fs.logout();

        } else if (cmd == "mkdir") {
            if (tok.size() < 2) { std::cerr << "Usage: mkdir <dir>\n"; continue; }
            fs.mkdir(tok[1]);

        } else if (cmd == "rmdir") {
            if (tok.size() < 2) { std::cerr << "Usage: rmdir <dir>\n"; continue; }
            fs.rmdir(tok[1]);

        } else if (cmd == "ls") {
            fs.ls();

        } else if (cmd == "create") {
            if (tok.size() < 2) { std::cerr << "Usage: create <file>\n"; continue; }
            fs.create(tok[1]);

        } else if (cmd == "open") {
            if (tok.size() < 3) { std::cerr << "Usage: open <file> <mode>\n"; continue; }
            fs.open(tok[1], std::stoi(tok[2]));

        } else if (cmd == "close") {
            if (tok.size() < 2) { std::cerr << "Usage: close <fd>\n"; continue; }
            fs.close(std::stoi(tok[1]));

        } else if (cmd == "read") {
            if (tok.size() < 3) { std::cerr << "Usage: read <fd> <bytes>\n"; continue; }
            fs.read(std::stoi(tok[1]), std::stoi(tok[2]));

        } else if (cmd == "write") {
            if (tok.size() < 3) { std::cerr << "Usage: write <fd> <string>\n"; continue; }
            std::string data = rest_after(line, 2);
            fs.write(std::stoi(tok[1]), data);

        } else if (cmd == "seek") {
            if (tok.size() < 3) { std::cerr << "Usage: seek <fd> <loc>\n"; continue; }
            fs.seek(std::stoi(tok[1]), std::stoi(tok[2]));

        } else if (cmd == "rm") {
            if (tok.size() < 2) { std::cerr << "Usage: rm <file>\n"; continue; }
            fs.rm(tok[1]);

        } else if (cmd == "copy") {
            if (tok.size() < 3) { std::cerr << "Usage: copy <src> <dst>\n"; continue; }
            fs.copy(tok[1], tok[2]);

        } else if (cmd == "import") {
            if (tok.size() < 3) { std::cerr << "Usage: import <unix_path> <sim_path>\n"; continue; }
            fs.import_file(tok[1], tok[2]);

        } else if (cmd == "chmod") {
            if (tok.size() < 3) { std::cerr << "Usage: chmod <mode> <file>\n"; continue; }
            fs.chmod(std::stoi(tok[1]), tok[2]);

        } else if (cmd == "cache_status") {
            g_cache.print_status();

        } else if (cmd == "monitor") {
            Monitor::enable();

        } else if (cmd == "no_monitor") {
            Monitor::disable();

        } else {
            std::cerr << "Unknown command: " << cmd << "\n";
        }
    }
    return 0;
}