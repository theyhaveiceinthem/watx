#ifndef WATX_HOST_H
#define WATX_HOST_H
#include <map>
#include <string>
#include <watx/export.h>
#include <watx/runtime/imports.h>
namespace watx {
class WATX_API HostModule {
  public:
    explicit HostModule(const std::string &name);
    const std::string &name() const;
    void add_function(const std::string &name, const FunctionType &type,
                      RuntimeFunctionImport::Callback callback, void *context = 0);
    void add_memory(const std::string &name, RuntimeMemory *memory);
    void add_table(const std::string &name, RuntimeTable *table);
    void add_global(const std::string &name, RuntimeGlobal *global);

  private:
    struct FunctionBinding {
        FunctionType type;
        RuntimeFunctionImport function;
    };
    std::string name_;
    std::map<std::string, FunctionBinding> functions_;
    std::map<std::string, RuntimeMemory *> memories_;
    std::map<std::string, RuntimeTable *> tables_;
    std::map<std::string, RuntimeGlobal *> globals_;
    void require_new_name(const std::string &name) const;
    friend class Host;
};
class WATX_API Host {
  public:
    void add_module(const HostModule &module);
    RuntimeImports bind(const Module &module) const;

  private:
    std::map<std::string, HostModule> modules_;
    const HostModule &find_module(const std::string &name) const;
};
}
#endif
