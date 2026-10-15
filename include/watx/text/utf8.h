#ifndef WATX_TEXT_UTF8_H
#define WATX_TEXT_UTF8_H
#include <string>
#include <watx/export.h>
namespace watx {
WATX_API bool valid_utf8(const std::string &text);
}
#endif
