#ifndef WATX_TEXT_LITERALS_H
#define WATX_TEXT_LITERALS_H
#include <stdint.h>
#include <string>
#include <vector>
#include <watx/export.h>
namespace watx {
WATX_API uint32_t parse_natural(const std::string &text);
WATX_API uint64_t parse_integer_bits(const std::string &text, unsigned int width);
WATX_API std::vector<unsigned char> parse_float_bits(const std::string &text, unsigned int width);
}
#endif
