#include <stdexcept>
#include <watx/host.h>
namespace watx {
HostModule::HostModule(const std::string &name) : name_(name) {}
const std::string &HostModule::name() const {
    return name_;
}
void HostModule::require_new_name(const std::string &name) const {
    if (functions_.count(name) || memories_.count(name) || tables_.count(name) ||
        globals_.count(name))
        throw std::runtime_error("duplicate host export '" + name_ + "." + name + "'");
}
void HostModule::add_function(const std::string &name, const FunctionType &type,
                              RuntimeFunctionImport::Callback callback, void *context) {
    require_new_name(name);
    if (!callback)
        throw std::runtime_error("host function callback must not be null");
    FunctionBinding binding;
    binding.type = type;
    binding.function = RuntimeFunctionImport(callback, context);
    functions_.insert(std::make_pair(name, binding));
}
void HostModule::add_memory(const std::string &name, RuntimeMemory *memory) {
    require_new_name(name);
    if (!memory)
        throw std::runtime_error("host memory must not be null");
    memories_.insert(std::make_pair(name, memory));
}
void HostModule::add_table(const std::string &name, RuntimeTable *table) {
    require_new_name(name);
    if (!table)
        throw std::runtime_error("host table must not be null");
    tables_.insert(std::make_pair(name, table));
}
void HostModule::add_global(const std::string &name, RuntimeGlobal *global) {
    require_new_name(name);
    if (!global)
        throw std::runtime_error("host global must not be null");
    globals_.insert(std::make_pair(name, global));
}
void Host::add_module(const HostModule &module) {
    if (modules_.count(module.name()))
        throw std::runtime_error("duplicate host module '" + module.name() + "'");
    modules_.insert(std::make_pair(module.name(), module));
}
const HostModule &Host::find_module(const std::string &name) const {
    std::map<std::string, HostModule>::const_iterator found = modules_.find(name);
    if (found == modules_.end())
        throw std::runtime_error("host module '" + name + "' is not registered");
    return found->second;
}
RuntimeImports Host::bind(const Module &module) const {
    RuntimeImports imports;
    for (std::size_t i = 0; i < module.functions.size(); ++i) {
        const Function &function = module.functions[i];
        if (!function.imported)
            continue;
        const HostModule &host = find_module(function.import_module);
        std::map<std::string, HostModule::FunctionBinding>::const_iterator found =
            host.functions_.find(function.import_name);
        if (found == host.functions_.end())
            throw std::runtime_error("host function '" + function.import_module + "." +
                                     function.import_name + "' is not registered");
        if (function.type_index >= module.types.size())
            throw std::runtime_error("imported function has an invalid type index");
        const FunctionType &expected = module.types[function.type_index];
        if (found->second.type.parameters != expected.parameters ||
            found->second.type.results != expected.results)
            throw std::runtime_error("host function signature mismatch for '" +
                                     function.import_module + "." + function.import_name + "'");
        imports.add_function(function.import_module, function.import_name, found->second.function);
    }
    for (std::size_t i = 0; i < module.memories.size(); ++i) {
        const Memory &memory = module.memories[i];
        if (!memory.imported)
            continue;
        const HostModule &host = find_module(memory.import_module);
        std::map<std::string, RuntimeMemory *>::const_iterator found =
            host.memories_.find(memory.import_name);
        if (found == host.memories_.end())
            throw std::runtime_error("host memory is not registered: " + memory.import_module +
                                     "." + memory.import_name);
        imports.add_memory(memory.import_module, memory.import_name, found->second);
    }
    for (std::size_t i = 0; i < module.tables.size(); ++i) {
        const Table &table = module.tables[i];
        if (!table.imported)
            continue;
        const HostModule &host = find_module(table.import_module);
        std::map<std::string, RuntimeTable *>::const_iterator found =
            host.tables_.find(table.import_name);
        if (found == host.tables_.end())
            throw std::runtime_error("host table is not registered: " + table.import_module + "." +
                                     table.import_name);
        imports.add_table(table.import_module, table.import_name, found->second);
    }
    for (std::size_t i = 0; i < module.globals.size(); ++i) {
        const Global &global = module.globals[i];
        if (!global.imported)
            continue;
        const HostModule &host = find_module(global.import_module);
        std::map<std::string, RuntimeGlobal *>::const_iterator found =
            host.globals_.find(global.import_name);
        if (found == host.globals_.end())
            throw std::runtime_error("host global is not registered: " + global.import_module +
                                     "." + global.import_name);
        imports.add_global(global.import_module, global.import_name, found->second);
    }
    return imports;
}
}
