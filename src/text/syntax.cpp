#include <stdexcept>
#include <watx/text/syntax.h>

namespace watx {
namespace {
Node read_node(const std::vector<Token> &tokens, std::size_t &pos, unsigned depth) {
    if (depth > 1024)
        throw std::runtime_error("text nesting limit exceeded");
    if (pos == tokens.size())
        throw std::runtime_error("unexpected end of text");
    std::string text = tokens[pos++].text;
    if (text == ")")
        throw std::runtime_error("unexpected ')'");
    if (text != "(")
        return Node(text);
    Node result;
    result.list = true;
    while (pos < tokens.size() && tokens[pos].text != ")")
        result.children.push_back(read_node(tokens, pos, depth + 1));
    if (pos == tokens.size())
        throw std::runtime_error("unterminated parenthesized expression");
    ++pos;
    return result;
}
int hex(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
void utf8(std::string &out, unsigned long cp) {
    if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
        throw std::runtime_error("invalid Unicode scalar in string");
    if (cp < 0x80)
        out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xc0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 63));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xe0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 63));
        out += static_cast<char>(0x80 | (cp & 63));
    } else {
        out += static_cast<char>(0xf0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 63));
        out += static_cast<char>(0x80 | ((cp >> 6) & 63));
        out += static_cast<char>(0x80 | (cp & 63));
    }
}
}
std::vector<Node> parse_syntax(const std::vector<Token> &tokens) {
    std::vector<Node> nodes;
    std::size_t pos = 0;
    while (pos < tokens.size())
        nodes.push_back(read_node(tokens, pos, 0));
    return nodes;
}
bool is_form(const Node &node, const std::string &head) {
    return node.list && !node.children.empty() && !node.children[0].list &&
           node.children[0].text == head;
}
std::string form_head(const Node &node) {
    if (!node.list || node.children.empty() || node.children[0].list)
        throw std::runtime_error("expected an expression with a keyword");
    return node.children[0].text;
}
std::string decode_string(const std::string &text) {
    if (text.size() < 2 || text[0] != '"' || text[text.size() - 1] != '"')
        throw std::runtime_error("expected a quoted string");
    std::string out;
    for (std::size_t i = 1; i + 1 < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '"' || c < 0x20 || c == 0x7f)
            throw std::runtime_error("invalid character in string");
        if (c != '\\') {
            out += static_cast<char>(c);
            continue;
        }
        if (++i + 1 >= text.size())
            throw std::runtime_error("unterminated string escape");
        char escape = text[i];
        if (escape == 't')
            out += '\t';
        else if (escape == 'n')
            out += '\n';
        else if (escape == 'r')
            out += '\r';
        else if (escape == '"' || escape == '\'' || escape == '\\')
            out += escape;
        else if (hex(escape) >= 0) {
            if (i + 2 >= text.size() || hex(text[i + 1]) < 0)
                throw std::runtime_error("byte escapes require two hexadecimal digits");
            out += static_cast<char>((hex(escape) << 4) | hex(text[++i]));
        } else if (escape == 'u' && i + 1 < text.size() && text[i + 1] == '{') {
            i += 2;
            unsigned long cp = 0;
            bool digit = false;
            while (i + 1 < text.size() && text[i] != '}') {
                if (text[i] == '_') {
                    if (!digit || i + 2 >= text.size() || hex(text[i + 1]) < 0)
                        throw std::runtime_error("invalid underscore in Unicode escape");
                    ++i;
                }
                int d = hex(text[i]);
                if (d < 0 || cp > 0x10ffffUL / 16)
                    throw std::runtime_error("invalid Unicode escape");
                cp = cp * 16 + static_cast<unsigned long>(d);
                digit = true;
                ++i;
            }
            if (!digit || i + 1 >= text.size() || text[i] != '}')
                throw std::runtime_error("invalid Unicode escape");
            utf8(out, cp);
        } else
            throw std::runtime_error("unknown string escape");
    }
    return out;
}
}
