#include <stdexcept>
#include <watx/model.h>
#include <watx/text/literals.h>
namespace watx {
ValueType value_type(const std::string &name) {
    if (name == "i32")
        return TYPE_I32;
    if (name == "i64")
        return TYPE_I64;
    if (name == "f32")
        return TYPE_F32;
    if (name == "f64")
        return TYPE_F64;
    throw std::runtime_error("unknown MVP value type '" + name + "'");
}
uint32_t intern_type(Module &module, const FunctionType &type) {
    for (std::size_t i = 0; i < module.types.size(); ++i)
        if (module.types[i].parameters == type.parameters &&
            module.types[i].results == type.results)
            return static_cast<uint32_t>(i);
    module.types.push_back(type);
    return static_cast<uint32_t>(module.types.size() - 1);
}
namespace {
template <class T>
uint32_t index_of(const std::vector<T> &values, const std::string &name, const char *kind) {
    if (!name.empty() && name[0] == '$') {
        for (std::size_t i = 0; i < values.size(); ++i)
            if (values[i].name == name)
                return static_cast<uint32_t>(i);
        throw std::runtime_error(std::string("unknown ") + kind + " identifier '" + name + "'");
    }
    uint32_t index = parse_natural(name);
    if (index >= values.size())
        throw std::runtime_error(std::string(kind) + " index out of range: " + name);
    return index;
}
}
uint32_t function_index(const Module &m, const std::string &n) {
    return index_of(m.functions, n, "function");
}
uint32_t type_index(const Module &m, const std::string &n) {
    return index_of(m.types, n, "type");
}
uint32_t global_index(const Module &m, const std::string &n) {
    return index_of(m.globals, n, "global");
}
uint32_t table_index(const Module &m, const std::string &n) {
    return index_of(m.tables, n, "table");
}
uint32_t memory_index(const Module &m, const std::string &n) {
    return index_of(m.memories, n, "memory");
}
}
