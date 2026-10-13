#include "Memory.h"

#include <android/log.h>
#include <cstdio>
#include <cstring>

#define LOG_TAG "Axiom-RE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {

std::vector<MemoryModule> g_modules;
std::vector<MemoryRange> g_ranges;

}

namespace Memory {

void Refresh() {
    g_modules.clear();
    g_ranges.clear();

    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        unsigned long long start = 0, end = 0, offset = 0, inode = 0;
        char perms[5] = {0};
        char path[256] = {0};

        int n = sscanf(line, "%llx-%llx %4s %llx %*s %llu %255s",
                       &start, &end, perms, &offset, &inode, path);
        if (n < 3) continue;

        MemoryRange r;
        r.start = (uintptr_t)start;
        r.end = (uintptr_t)end;
        r.perms = perms;
        r.path = path;
        g_ranges.push_back(r);

        if (path[0] == '/' && strstr(path, ".so")) {
            const char* baseName = strrchr(path, '/');
            baseName = baseName ? baseName + 1 : path;

            bool found = false;
            for (auto& m : g_modules) {
                if (m.path == path) {
                    if (r.end > m.end) m.end = r.end;
                    found = true;
                    break;
                }
            }
            if (!found) {
                MemoryModule m;
                m.path = path;
                m.name = baseName;
                m.base = (uintptr_t)start;
                m.end = (uintptr_t)end;
                g_modules.push_back(m);
            }
        }
    }
    fclose(f);

    LOGI("Memory: %zu modules, %zu ranges", g_modules.size(), g_ranges.size());
}

const std::vector<MemoryModule>& Modules() { return g_modules; }
const std::vector<MemoryRange>& Ranges() { return g_ranges; }

static const MemoryRange* FindRange(uintptr_t addr, size_t len) {
    for (const auto& r : g_ranges) {
        if (addr >= r.start && (addr + len) <= r.end) return &r;
    }
    return nullptr;
}

bool IsReadable(uintptr_t addr, size_t len) {
    const MemoryRange* r = FindRange(addr, len);
    return r && !r->perms.empty() && r->perms[0] == 'r';
}

bool IsWritable(uintptr_t addr, size_t len) {
    const MemoryRange* r = FindRange(addr, len);
    return r && r->perms.size() > 1 && r->perms[1] == 'w';
}

bool Read(uintptr_t addr, void* out, size_t len) {
    if (!IsReadable(addr, len)) return false;
    memcpy(out, (const void*)addr, len);
    return true;
}

bool Write(uintptr_t addr, const void* in, size_t len) {
    if (!IsWritable(addr, len)) return false;
    memcpy((void*)addr, in, len);
    return true;
}

uintptr_t ModuleBase(const std::string& name) {
    for (const auto& m : g_modules) {
        if (m.name == name) return m.base;
    }
    return 0;
}

}
