#include "Disasm.h"

#include <capstone/capstone.h>
#include <android/log.h>

#define LOG_TAG "Axiom-RE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {
    csh g_handle = 0;
    bool g_initialized = false;
}

namespace Disasm {

bool Initialize() {
    cs_err err = cs_open(CS_ARCH_AARCH64, CS_MODE_ARM, &g_handle);
    if (err != CS_ERR_OK) {
        LOGE("Capstone init failed: %s", cs_strerror(err));
        return false;
    }
    cs_option(g_handle, CS_OPT_DETAIL, CS_OPT_ON);
    g_initialized = true;
    LOGI("Capstone disassembler initialized");
    return true;
}

void Shutdown() {
    if (g_initialized) {
        cs_close(&g_handle);
        g_initialized = false;
    }
}

std::vector<DisasmInstruction> Disassemble(uint64_t address, size_t size, int count) {
    std::vector<DisasmInstruction> results;
    if (!g_initialized) return results;

    const uint8_t* code = reinterpret_cast<const uint8_t*>(address);
    cs_insn* insn = nullptr;
    size_t disasmCount = cs_disasm(g_handle, code, size, address, count, &insn);

    if (disasmCount > 0) {
        for (size_t i = 0; i < disasmCount; i++) {
            DisasmInstruction inst;
            inst.address = insn[i].address;
            inst.mnemonic = insn[i].mnemonic;
            inst.opStr = insn[i].op_str;
            inst.fullText = std::string(insn[i].mnemonic) + " " + insn[i].op_str;
            results.push_back(inst);
        }
        cs_free(insn, disasmCount);
    }

    return results;
}

}
