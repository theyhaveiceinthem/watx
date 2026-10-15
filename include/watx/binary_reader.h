#ifndef WATX_BINARY_READER_H
#define WATX_BINARY_READER_H
#include <vector>
#include <watx/export.h>
#include <watx/model.h>
namespace watx {
WATX_API Module read_module(const std::vector<unsigned char> &bytes);
}
#endif
