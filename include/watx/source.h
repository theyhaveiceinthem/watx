#ifndef WATX_SOURCE_H
#define WATX_SOURCE_H

#include <watx/export.h>
#include <watx/token.h>

#include <set>
#include <string>
#include <vector>

namespace watx {

WATX_API void append_expanded_file(const std::string &path, std::vector<Token> &out,
                                   std::set<std::string> *dependencies = 0);
WATX_API void append_expanded_source(const std::string &source, const std::string &directory,
                                     std::vector<Token> &out,
                                     std::set<std::string> *dependencies = 0);
WATX_API std::string canonical_source_path(const std::string &path);
WATX_API std::string directory_of(const std::string &path);
WATX_API std::string read_file(const std::string &path);

}

#endif
