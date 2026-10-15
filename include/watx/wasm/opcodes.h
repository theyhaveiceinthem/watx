#ifndef WATX_WASM_OPCODES_H
#define WATX_WASM_OPCODES_H

#include <stdint.h>
#include <string>
#include <watx/export.h>

namespace watx {

WATX_API bool numeric_opcode(const std::string &name, unsigned char &opcode);
WATX_API bool memory_opcode(const std::string &name, unsigned char &opcode);
WATX_API bool atomic_opcode(const std::string &name, uint32_t &subopcode);

WATX_API uint32_t natural_alignment(unsigned char opcode);
WATX_API uint32_t atomic_natural_alignment(uint32_t subopcode);

}

#endif
