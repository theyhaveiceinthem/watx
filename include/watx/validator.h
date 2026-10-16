#ifndef WATX_VALIDATOR_H
#define WATX_VALIDATOR_H

#include <cstddef>
#include <watx/export.h>
#include <watx/model.h>

namespace watx {

WATX_API void validate_module(const Module &module, unsigned workers = 1);
WATX_API std::size_t analyze_function_stack(const Module &module, std::size_t index);

}

#endif
