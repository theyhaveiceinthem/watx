#ifndef WATX_RUNTIME_NATIVE_FRAME_H
#define WATX_RUNTIME_NATIVE_FRAME_H
#include <cstddef>
#include <watx/export.h>
namespace watx {
struct RuntimeValue;
struct RuntimeFrame;
struct RuntimeNativeFrame {
    RuntimeValue *stack;
    std::size_t size;
    RuntimeValue *locals;
    std::size_t capacity;
};
WATX_API RuntimeNativeFrame *runtime_native_frame(RuntimeFrame *frame);
}
#endif
