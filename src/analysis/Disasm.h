#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct DisasmInstruction {
    uint64_t address;
    std::string mnemonic;
    std::string opStr;
    std::string fullText;
};

namespace Disasm {

bool Initialize();
void Shutdown();
std::vector<DisasmInstruction> Disassemble(uint64_t address, size_t size, int count = 20);

}
