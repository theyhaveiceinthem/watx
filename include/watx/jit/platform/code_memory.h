#ifndef WATX_JIT_PLATFORM_CODE_MEMORY_H
#define WATX_JIT_PLATFORM_CODE_MEMORY_H
#include <cstddef>
#include <watx/export.h>
namespace watx {
namespace jit_platform {
WATX_API void *publish_code(const unsigned char *bytes, std::size_t size);
WATX_API void release_code(void *memory, std::size_t size);
}
}
#endif
