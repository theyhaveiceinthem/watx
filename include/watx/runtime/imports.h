#ifndef WATX_RUNTIME_IMPORTS_H
#define WATX_RUNTIME_IMPORTS_H
#include <map>
#include <utility>
#include <watx/runtime/runtime.h>
namespace watx {
typedef std::pair<std::string, std::string> RuntimeImportKey;
struct RuntimeImports {
    std::map<RuntimeImportKey, RuntimeFunctionImport> functions;
    std::map<RuntimeImportKey, RuntimeMemory *> memories;
    std::map<RuntimeImportKey, RuntimeTable *> tables;
    std::map<RuntimeImportKey, RuntimeGlobal *> globals;
    static RuntimeImportKey key(const std::string &module, const std::string &name) {
        return RuntimeImportKey(module, name);
    }
    void add_function(const std::string &module, const std::string &name,
                      const RuntimeFunctionImport &value) {
        functions[key(module, name)] = value;
    }
    void add_memory(const std::string &module, const std::string &name, RuntimeMemory *value) {
        memories[key(module, name)] = value;
    }
    void add_table(const std::string &module, const std::string &name, RuntimeTable *value) {
        tables[key(module, name)] = value;
    }
    void add_global(const std::string &module, const std::string &name, RuntimeGlobal *value) {
        globals[key(module, name)] = value;
    }
};
}
#endif
