#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <watx/source.h>
#include <watx/text/syntax.h>
namespace watx {
std::string read_file(const std::string &path) {
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input)
        throw std::runtime_error("could not open input file '" + path + "'");
    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad())
        throw std::runtime_error("failed reading input file '" + path + "'");
    return contents.str();
}
std::string directory_of(const std::string &path) {
    std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}
namespace {
std::string join_path(const std::string &base, const std::string &path) {
    if (!path.empty() && (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':')))
        return path;
    return base + "/" + path;
}
void expand_file(const std::string &path, std::vector<Token> &out, std::set<std::string> &stack,
                 std::set<std::string> *dependencies);
void expand_tokens(const std::vector<Token> &tokens, const std::string &directory,
                   std::vector<Token> &out, std::set<std::string> &stack,
                   std::set<std::string> *dependencies) {
    for (std::size_t i = 0; i < tokens.size();) {
        if (tokens[i].text == "(" && i + 1 < tokens.size() && tokens[i + 1].text == "include") {
            if (i + 3 >= tokens.size() || tokens[i + 3].text != ")")
                throw std::runtime_error("include syntax is (include \"path.wat\")");
            std::string include = decode_string(tokens[i + 2].text);
            if (include.empty() || include.find('\0') != std::string::npos)
                throw std::runtime_error("invalid include path");
            std::string path = join_path(directory, include);
            if (dependencies)
                dependencies->insert(canonical_source_path(path));
            expand_file(path, out, stack, dependencies);
            i += 4;
        } else
            out.push_back(tokens[i++]);
    }
}
void expand_file(const std::string &path, std::vector<Token> &out, std::set<std::string> &stack,
                 std::set<std::string> *dependencies) {
    if (path.empty() || path.find('\0') != std::string::npos)
        throw std::runtime_error("invalid input path");
    if (stack.size() >= 256)
        throw std::runtime_error("include nesting limit exceeded");
    std::string key = canonical_source_path(path);
    if (dependencies && !stack.empty())
        dependencies->insert(key);
    if (stack.find(key) != stack.end())
        throw std::runtime_error("include cycle detected at '" + path + "'");
    stack.insert(key);
    expand_tokens(tokenize(read_file(path)), directory_of(path), out, stack, dependencies);
    stack.erase(key);
}
}
std::string canonical_source_path(const std::string &path) {
    if (path.empty() || path.find('\0') != std::string::npos)
        throw std::runtime_error("invalid input path");
#ifdef _WIN32
    char *resolved = _fullpath(0, path.c_str(), 0);
#else
    char *resolved = realpath(path.c_str(), 0);
#endif
    if (!resolved)
        throw std::runtime_error("could not resolve input file '" + path + "'");
    std::string key(resolved);
    std::free(resolved);
    return key;
}
void append_expanded_file(const std::string &path, std::vector<Token> &out,
                          std::set<std::string> *dependencies) {
    std::set<std::string> stack;
    expand_file(path, out, stack, dependencies);
}
void append_expanded_source(const std::string &source, const std::string &directory,
                            std::vector<Token> &out, std::set<std::string> *dependencies) {
    std::set<std::string> stack;
    expand_tokens(tokenize(source), directory, out, stack, dependencies);
}
}
