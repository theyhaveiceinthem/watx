#ifndef WATX_JIT_ARM64_PLATFORM_EMITTER_H
#define WATX_JIT_ARM64_PLATFORM_EMITTER_H
#include <cstddef>
#include <vector>
#include <watx/export.h>
#include <watx/runtime/runtime.h>
namespace watx {
namespace jit_platform {
typedef uint32_t (*StepFunction)(RuntimeFrame *, uint32_t);
WATX_API void emit_arm64(const std::vector<unsigned> &instructions, StepFunction step,
                         std::vector<unsigned char> &bytes);
}
}
#endif
