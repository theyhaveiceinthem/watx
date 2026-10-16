#ifndef WATX_JIT_X86_64_PLATFORM_EMITTER_H
#define WATX_JIT_X86_64_PLATFORM_EMITTER_H
#include <cstddef>
#include <vector>
#include <watx/export.h>
#include <watx/runtime/runtime.h>
namespace watx {
namespace jit_platform {
typedef uint32_t (*StepFunction)(RuntimeFrame *, uint32_t);
WATX_API void emit_x86_64(const std::vector<unsigned> &instructions, StepFunction step,
                          std::vector<unsigned char> &bytes);
WATX_API void emit_fast_x86_64(const std::vector<Instruction> &instructions, StepFunction step,
                               std::vector<unsigned char> &bytes);
}
}
#endif
