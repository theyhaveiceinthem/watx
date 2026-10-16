#include <watx/platform/threads.h>
#include <watx/text/utf8.h>
#include <watx/validator.h>
#include <watx/wasm/opcodes.h>

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace watx {
namespace {

static void fail(const std::string &what) {
    throw std::runtime_error(what);
}

static bool valid_value_type(ValueType type) {
    return type == TYPE_I32 || type == TYPE_I64 || type == TYPE_F32 || type == TYPE_F64;
}

static std::string at(const std::string &what, std::size_t index) {
    std::ostringstream out;
    out << what << " at index " << index;
    return out.str();
}

static void check_name(const std::string &s, const std::string &what) {
    if (!valid_utf8(s))
        fail(what + " is not valid UTF-8");
}

struct Control {
    unsigned char kind;
    std::size_t height;
    ValueType result;
    bool unreachable;
    bool saw_else;
    Control(unsigned char k, std::size_t h, ValueType r)
        : kind(k), height(h), result(r), unreachable(false), saw_else(false) {}
};

class FunctionValidator {
  public:
    FunctionValidator(const Module &m, std::size_t index)
        : module_(m), function_(m.functions[index]), index_(index), type_(get_type()), maximum_(0) {
    }

    std::size_t maximum_stack() const {
        return maximum_;
    }

    void run() {
        if (function_.imported) {
            if (!function_.code.empty() || !function_.locals.empty())
                fail(at("imported function has a body", index_));
            return;
        }
        locals_ = type_.parameters;
        for (std::size_t i = 0; i < function_.locals.size(); ++i) {
            if (!valid_value_type(function_.locals[i].type))
                fail(at("invalid local type", i));
            locals_.push_back(function_.locals[i].type);
        }
        if (function_.code.empty())
            fail(at("function has no final end", index_));
        controls_.push_back(Control(0, 0, type_.results.empty() ? TYPE_NONE : type_.results[0]));
        bool finished = false;
        for (std::size_t pc = 0; pc < function_.code.size(); ++pc) {
            if (finished)
                fail(at("instruction follows function end", pc));
            const Instruction &ins = function_.code[pc];
            validate_instruction(ins, pc);
            if (ins.opcode == 0x0b && controls_.empty())
                finished = true;
        }
        if (!finished || !controls_.empty())
            fail(at("function body is not properly terminated", index_));
    }

  private:
    const Module &module_;
    const Function &function_;
    std::size_t index_;
    const FunctionType &type_;
    std::size_t maximum_;
    std::vector<ValueType> locals_, stack_;
    std::vector<Control> controls_;

    const FunctionType &get_type() const {
        if (function_.type_index >= module_.types.size())
            fail(at("function has invalid type index", index_));
        return module_.types[function_.type_index];
    }

    Control &current() {
        if (controls_.empty())
            fail("instruction outside function body");
        return controls_.back();
    }

    void push(ValueType t) {
        stack_.push_back(t);
        if (stack_.size() > maximum_)
            maximum_ = stack_.size();
    }

    ValueType pop_any() {
        Control &c = current();
        if (stack_.size() == c.height && c.unreachable)
            return TYPE_NONE;
        if (stack_.size() <= c.height)
            fail("operand stack underflow");
        ValueType t = stack_.back();
        stack_.pop_back();
        return t;
    }

    void pop(ValueType expected) {
        ValueType actual = pop_any();
        if (actual != TYPE_NONE && actual != expected)
            fail("operand type mismatch");
    }

    void pop_types(ValueType t) {
        if (t != TYPE_NONE)
            pop(t);
    }

    void mark_unreachable() {
        Control &c = current();
        stack_.resize(c.height);
        c.unreachable = true;
    }

    void finish_arm(Control &c) {
        pop_types(c.result);
        if (stack_.size() != c.height)
            fail("control construct leaves extra operands");
    }

    void branch_types(std::size_t depth, std::vector<ValueType> &out) {
        if (depth >= controls_.size())
            fail("branch depth is out of range");
        const Control &target = controls_[controls_.size() - 1 - depth];
        if (target.kind == 3)
            out.clear();
        else if (target.result != TYPE_NONE)
            out.push_back(target.result);
    }

    void require_memory() const {
        if (module_.memories.empty())
            fail("memory instruction used without a memory");
    }

    void validate_instruction(const Instruction &ins, std::size_t pc) {
        const unsigned char op = ins.opcode;
        if (op != 0xfe && ins.extended_opcode != 0)
            fail(at("extended opcode on a non-extended instruction", pc));
        if (op == 0xfe) {
            atomic_instruction(ins, pc);
            return;
        }
        if (op == 0x00) {
            mark_unreachable();
            return;
        }
        if (op == 0x01)
            return;
        if (op >= 0x02 && op <= 0x04) {
            if (ins.block_type != TYPE_NONE && !valid_value_type(ins.block_type))
                fail(at("invalid block result type", pc));
            if (op == 0x04)
                pop(TYPE_I32);
            controls_.push_back(Control(op, stack_.size(), ins.block_type));
            return;
        }
        if (op == 0x05) {
            if (controls_.size() <= 1 || current().kind != 4 || current().saw_else)
                fail(at("else without matching if", pc));
            Control &c = current();
            finish_arm(c);
            stack_.resize(c.height);
            c.unreachable = false;
            c.saw_else = true;
            return;
        }
        if (op == 0x0b) {
            if (controls_.empty())
                fail(at("unexpected end", pc));
            Control c = current();
            if (c.kind == 4 && c.result != TYPE_NONE && !c.saw_else)
                fail(at("result-producing if has no else", pc));
            finish_arm(controls_.back());
            controls_.pop_back();
            stack_.resize(c.height);
            if (c.result != TYPE_NONE && c.kind != 0)
                push(c.result);
            return;
        }
        if (op == 0x0c || op == 0x0d) {
            if (op == 0x0d)
                pop(TYPE_I32);
            std::vector<ValueType> types;
            branch_types(ins.immediate, types);
            for (std::size_t i = types.size(); i > 0; --i)
                pop(types[i - 1]);
            if (op == 0x0d) {
                for (std::size_t i = 0; i < types.size(); ++i)
                    push(types[i]);
            } else
                mark_unreachable();
            return;
        }
        if (op == 0x0e) {
            pop(TYPE_I32);
            std::vector<ValueType> expected;
            if (ins.labels.empty())
                fail(at("br_table has no default target", pc));
            for (std::size_t i = 0; i < ins.labels.size(); ++i) {
                std::vector<ValueType> types;
                branch_types(ins.labels[i], types);
                if (i == 0)
                    expected = types;
                else if (types != expected)
                    fail(at("br_table target types differ", pc));
            }
            for (std::size_t i = expected.size(); i > 0; --i)
                pop(expected[i - 1]);
            mark_unreachable();
            return;
        }
        if (op == 0x0f) {
            std::vector<ValueType> types;
            if (!type_.results.empty())
                types.push_back(type_.results[0]);
            for (std::size_t i = types.size(); i > 0; --i)
                pop(types[i - 1]);
            mark_unreachable();
            return;
        }
        if (op == 0x10) {
            if (ins.immediate >= module_.functions.size())
                fail(at("call function index out of range", pc));
            const Function &callee = module_.functions[ins.immediate];
            if (callee.type_index >= module_.types.size())
                fail("callee has invalid type index");
            const FunctionType &sig = module_.types[callee.type_index];
            for (std::size_t i = sig.parameters.size(); i > 0; --i)
                pop(sig.parameters[i - 1]);
            if (!sig.results.empty())
                push(sig.results[0]);
            return;
        }
        if (op == 0x11) {
            if (ins.immediate >= module_.types.size())
                fail(at("call_indirect type index out of range", pc));
            if (ins.secondary != 0)
                fail(at("call_indirect table index is not zero", pc));
            if (module_.tables.size() != 1)
                fail("call_indirect requires exactly one table");
            pop(TYPE_I32);
            const FunctionType &sig = module_.types[ins.immediate];
            for (std::size_t i = sig.parameters.size(); i > 0; --i)
                pop(sig.parameters[i - 1]);
            if (!sig.results.empty())
                push(sig.results[0]);
            return;
        }
        if (op == 0x1a) {
            pop_any();
            return;
        }
        if (op == 0x1b) {
            pop(TYPE_I32);
            ValueType b = pop_any(), a = pop_any();
            if (a != TYPE_NONE && b != TYPE_NONE && a != b)
                fail("select operands have different types");
            push(a == TYPE_NONE ? b : a);
            return;
        }
        if (op >= 0x20 && op <= 0x22) {
            if (ins.immediate >= locals_.size())
                fail(at("local index out of range", pc));
            ValueType t = locals_[ins.immediate];
            if (op == 0x20)
                push(t);
            else {
                pop(t);
                if (op == 0x22)
                    push(t);
            }
            return;
        }
        if (op == 0x23 || op == 0x24) {
            if (ins.immediate >= module_.globals.size())
                fail(at("global index out of range", pc));
            const Global &g = module_.globals[ins.immediate];
            if (op == 0x23)
                push(g.type);
            else {
                if (!g.mutable_)
                    fail("global.set targets an immutable global");
                pop(g.type);
            }
            return;
        }
        if (op >= 0x28 && op <= 0x3e) {
            memory_instruction(ins, pc);
            return;
        }
        if (op == 0x3f || op == 0x40) {
            require_memory();
            if (ins.immediate != 0)
                fail(at("memory.size/grow reserved immediate is not zero", pc));
            if (op == 0x3f)
                push(TYPE_I32);
            else {
                pop(TYPE_I32);
                push(TYPE_I32);
            }
            return;
        }
        if (op == 0x41 || op == 0x42 || op == 0x43 || op == 0x44) {
            ValueType t = op == 0x41   ? TYPE_I32
                          : op == 0x42 ? TYPE_I64
                          : op == 0x43 ? TYPE_F32
                                       : TYPE_F64;
            std::size_t length = (op == 0x41 || op == 0x43) ? 4 : 8;
            if (ins.bytes.size() != length)
                fail(at("constant has invalid bit width", pc));
            push(t);
            return;
        }
        if (numeric_instruction(op))
            return;
        fail(at("unsupported or invalid opcode", pc));
    }

    bool numeric_instruction(unsigned char op) {
        ValueType a = TYPE_NONE, b = TYPE_NONE, result = TYPE_NONE;
        bool unary = false, binary = false;
        if (op == 0x45) {
            a = TYPE_I32;
            result = TYPE_I32;
            unary = true;
        } else if (op >= 0x46 && op <= 0x4f) {
            a = b = result = TYPE_I32;
            binary = true;
        } else if (op == 0x50) {
            a = TYPE_I64;
            result = TYPE_I32;
            unary = true;
        } else if (op >= 0x51 && op <= 0x5a) {
            a = b = TYPE_I64;
            result = TYPE_I32;
            binary = true;
        } else if (op >= 0x5b && op <= 0x60) {
            a = b = TYPE_F32;
            result = TYPE_I32;
            binary = true;
        } else if (op >= 0x61 && op <= 0x66) {
            a = b = TYPE_F64;
            result = TYPE_I32;
            binary = true;
        } else if (op >= 0x67 && op <= 0x69) {
            a = result = TYPE_I32;
            unary = true;
        } else if (op >= 0x6a && op <= 0x78) {
            a = b = result = TYPE_I32;
            binary = true;
        } else if (op >= 0x79 && op <= 0x7b) {
            a = result = TYPE_I64;
            unary = true;
        } else if (op >= 0x7c && op <= 0x8a) {
            a = b = result = TYPE_I64;
            binary = true;
        } else if (op >= 0x8b && op <= 0x91) {
            a = result = TYPE_F32;
            unary = true;
        } else if (op >= 0x92 && op <= 0x98) {
            a = b = result = TYPE_F32;
            binary = true;
        } else if (op >= 0x99 && op <= 0x9f) {
            a = result = TYPE_F64;
            unary = true;
        } else if (op >= 0xa0 && op <= 0xa6) {
            a = b = result = TYPE_F64;
            binary = true;
        } else if (op == 0xa7) {
            a = TYPE_I64;
            result = TYPE_I32;
            unary = true;
        } else if (op == 0xa8 || op == 0xa9) {
            a = TYPE_F32;
            result = TYPE_I32;
            unary = true;
        } else if (op == 0xaa || op == 0xab) {
            a = TYPE_F64;
            result = TYPE_I32;
            unary = true;
        } else if (op == 0xac || op == 0xad) {
            a = TYPE_I32;
            result = TYPE_I64;
            unary = true;
        } else if (op == 0xae || op == 0xaf) {
            a = TYPE_F32;
            result = TYPE_I64;
            unary = true;
        } else if (op == 0xb0 || op == 0xb1) {
            a = TYPE_F64;
            result = TYPE_I64;
            unary = true;
        } else if (op >= 0xb2 && op <= 0xb3) {
            a = TYPE_I32;
            result = TYPE_F32;
            unary = true;
        } else if (op >= 0xb4 && op <= 0xb5) {
            a = TYPE_I64;
            result = TYPE_F32;
            unary = true;
        } else if (op == 0xb6) {
            a = TYPE_F64;
            result = TYPE_F32;
            unary = true;
        } else if (op >= 0xb7 && op <= 0xb8) {
            a = TYPE_I32;
            result = TYPE_F64;
            unary = true;
        } else if (op >= 0xb9 && op <= 0xba) {
            a = TYPE_I64;
            result = TYPE_F64;
            unary = true;
        } else if (op == 0xbb) {
            a = TYPE_F32;
            result = TYPE_F64;
            unary = true;
        } else if (op == 0xbc) {
            a = TYPE_F32;
            result = TYPE_I32;
            unary = true;
        } else if (op == 0xbd) {
            a = TYPE_F64;
            result = TYPE_I64;
            unary = true;
        } else if (op == 0xbe) {
            a = TYPE_I32;
            result = TYPE_F32;
            unary = true;
        } else if (op == 0xbf) {
            a = TYPE_I64;
            result = TYPE_F64;
            unary = true;
        } else
            return false;
        if (binary) {
            pop(b);
            pop(a);
        } else if (unary)
            pop(a);
        push(result);
        return true;
    }

    void memory_instruction(const Instruction &ins, std::size_t pc) {
        require_memory();
        unsigned char op = ins.opcode;
        ValueType access;
        unsigned int natural = 0;
        bool load = op <= 0x35;
        if (op == 0x28 || op == 0x2c || op == 0x2d || op == 0x2e || op == 0x2f) {
            access = TYPE_I32;
            natural = op == 0x28 ? 2 : (op <= 0x2d ? 0 : 1);
        } else if (op == 0x29 || (op >= 0x30 && op <= 0x35)) {
            access = TYPE_I64;
            natural = op == 0x29 ? 3 : ((op <= 0x31) ? 0 : (op <= 0x33 ? 1 : 2));
        } else if (op == 0x2a) {
            access = TYPE_F32;
            natural = 2;
        } else if (op == 0x2b) {
            access = TYPE_F64;
            natural = 3;
        } else if (op == 0x36 || op == 0x3a || op == 0x3b) {
            access = TYPE_I32;
            natural = op == 0x36 ? 2 : op == 0x3a ? 0 : 1;
        } else if (op == 0x37 || (op >= 0x3c && op <= 0x3e)) {
            access = TYPE_I64;
            natural = op == 0x37 ? 3 : op == 0x3c ? 0 : op == 0x3d ? 1 : 2;
        } else if (op == 0x38) {
            access = TYPE_F32;
            natural = 2;
        } else if (op == 0x39) {
            access = TYPE_F64;
            natural = 3;
        } else
            fail(at("invalid memory opcode", pc));
        if (ins.immediate > natural)
            fail(at("memory alignment exceeds natural alignment", pc));
        if (load) {
            pop(TYPE_I32);
            push(access);
        } else {
            pop(access);
            pop(TYPE_I32);
        }
    }

    ValueType atomic_access_type(uint32_t subopcode) const {
        if (subopcode == 0x10 || subopcode == 0x12 || subopcode == 0x13 || subopcode == 0x17 ||
            subopcode == 0x19 || subopcode == 0x1a)
            return TYPE_I32;
        if (subopcode >= 0x11 && subopcode <= 0x16)
            return TYPE_I64;
        if (subopcode >= 0x18 && subopcode <= 0x1d)
            return TYPE_I64;
        const uint32_t lane = (subopcode - 0x1e) % 7;
        return lane == 0 || lane == 2 || lane == 3 ? TYPE_I32 : TYPE_I64;
    }

    void atomic_instruction(const Instruction &ins, std::size_t pc) {
        const uint32_t subopcode = ins.extended_opcode;
        const uint32_t natural = atomic_natural_alignment(subopcode);
        if (natural == 0xffffffffU)
            fail(at("invalid atomic instruction subopcode", pc));
        if (subopcode == 0x03) {
            if (ins.immediate != 0 || ins.secondary != 0 || !ins.bytes.empty())
                fail(at("atomic.fence has invalid immediates", pc));
            return;
        }
        require_memory();
        if (ins.immediate != natural)
            fail(at("atomic alignment does not equal natural alignment", pc));
        if (!ins.bytes.empty())
            fail(at("atomic instruction has unexpected immediate bytes", pc));
        if (subopcode == 0x00) {
            pop(TYPE_I32);
            pop(TYPE_I32);
            push(TYPE_I32);
            return;
        }
        if (subopcode == 0x01 || subopcode == 0x02) {
            pop(TYPE_I64);
            pop(subopcode == 0x01 ? TYPE_I32 : TYPE_I64);
            pop(TYPE_I32);
            push(TYPE_I32);
            return;
        }
        if (subopcode >= 0x10 && subopcode <= 0x16) {
            pop(TYPE_I32);
            push(atomic_access_type(subopcode));
            return;
        }
        if (subopcode >= 0x17 && subopcode <= 0x1d) {
            pop(atomic_access_type(subopcode));
            pop(TYPE_I32);
            return;
        }
        const ValueType type = atomic_access_type(subopcode);
        if (subopcode >= 0x48) {
            pop(type);
            pop(type);
        } else {
            pop(type);
        }
        pop(TYPE_I32);
        push(type);
    }
};

static bool const_expression(const std::vector<Instruction> &code, ValueType expected,
                             const Module &module, bool allow_global, std::size_t global_limit) {
    if (code.size() != 2 || code[1].opcode != 0x0b)
        return false;
    const Instruction &i = code[0];
    if (i.opcode == 0x41 || i.opcode == 0x42 || i.opcode == 0x43 || i.opcode == 0x44) {
        ValueType got = i.opcode == 0x41   ? TYPE_I32
                        : i.opcode == 0x42 ? TYPE_I64
                        : i.opcode == 0x43 ? TYPE_F32
                                           : TYPE_F64;
        std::size_t bytes = (got == TYPE_I32 || got == TYPE_F32) ? 4 : 8;
        return got == expected && i.bytes.size() == bytes;
    }
    if (allow_global && i.opcode == 0x23 && i.immediate < global_limit &&
        i.immediate < module.globals.size()) {
        const Global &g = module.globals[i.immediate];
        return g.imported && !g.mutable_ && g.type == expected;
    }
    return false;
}

static void check_limits(const Limits &l, uint32_t hard_max, const std::string &what) {
    if (l.has_maximum && l.maximum < l.minimum)
        fail(what + " maximum is below minimum");
    if (l.minimum > hard_max || (l.has_maximum && l.maximum > hard_max))
        fail(what + " limit exceeds MVP maximum");
}

static void check_import_order(const std::vector<Function> &v, const char *what) {
    bool defined = false;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (!v[i].imported)
            defined = true;
        else if (defined)
            fail(std::string("imported ") + what + " follows a definition");
    }
}
static void check_import_order(const std::vector<Table> &v, const char *what) {
    bool defined = false;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (!v[i].imported)
            defined = true;
        else if (defined)
            fail(std::string("imported ") + what + " follows a definition");
    }
}
static void check_import_order(const std::vector<Memory> &v, const char *what) {
    bool defined = false;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (!v[i].imported)
            defined = true;
        else if (defined)
            fail(std::string("imported ") + what + " follows a definition");
    }
}
static void check_import_order(const std::vector<Global> &v, const char *what) {
    bool defined = false;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (!v[i].imported)
            defined = true;
        else if (defined)
            fail(std::string("imported ") + what + " follows a definition");
    }
}

