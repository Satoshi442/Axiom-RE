#include "Disasm.h"

#include <android/log.h>
#include <capstone/capstone.h>

#include <cstring>
#include <vector>

#include "Memory.h"

#define LOG_TAG "Axiom-RE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {
csh g_handle = 0;
bool g_ready = false;
}

namespace Disasm {

bool Initialize() {
    if (cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &g_handle) == CS_ERR_OK) {
        cs_option(g_handle, CS_OPT_DETAIL, CS_OPT_OFF);
        g_ready = true;
        LOGI("Capstone disassembler initialized");
        return true;
    }
    LOGE("Capstone failed to initialize");
    return false;
}

void Shutdown() {
    if (g_ready) cs_close(&g_handle);
    g_ready = false;
}

std::vector<DisasmInstruction> Disassemble(uint64_t addr, size_t maxBytes, size_t maxCount) {
    std::vector<DisasmInstruction> out;
    if (!g_ready || addr == 0) return out;
    if (maxBytes > 4096) maxBytes = 4096;
    if (maxCount == 0 || maxCount > 512) maxCount = 512;

    // SAFE read: 4 bytes at a time through the maps-checked Memory::Read.
    // Unmapped / unreadable address just stops the loop -> empty result, NO crash.
    std::vector<uint8_t> buf;
    buf.reserve(maxBytes);
    for (size_t off = 0; off < maxBytes; off += 4) {
        uint32_t word = 0;
        if (!Memory::Read(addr + off, &word, 4)) break;
        buf.push_back((uint8_t)(word & 0xFF));
        buf.push_back((uint8_t)((word >> 8) & 0xFF));
        buf.push_back((uint8_t)((word >> 16) & 0xFF));
        buf.push_back((uint8_t)((word >> 24) & 0xFF));
    }
    if (buf.empty()) return out;

    cs_insn* insn = nullptr;
    size_t count = cs_disasm(g_handle, buf.data(), buf.size(), addr, maxCount, &insn);
    if (count > 0 && insn) {
        for (size_t i = 0; i < count; i++) {
            DisasmInstruction d;
            d.address = insn[i].address;
            d.mnemonic = insn[i].mnemonic;
            d.opStr = insn[i].op_str;
            out.push_back(d);
        }
        cs_free(insn, count);
    }
    return out;
}

}
