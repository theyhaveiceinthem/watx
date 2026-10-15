#ifndef WATX_INSTRUCTIONS_H
#define WATX_INSTRUCTIONS_H
#include <watx/export.h>
#include <watx/model.h>
namespace watx {
WATX_API void compile_module_instructions(Module &module, unsigned workers = 1);
}
#endif
