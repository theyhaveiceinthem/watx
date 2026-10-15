#include <stdexcept>
#include <watx/text/syntax.h>
#include <watx/text/utf8.h>
#include <watx/token.h>
namespace watx {
namespace {
bool space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}
bool idchar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           std::string("!#$%&'*+-./:<=>?@\\^_`|~").find(c) != std::string::npos;
}
}
std::vector<Token> tokenize(const std::string &source) {
    if (!valid_utf8(source))
        throw std::runtime_error("invalid UTF-8 source encoding");
    std::vector<Token> tokens;
    std::size_t i = 0;
    while (i < source.size()) {
        if (space(source[i])) {
            ++i;
            continue;
        }
        if (source.compare(i, 2, ";;") == 0) {
            while (i < source.size() && source[i] != '\n')
                ++i;
            continue;
        }
        if (source.compare(i, 2, "(;") == 0) {
            i += 2;
            unsigned int depth = 1;
            while (i < source.size() && depth) {
                if (source.compare(i, 2, "(;") == 0) {
                    ++depth;
                    i += 2;
                } else if (source.compare(i, 2, ";)") == 0) {
                    --depth;
                    i += 2;
                } else
                    ++i;
            }
            if (depth)
                throw std::runtime_error("unterminated block comment");
            continue;
        }
        if (source[i] == '(' || source[i] == ')') {
            tokens.push_back(Token(std::string(1, source[i++])));
            continue;
        }
        if (source[i] == '"') {
            std::size_t start = i++;
            bool closed = false;
            while (i < source.size()) {
                if (source[i] == '\\') {
                    i += i + 1 < source.size() ? 2 : 1;
                } else if (source[i++] == '"') {
                    closed = true;
                    break;
                }
            }
            if (!closed)
                throw std::runtime_error("unterminated string literal");
            std::string text = source.substr(start, i - start);
            decode_string(text);
            tokens.push_back(Token(text));
            continue;
        }
        std::size_t start = i;
        while (i < source.size() && idchar(source[i]))
            ++i;
        if (i == start)
            throw std::runtime_error("invalid character outside string or comment");
        std::string atom = source.substr(start, i - start);
        if (atom == "$")
            throw std::runtime_error("identifier requires a name after '$'");
        tokens.push_back(Token(atom));
    }
    return tokens;
}
}
