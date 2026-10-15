#ifndef WATX_PARSER_H
#define WATX_PARSER_H
#include <watx/export.h>
#include <watx/model.h>
#include <watx/token.h>
namespace watx {
WATX_API Module parse_module(const std::vector<Token> &tokens);
}
#endif
