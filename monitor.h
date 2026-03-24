#pragma once
// ============================================================
//  Monitor Mode
//
//  When enabled, every function call across all layers
//  prints a trace message to stdout.
//
//  Usage:
//    Monitor::enable();
//    Monitor::log(1, "FileSystem", "read(fd=0, bytes=5)");
//    Monitor::disable();
//
//  Layer numbers:
//    1 = User Interface
//    2 = FileSystem (User System Calls)
//    3 = LowFS (Low-Level FS)
//    4 = Cache
//    5 = Disk
// ============================================================
#include <iostream>
#include <string>

class Monitor {
public:
    static void enable()  { active = true;  std::cout << "[Monitor ON]\n";  }
    static void disable() { active = false; std::cout << "[Monitor OFF]\n"; }
    static bool is_active() { return active; }

    // Print a trace line indented by layer depth
    // layer 2 = 2 spaces, layer 3 = 4 spaces, etc.
    static void log(int layer, const std::string& component,
                    const std::string& message) {
        if (!active) return;
        std::string indent(layer * 2, ' ');
        std::cout << indent
                  << "[" << component << "] "
                  << message << "\n";
    }

private:
    static bool active;
};