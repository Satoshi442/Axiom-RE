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
    // Capstone 6 renamed ARM64 -> AARCH64. CS_MODE_LITTLE_ENDIAN == CS_MODE_ARM == 0.
    if (cs_open(CS_ARCH_AARCH64, CS_MODE_LITTLE_ENDIAN, &g_handle) == CS_ERR_OK) {
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

// Must match the declaration in Disasm.h exactly: (uint64_t, size_t, int).
// It was (uint64_t, size_t, size_t), a different overload, which the linker
// reported as "undefined symbol Disasm::Disassemble(unsigned long, unsigned long, int)".
std::vector<DisasmInstruction> Disassemble(uint64_t addr, size_t maxBytes, int count) {
    std::vector<DisasmInstruction> out;
    if (!g_ready || addr == 0) return out;
    if (maxBytes > 4096) maxBytes = 4096;
    size_t maxCount = count <= 0 ? 0 : static_cast<size_t>(count);
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
    size_t n = cs_disasm(g_handle, buf.data(), buf.size(), addr, maxCount, &insn);
    if (n > 0 && insn) {
        for (size_t i = 0; i < n; i++) {
            DisasmInstruction d;
            d.address = insn[i].address;
            d.mnemonic = insn[i].mnemonic;
            d.opStr = insn[i].op_str;
            d.fullText = d.mnemonic + " " + d.opStr;  // header declares it; keep it filled
            out.push_back(d);
        }
        cs_free(insn, n);
    }
    return out;
}

}
