#ifndef WATX_TEXT_SYNTAX_H
#define WATX_TEXT_SYNTAX_H
#include <string>
#include <vector>
#include <watx/export.h>
#include <watx/token.h>
namespace watx {
struct Node {
    bool list;
    std::string text;
    std::vector<Node> children;
    Node() : list(false) {}
    explicit Node(const std::string &atom) : list(false), text(atom) {}
};
WATX_API std::vector<Node> parse_syntax(const std::vector<Token> &tokens);
WATX_API bool is_form(const Node &node, const std::string &head);
WATX_API std::string form_head(const Node &node);
WATX_API std::string decode_string(const std::string &text);
}
#endif
