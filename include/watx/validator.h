#ifndef WATX_VALIDATOR_H
#define WATX_VALIDATOR_H

#include <watx/export.h>
#include <watx/model.h>

namespace watx {

WATX_API void validate_module(const Module &module, unsigned workers = 1);

}

#endif
