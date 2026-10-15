#ifndef WATX_BINARY_WRITER_H
#define WATX_BINARY_WRITER_H

#include <stdint.h>
#include <watx/export.h>
#include <watx/model.h>

namespace watx {

WATX_API void append_u32(std::vector<unsigned char> &out, unsigned long value);
WATX_API void append_i32(std::vector<unsigned char> &out, long value);
WATX_API void append_i64(std::vector<unsigned char> &out, int64_t value);
WATX_API std::vector<unsigned char> write_module(const Module &module);

}

#endif
