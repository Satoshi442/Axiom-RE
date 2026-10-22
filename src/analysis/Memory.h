#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct MemoryModule {
    std::string path;
    std::string name;
    uintptr_t base;
    uintptr_t end;   // end of the module's last mapping; size = end - base
};

// What we know about one address (from the last Refresh()).
struct MemoryInfo {
    bool mapped = false;           // inside a /proc/self/maps range
    uintptr_t regionStart = 0;
    uintptr_t regionEnd = 0;
    char perms[5] = {0};           // e.g. "r-xp"
    bool inModule = false;
    std::string moduleName;        // valid if inModule
    uintptr_t moduleOffset = 0;    // address - module base
};

namespace Memory {

void Refresh();
const std::vector<MemoryModule>& Modules();

bool IsReadable(uintptr_t addr, size_t len);
bool IsWritable(uintptr_t addr, size_t len);

// Read/Write never crash on a bad address: the kernel validates the pages
// (through a pipe) and the call just returns false.
bool Read(uintptr_t addr, void* out, size_t len);
bool Write(uintptr_t addr, const void* in, size_t len);

uintptr_t ModuleBase(const std::string& name);

bool Describe(uintptr_t addr, MemoryInfo& out);   // false if the address is in no known range
std::string Format(uintptr_t addr);               // "libfoo.so+0x1234", or "0x7F12AB" if in no module

}