struct ValidationContext {
    const Module *module;
};

void validate_function_job(void *context, std::size_t index) {
    const ValidationContext &validation = *static_cast<ValidationContext *>(context);
    FunctionValidator(*validation.module, index).run();
}
}

std::size_t analyze_function_stack(const Module &module, std::size_t index) {
    if (index >= module.functions.size())
        fail("function index is out of range");
    FunctionValidator validator(module, index);
    validator.run();
    return validator.maximum_stack();
}

void validate_module(const Module &module, unsigned workers) {
    if (module.tables.size() > 1)
        fail("MVP module may define at most one table");
    if (module.memories.size() > 1)
        fail("MVP module may define at most one memory");
    check_import_order(module.functions, "function");
    check_import_order(module.tables, "table");
    check_import_order(module.memories, "memory");
    check_import_order(module.globals, "global");

    for (std::size_t i = 0; i < module.types.size(); ++i) {
        const FunctionType &t = module.types[i];
        check_name(t.name, at("type identifier", i));
        if (t.results.size() > 1)
            fail(at("MVP function type has more than one result", i));
        for (std::size_t j = 0; j < t.parameters.size(); ++j)
            if (!valid_value_type(t.parameters[j]))
                fail(at("invalid parameter type", j));
        for (std::size_t j = 0; j < t.results.size(); ++j)
            if (!valid_value_type(t.results[j]))
                fail(at("invalid result type", j));
    }
    for (std::size_t i = 0; i < module.types.size(); ++i)
        if (!module.types[i].name.empty())
            for (std::size_t j = 0; j < i; ++j)
                if (module.types[i].name == module.types[j].name)
                    fail("duplicate type identifier '" + module.types[i].name + "'");

    for (std::size_t i = 0; i < module.functions.size(); ++i) {
        const Function &f = module.functions[i];
        check_name(f.name, at("function identifier", i));
        if (f.type_index >= module.types.size())
            fail(at("function type index out of range", i));
        if (f.imported) {
            check_name(f.import_module, "function import module");
            check_name(f.import_name, "function import name");
        }
        for (std::size_t j = 0; j < f.parameter_names.size(); ++j) {
            check_name(f.parameter_names[j], at("parameter identifier", j));
            if (j >= module.types[f.type_index].parameters.size())
                fail(at("too many parameter names", i));
            if (!f.parameter_names[j].empty())
                for (std::size_t k = 0; k < j; ++k)
                    if (f.parameter_names[j] == f.parameter_names[k])
                        fail("duplicate parameter identifier '" + f.parameter_names[j] + "'");
        }
        for (std::size_t j = 0; j < f.locals.size(); ++j) {
            check_name(f.locals[j].name, at("local identifier", j));
            if (!f.locals[j].name.empty()) {
                for (std::size_t k = 0; k < f.parameter_names.size(); ++k)
                    if (f.locals[j].name == f.parameter_names[k])
                        fail("local identifier duplicates a parameter identifier");
                for (std::size_t k = 0; k < j; ++k)
                    if (f.locals[j].name == f.locals[k].name)
                        fail("duplicate local identifier '" + f.locals[j].name + "'");
            }
        }
    }
    for (std::size_t i = 0; i < module.functions.size(); ++i)
        if (!module.functions[i].name.empty())
            for (std::size_t j = 0; j < i; ++j)
                if (module.functions[i].name == module.functions[j].name)
                    fail("duplicate function identifier '" + module.functions[i].name + "'");
    for (std::size_t i = 0; i < module.tables.size(); ++i) {
        const Table &t = module.tables[i];
        check_name(t.name, at("table identifier", i));
        check_limits(t.limits, 0xffffffffU, "table");
        if (t.imported) {
            check_name(t.import_module, "table import module");
            check_name(t.import_name, "table import name");
        }
    }
    for (std::size_t i = 0; i < module.tables.size(); ++i)
        if (!module.tables[i].name.empty())
            for (std::size_t j = 0; j < i; ++j)
                if (module.tables[i].name == module.tables[j].name)
                    fail("duplicate table identifier '" + module.tables[i].name + "'");
    for (std::size_t i = 0; i < module.memories.size(); ++i) {
        const Memory &m = module.memories[i];
        check_name(m.name, at("memory identifier", i));
        check_limits(m.limits, 65536U, "memory");
        if (m.shared && !m.limits.has_maximum)
            fail(at("shared memory has no maximum", i));
        if (m.imported) {
            check_name(m.import_module, "memory import module");
            check_name(m.import_name, "memory import name");
        }
    }
    for (std::size_t i = 0; i < module.memories.size(); ++i)
        if (!module.memories[i].name.empty())
            for (std::size_t j = 0; j < i; ++j)
                if (module.memories[i].name == module.memories[j].name)
                    fail("duplicate memory identifier '" + module.memories[i].name + "'");
    for (std::size_t i = 0; i < module.globals.size(); ++i) {
        const Global &g = module.globals[i];
        check_name(g.name, at("global identifier", i));
        if (!valid_value_type(g.type))
            fail(at("invalid global type", i));
        if (g.imported) {
            check_name(g.import_module, "global import module");
            check_name(g.import_name, "global import name");
            if (!g.init.empty() || !g.raw_init.empty())
                fail(at("imported global has an initializer", i));
        } else if (!const_expression(g.init, g.type, module, true, i))
            fail(at("invalid global initializer", i));
    }
    for (std::size_t i = 0; i < module.globals.size(); ++i)
        if (!module.globals[i].name.empty())
            for (std::size_t j = 0; j < i; ++j)
                if (module.globals[i].name == module.globals[j].name)
                    fail("duplicate global identifier '" + module.globals[i].name + "'");
    ValidationContext context;
    context.module = &module;
    parallel_for(module.functions.size(), workers, validate_function_job, &context);

    std::vector<std::string> export_names;
    for (std::size_t i = 0; i < module.exports.size(); ++i) {
        const Export &e = module.exports[i];
        check_name(e.name, at("export name", i));
        for (std::size_t j = 0; j < i; ++j)
            if (module.exports[j].name == e.name)
                fail("duplicate export name '" + e.name + "'");
        std::size_t bound = e.kind == 0   ? module.functions.size()
                            : e.kind == 1 ? module.tables.size()
                            : e.kind == 2 ? module.memories.size()
                            : e.kind == 3 ? module.globals.size()
                                          : 0;
        if (!bound || e.index >= bound)
            fail(at("export index or kind is invalid", i));
    }
    if (module.has_start) {
        if (module.start >= module.functions.size())
            fail("start function index out of range");
        const FunctionType &t = module.types[module.functions[module.start].type_index];
        if (!t.parameters.empty() || !t.results.empty())
            fail("start function must have type [] -> []");
    }
    for (std::size_t i = 0; i < module.elements.size(); ++i) {
        const ElementSegment &e = module.elements[i];
        if (e.table_index >= module.tables.size())
            fail(at("element table index out of range", i));
        if (!const_expression(e.offset, TYPE_I32, module, true, module.globals.size()))
            fail(at("invalid element offset expression", i));
        for (std::size_t j = 0; j < e.functions.size(); ++j)
            if (e.functions[j] >= module.functions.size())
                fail(at("element function index out of range", j));
    }
    for (std::size_t i = 0; i < module.data.size(); ++i) {
        const DataSegment &d = module.data[i];
        if (d.memory_index >= module.memories.size())
            fail(at("data memory index out of range", i));
        if (!const_expression(d.offset, TYPE_I32, module, true, module.globals.size()))
            fail(at("invalid data offset expression", i));
    }
}

}
