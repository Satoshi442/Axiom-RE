#include "Memory.h"

#include <android/log.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>

#define LOG_TAG "Axiom-RE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {

std::vector<MemoryModule> g_modules;

struct Range {
    uintptr_t start;
    uintptr_t end;
    char perms[5];
};
std::vector<Range> g_ranges;

// ----------------------------------------------------------------------------
// Crash-proof copy. Reading a bad address with memcpy kills the game (SIGSEGV),
// and the ranges below can be stale (memory freed since the last Refresh).
// write(pipe, addr, n) / read(pipe, addr, n) make the KERNEL touch the address:
// a bad or protected page gives EFAULT instead of a crash. Only plain pipe I/O
// is used, so there are no seccomp surprises.
// ----------------------------------------------------------------------------
std::mutex g_pipeMutex;
int g_pipe[2] = {-1, -1};

bool EnsurePipe() {
    if (g_pipe[0] >= 0) return true;
    if (pipe2(g_pipe, O_NONBLOCK | O_CLOEXEC) != 0) { g_pipe[0] = g_pipe[1] = -1; return false; }
    return true;
}

void DrainPipe() {
    char scratch[256];
    while (read(g_pipe[0], scratch, sizeof(scratch)) > 0) {}
}

constexpr size_t kChunk = 4096;   // well under the 64 KiB pipe buffer

bool SafeRead(uintptr_t addr, void* out, size_t len) {
    std::lock_guard<std::mutex> lock(g_pipeMutex);
    if (!EnsurePipe()) return false;
    auto* dst = static_cast<uint8_t*>(out);
    size_t done = 0;
    while (done < len) {
        size_t chunk = std::min(len - done, kChunk);
        ssize_t w = write(g_pipe[1], reinterpret_cast<const void*>(addr + done), chunk);  // kernel reads target memory
        if (w != static_cast<ssize_t>(chunk)) { DrainPipe(); return false; }
        size_t got = 0;
        while (got < chunk) {
            ssize_t r = read(g_pipe[0], dst + done + got, chunk - got);
            if (r <= 0) { DrainPipe(); return false; }
            got += static_cast<size_t>(r);
        }
        done += chunk;
    }
    return true;
}

bool SafeWrite(uintptr_t addr, const void* in, size_t len) {
    std::lock_guard<std::mutex> lock(g_pipeMutex);
    if (!EnsurePipe()) return false;
    auto* src = static_cast<const uint8_t*>(in);
    size_t done = 0;
    while (done < len) {
        size_t chunk = std::min(len - done, kChunk);
        if (write(g_pipe[1], src + done, chunk) != static_cast<ssize_t>(chunk)) { DrainPipe(); return false; }
        ssize_t r = read(g_pipe[0], reinterpret_cast<void*>(addr + done), chunk);          // kernel writes target memory
        if (r != static_cast<ssize_t>(chunk)) { DrainPipe(); return false; }
        done += chunk;
    }
    return true;
}

const Range* FindRange(uintptr_t addr, size_t len) {
    for (const auto& r : g_ranges)
        if (addr >= r.start && (addr + len) <= r.end) return &r;
    return nullptr;
}

}  // namespace

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

        g_ranges.push_back({(uintptr_t)start, (uintptr_t)end, {perms[0], perms[1], perms[2], perms[3], 0}});

        if (path[0] == '/' && strstr(path, ".so")) {
            const char* baseName = strrchr(path, '/');
            baseName = baseName ? baseName + 1 : path;

            bool found = false;
            for (auto& m : g_modules) {
                if (m.path == path) {
                    if (end > m.end) m.end = end;
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

bool IsReadable(uintptr_t addr, size_t len) {
    const Range* r = FindRange(addr, len);
    return r && r->perms[0] == 'r';
}

bool IsWritable(uintptr_t addr, size_t len) {
    const Range* r = FindRange(addr, len);
    return r && r->perms[1] == 'w';
}

// No maps pre-check: it can be stale in either direction, and the kernel
// check is the authoritative one (and crash-proof).
bool Read(uintptr_t addr, void* out, size_t len) {
    if (!out || len == 0) return false;
    return SafeRead(addr, out, len);
}

bool Write(uintptr_t addr, const void* in, size_t len) {
    if (!in || len == 0) return false;
    return SafeWrite(addr, in, len);   // read-only pages are refused by the kernel
}

uintptr_t ModuleBase(const std::string& name) {
    for (const auto& m : g_modules)
        if (m.name == name) return m.base;
    return 0;
}

bool Describe(uintptr_t addr, MemoryInfo& out) {
    out = MemoryInfo{};
    const Range* r = FindRange(addr, 1);
    if (r) {
        out.mapped = true;
        out.regionStart = r->start;
        out.regionEnd = r->end;
        memcpy(out.perms, r->perms, sizeof(out.perms));
    }
    for (const auto& m : g_modules) {
        if (addr >= m.base && addr < m.end) {
            out.inModule = true;
            out.moduleName = m.name;
            out.moduleOffset = addr - m.base;
            break;
        }
    }
    return out.mapped;
}

std::string Format(uintptr_t addr) {
    MemoryInfo info;
    Describe(addr, info);
    char buf[320];
    if (info.inModule)
        snprintf(buf, sizeof(buf), "%s+0x%llX", info.moduleName.c_str(), (unsigned long long)info.moduleOffset);
    else
        snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)addr);
    return buf;
}

}
