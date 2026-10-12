#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct MemoryModule {
    std::string path;
    std::string name;
    uintptr_t base;
    uintptr_t end;
};

namespace Memory {

void Refresh();
const std::vector<MemoryModule>& Modules();

bool IsReadable(uintptr_t addr, size_t len);
bool IsWritable(uintptr_t addr, size_t len);
bool Read(uintptr_t addr, void* out, size_t len);
bool Write(uintptr_t addr, const void* in, size_t len);

uintptr_t ModuleBase(const std::string& name);

}
