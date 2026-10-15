#ifndef WATX_TOKEN_H
#define WATX_TOKEN_H

#include <string>
#include <vector>
#include <watx/export.h>

namespace watx {

struct Token {
    std::string text;
    Token() {}
    explicit Token(const std::string &value) : text(value) {}
};

WATX_API std::vector<Token> tokenize(const std::string &source);

}

#endif
