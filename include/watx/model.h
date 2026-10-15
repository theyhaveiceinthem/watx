#ifndef WATX_MODEL_H
#define WATX_MODEL_H
#include <stdint.h>
#include <string>
#include <vector>
#include <watx/export.h>
#include <watx/text/syntax.h>
namespace watx {
enum ValueType {
    TYPE_I32 = 0x7f,
    TYPE_I64 = 0x7e,
    TYPE_F32 = 0x7d,
    TYPE_F64 = 0x7c,
    TYPE_NONE = 0x40
};
struct FunctionType {
    std::string name;
    std::vector<ValueType> parameters, results;
};
struct Local {
    std::string name;
    ValueType type;
    Local() : type(TYPE_I32) {}
    Local(const std::string &n, ValueType t) : name(n), type(t) {}
};

struct Instruction {
    unsigned char opcode;
    uint32_t immediate, secondary, extended_opcode;
    ValueType block_type;
    std::vector<uint32_t> labels;
    std::vector<unsigned char> bytes;
    explicit Instruction(unsigned char op = 0x01)
        : opcode(op), immediate(0), secondary(0), extended_opcode(0), block_type(TYPE_NONE) {}
};
struct Function {
    std::string name;
    uint32_t type_index;
    std::vector<std::string> parameter_names;
    std::vector<Local> locals;
    std::vector<Node> raw_body;
    std::vector<Instruction> code;
    bool imported;
    std::string import_module, import_name;
    Function() : type_index(0), imported(false) {}
};
struct Limits {
    uint32_t minimum, maximum;
    bool has_maximum;
    Limits() : minimum(0), maximum(0), has_maximum(false) {}
};
struct Table {
    std::string name;
    Limits limits;
    bool imported;
    std::string import_module, import_name;
    Table() : imported(false) {}
};
struct Memory {
    std::string name;
    Limits limits;
    bool imported, shared;
    std::string import_module, import_name;
    Memory() : imported(false), shared(false) {}
};
struct Global {
    std::string name;
    ValueType type;
    bool mutable_, imported;
    std::string import_module, import_name;
    std::vector<Node> raw_init;
    std::vector<Instruction> init;
    Global() : type(TYPE_I32), mutable_(false), imported(false) {}
};
struct Export {
    std::string name;
    unsigned char kind;
    uint32_t index;
    Export() : kind(0), index(0) {}
};
struct DataSegment {
    uint32_t memory_index;
    std::vector<Node> raw_offset;
    std::vector<Instruction> offset;
    std::string bytes;
    DataSegment() : memory_index(0) {}
};
struct ElementSegment {
    uint32_t table_index;
    std::vector<Node> raw_offset;
    std::vector<Instruction> offset;
    std::vector<uint32_t> functions;
    ElementSegment() : table_index(0) {}
};
struct Module {
    std::vector<FunctionType> types;
    std::vector<Function> functions;
    std::vector<Table> tables;
    std::vector<Memory> memories;
    std::vector<Global> globals;
    std::vector<Export> exports;
    std::vector<DataSegment> data;
    std::vector<ElementSegment> elements;
    bool has_start;
    uint32_t start;
    Module() : has_start(false), start(0) {}
};
WATX_API ValueType value_type(const std::string &name);
WATX_API uint32_t intern_type(Module &module, const FunctionType &type);
WATX_API uint32_t function_index(const Module &module, const std::string &name);
WATX_API uint32_t type_index(const Module &module, const std::string &name);
WATX_API uint32_t global_index(const Module &module, const std::string &name);
WATX_API uint32_t table_index(const Module &module, const std::string &name);
WATX_API uint32_t memory_index(const Module &module, const std::string &name);
}
#endif
