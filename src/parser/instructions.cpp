#include <watx/instructions.h>
#include <watx/platform/threads.h>
#include <watx/text/literals.h>
#include <watx/wasm/opcodes.h>

#include <stdexcept>

namespace watx {
namespace {

static std::string err_name(const std::string &s) {
    return "instruction '" + s + "'";
}

static uint32_t number(const std::string &s, const std::string &what) {
    if (s.empty() || s[0] == '-')
        throw std::runtime_error("invalid " + what + " '" + s + "'");
    return parse_natural(s);
}
static bool is_name(const std::string &s) {
    return s.size() > 1 && s[0] == '$';
}
static bool is_numeric_token(const std::string &s) {
    return !s.empty() && ((s[0] >= '0' && s[0] <= '9') || s[0] == '+');
}

class Compiler {
  public:
    Compiler(const Module &m, const Function *f, std::vector<Instruction> &out)
        : module(m), function(f), code(out) {}
    void body(const std::vector<Node> &nodes) {
        labels.push_back(std::string());
        std::size_t p = 0;
        sequence(nodes, p, false);
        code.push_back(Instruction(0x0b));
    }
    void expression(const std::vector<Node> &nodes) {
        std::size_t p = 0;
        sequence(nodes, p, false);
        code.push_back(Instruction(0x0b));
    }

  private:
    const Module &module;
    const Function *function;
    std::vector<Instruction> &code;
    std::vector<std::string> labels;

    uint32_t local_ref(const std::string &s) {
        if (!function)
            throw std::runtime_error("local instruction outside a function");
        const FunctionType &ft = type_for(*function);
        if (is_name(s)) {
            for (std::size_t i = 0;
                 i < function->parameter_names.size() && i < ft.parameters.size(); ++i)
                if (function->parameter_names[i] == s)
                    return static_cast<uint32_t>(i);
            for (std::size_t i = 0; i < function->locals.size(); ++i)
                if (function->locals[i].name == s)
                    return static_cast<uint32_t>(ft.parameters.size() + i);
            throw std::runtime_error("unknown local '" + s + "'");
        }
        uint32_t n = number(s, "local index");
        if (n >= ft.parameters.size() + function->locals.size())
            throw std::runtime_error("invalid local index '" + s + "'");
        return n;
    }
    const FunctionType &type_for(const Function &f) const {
        if (f.type_index >= module.types.size())
            throw std::runtime_error("function has invalid type index");
        return module.types[f.type_index];
    }
    uint32_t resolve_func(const std::string &s) const {
        uint32_t n;
        if (!s.empty() && s[0] == '$')
            n = function_index(module, s);
        else
            n = number(s, "function index");
        if (n >= module.functions.size())
            throw std::runtime_error("invalid function index '" + s + "'");
        return n;
    }
    uint32_t resolve_global(const std::string &s) const {
        uint32_t n;
        if (!s.empty() && s[0] == '$')
            n = global_index(module, s);
        else
            n = number(s, "global index");
        if (n >= module.globals.size())
            throw std::runtime_error("invalid global index '" + s + "'");
        return n;
    }
    uint32_t resolve_type(const std::string &s) const {
        uint32_t n;
        if (!s.empty() && s[0] == '$')
            n = type_index(module, s);
        else
            n = number(s, "type index");
        if (n >= module.types.size())
            throw std::runtime_error("invalid type index '" + s + "'");
        return n;
    }
    uint32_t label_depth(const std::string &s) const {
        if (is_name(s)) {
            for (std::size_t i = labels.size(); i > 0; --i)
                if (labels[i - 1] == s)
                    return static_cast<uint32_t>(labels.size() - i);
            throw std::runtime_error("unknown label '" + s + "'");
        }
        uint32_t n = number(s, "label depth");
        if (n >= labels.size())
            throw std::runtime_error("invalid label depth '" + s + "'");
        return n;
    }
    void push_label(const std::string &label) {

        labels.push_back(label);
    }
    bool take_header(const std::vector<Node> &nodes, std::size_t &p, std::string &label,
                     ValueType &bt) {
        bool has_result = false;
        if (p < nodes.size() && !nodes[p].list && is_name(nodes[p].text))
            label = nodes[p++].text;
        while (p < nodes.size() && nodes[p].list) {
            const Node &h = nodes[p];
            if (is_form(h, "result")) {
                if (h.children.size() > 2)
                    throw std::runtime_error("MVP block may have at most one result");
                if (h.children.size() == 2) {
                    if (has_result)
                        throw std::runtime_error("MVP block may have at most one result");
                    if (h.children[1].list)
                        throw std::runtime_error("block result requires a value type");
                    bt = value_type(h.children[1].text);
                    has_result = true;
                }
                ++p;
            } else if (is_form(h, "type")) {
                throw std::runtime_error("MVP block types cannot use type indices");
            } else
                break;
        }
        return has_result;
    }
    void sequence(const std::vector<Node> &nodes, std::size_t &p, bool stop_control) {
        while (p < nodes.size()) {
            const Node &n = nodes[p];
            if (!n.list && (n.text == "else" || n.text == "end")) {
                if (stop_control)
                    return;
                throw std::runtime_error("unexpected '" + n.text + "'");
            }
            if (n.list) {
                folded(n);
                ++p;
            } else {
                std::string op = n.text;
                ++p;
                flat(op, nodes, p);
            }
        }
        if (stop_control)
            throw std::runtime_error("unterminated structured instruction");
    }
    void folded(const Node &n) {
        if (n.children.empty() || n.children[0].list)
            throw std::runtime_error("invalid folded instruction");
        std::string op = n.children[0].text;
        if (op == "atomic.fence" && n.children.size() != 1)
            throw std::runtime_error("atomic.fence takes no operands");
        std::size_t p = 1;

        if (op == "block" || op == "loop" || op == "if") {
            folded_control(op, n.children, p);
            return;
        }

        std::vector<Node> imm;
        while (p < n.children.size() && !n.children[p].list)
            imm.push_back(n.children[p++]);
        while (p < n.children.size() && n.children[p].list &&
               (is_form(n.children[p], "type") || is_form(n.children[p], "table") ||
                is_form(n.children[p], "param") || is_form(n.children[p], "result")))
            imm.push_back(n.children[p++]);
        for (; p < n.children.size(); ++p) {
            if (!n.children[p].list)
                throw std::runtime_error("unexpected atom in folded instruction '" + op + "'");
            folded(n.children[p]);
        }
        std::size_t ip = 0;
        flat(op, imm, ip);
        if (ip != imm.size())
            throw std::runtime_error("unexpected immediate in folded instruction '" + op + "'");
    }
    void folded_control(const std::string &op, const std::vector<Node> &c, std::size_t p) {
        std::string label;
        ValueType bt = TYPE_NONE;

        std::vector<Node> hdr;
        while (p < c.size() && !c[p].list && is_name(c[p].text))
            hdr.push_back(c[p++]);
        while (p < c.size() && c[p].list && (is_form(c[p], "result") || is_form(c[p], "type")))
            hdr.push_back(c[p++]);
        std::size_t hp = 0;
        take_header(hdr, hp, label, bt);
        if (hp != hdr.size())
            throw std::runtime_error("invalid folded block header");
        if (op == "if") {
            std::vector<Node> condition;
            while (p < c.size() && !is_form(c[p], "then") && !is_form(c[p], "else")) {
                if (!c[p].list)
                    throw std::runtime_error("folded if conditions must be folded instructions");
                condition.push_back(c[p++]);
            }
            std::size_t condp = 0;
            sequence(condition, condp, false);
            Instruction ins(0x04);
            ins.block_type = bt;
            code.push_back(ins);
            push_label(label);
            if (p >= c.size() || !is_form(c[p], "then"))
                throw std::runtime_error("folded if requires a then branch");
            {
                std::size_t q = 1;
                sequence(c[p].children, q, false);
                ++p;
            }
            if (p < c.size() && is_form(c[p], "else")) {
                code.push_back(Instruction(0x05));
                std::size_t q = 1;
                sequence(c[p].children, q, false);
                ++p;
            }
            labels.pop_back();
            code.push_back(Instruction(0x0b));
            if (p != c.size())
                throw std::runtime_error("invalid folded if");
        } else {
            Instruction ins(op == "block" ? 0x02 : 0x03);
            ins.block_type = bt;
            code.push_back(ins);
            push_label(label);
            std::vector<Node> body;
            for (; p < c.size(); ++p)
                body.push_back(c[p]);
            std::size_t bp = 0;
            sequence(body, bp, false);
            labels.pop_back();
            code.push_back(Instruction(0x0b));
        }
    }
    void flat(const std::string &op, const std::vector<Node> &n, std::size_t &p) {
        if (op == "unreachable") {
            code.push_back(Instruction(0x00));
            return;
        }
        if (op == "nop") {
            code.push_back(Instruction(0x01));
            return;
        }
        if (op == "return") {
            code.push_back(Instruction(0x0f));
            return;
        }
        if (op == "drop") {
            code.push_back(Instruction(0x1a));
            return;
        }
        if (op == "select") {
            code.push_back(Instruction(0x1b));
            return;
        }
        if (op == "block" || op == "loop" || op == "if") {
            control(op, n, p);
            return;
        }
        if (op == "else" || op == "end")
            throw std::runtime_error("unexpected '" + op + "'");
        if (op == "br" || op == "br_if") {
            Instruction i(op == "br" ? 0x0c : 0x0d);
            i.immediate = label_depth(take_atom(n, p, op));
            code.push_back(i);
            return;
        }
        if (op == "br_table") {
            Instruction i(0x0e);
            while (p < n.size() && !n[p].list &&
                   (is_name(n[p].text) || is_numeric_token(n[p].text)))
                i.labels.push_back(label_depth(take_atom(n, p, op)));
            if (i.labels.empty())
                throw std::runtime_error("br_table requires a default label");
            code.push_back(i);
            return;
        }
        if (op == "call") {
            Instruction i(0x10);
            i.immediate = resolve_func(take_atom(n, p, op));
            code.push_back(i);
            return;
        }
        if (op == "call_indirect") {
            call_indirect(n, p);
            return;
        }
        if (op == "local.get" || op == "local.set" || op == "local.tee") {
            Instruction i(op == "local.get" ? 0x20 : op == "local.set" ? 0x21 : 0x22);
            i.immediate = local_ref(take_atom(n, p, op));
            code.push_back(i);
            return;
        }
        if (op == "global.get" || op == "global.set") {
            Instruction i(op == "global.get" ? 0x23 : 0x24);
            i.immediate = resolve_global(take_atom(n, p, op));
            code.push_back(i);
            return;
        }
        if (op == "memory.size" || op == "memory.grow") {
            uint32_t m = 0;
            if (p < n.size() && !n[p].list && n[p].text == "0") {
                ++p;
            }
            if (module.memories.empty())
                throw std::runtime_error("invalid memory index");
            Instruction i(op == "memory.size" ? 0x3f : 0x40);
            i.immediate = m;
            code.push_back(i);
            return;
        }
        if (op == "i32.const" || op == "i64.const" || op == "f32.const" || op == "f64.const") {
            constant(op, take_atom(n, p, op));
            return;
        }
        uint32_t subopcode = 0;
        if (atomic_opcode(op, subopcode)) {
            atomic_instruction(op, subopcode, n, p);
            return;
        }
        unsigned char opcode = 0;
        if (memory_opcode(op, opcode)) {
            memarg(op, opcode, n, p);
            return;
        }
        if (numeric_opcode(op, opcode)) {
            code.push_back(Instruction(opcode));
            return;
        }
        throw std::runtime_error("unsupported MVP instruction '" + op + "'");
    }
    std::string take_atom(const std::vector<Node> &n, std::size_t &p, const std::string &op) {
        if (p >= n.size() || n[p].list)
            throw std::runtime_error("missing immediate for " + err_name(op));
        return n[p++].text;
    }
    void constant(const std::string &op, const std::string &value) {
        unsigned int width = op == "i32.const" || op == "f32.const" ? 32 : 64;
        Instruction i(op == "i32.const"   ? 0x41
                      : op == "i64.const" ? 0x42
                      : op == "f32.const" ? 0x43
                                          : 0x44);
        if (op == "i32.const" || op == "i64.const") {
            uint64_t bits = parse_integer_bits(value, width);
            for (unsigned int b = 0; b < width / 8; ++b)
                i.bytes.push_back(static_cast<unsigned char>((bits >> (b * 8)) & 255));
        } else
            i.bytes = parse_float_bits(value, width);
        code.push_back(i);
    }
    void memarg(const std::string &op, unsigned char opcode, const std::vector<Node> &n,
                std::size_t &p) {
        if (module.memories.empty())
            throw std::runtime_error("memory instruction requires a memory");
        Instruction i(opcode);
        i.immediate = natural_alignment(opcode);
        i.secondary = 0;
        bool saw_offset = false, saw_alignment = false;
        while (p < n.size() && !n[p].list) {
            std::string x = n[p].text;
            if (x.compare(0, 7, "offset=") == 0) {
                if (saw_offset || saw_alignment)
                    throw std::runtime_error("memory offset must appear once before alignment");
                i.secondary = number(x.substr(7), "memory offset");
                saw_offset = true;
                ++p;
            } else if (x.compare(0, 6, "align=") == 0) {
                if (saw_alignment)
                    throw std::runtime_error("memory alignment may appear only once");
                uint32_t a = number(x.substr(6), "memory alignment");
                if (a == 0 || (a & (a - 1)) != 0)
                    throw std::runtime_error("memory alignment must be a power of two");
                uint32_t exp = 0;
                while ((1u << exp) < a && exp < 31)
                    ++exp;
                i.immediate = exp;
                saw_alignment = true;
                ++p;
            } else
                break;
        }
        if (i.immediate > natural_alignment(opcode))
            throw std::runtime_error("memory alignment exceeds natural alignment for '" + op + "'");
        code.push_back(i);
    }
    void atomic_instruction(const std::string &op, uint32_t subopcode, const std::vector<Node> &n,
                            std::size_t &p) {
        Instruction i(0xfe);
        i.extended_opcode = subopcode;
        if (subopcode == 0x03) {
            code.push_back(i);
            return;
        }
        if (module.memories.empty())
            throw std::runtime_error("atomic instruction requires a memory");
        const uint32_t natural = atomic_natural_alignment(subopcode);
        i.immediate = natural;
        i.secondary = 0;
        bool saw_offset = false, saw_alignment = false;
        while (p < n.size() && !n[p].list) {
            std::string x = n[p].text;
            if (x.compare(0, 7, "offset=") == 0) {
                if (saw_offset || saw_alignment)
                    throw std::runtime_error("atomic offset must appear once before alignment");
                i.secondary = number(x.substr(7), "atomic memory offset");
                saw_offset = true;
                ++p;
            } else if (x.compare(0, 6, "align=") == 0) {
                if (saw_alignment)
                    throw std::runtime_error("atomic alignment may appear only once");
                uint32_t bytes = number(x.substr(6), "atomic memory alignment");
                if (bytes == 0 || (bytes & (bytes - 1)) != 0)
                    throw std::runtime_error("atomic alignment must be a power of two");
                uint32_t exponent = 0;
                while ((1u << exponent) < bytes && exponent < 31)
                    ++exponent;
                i.immediate = exponent;
                saw_alignment = true;
                ++p;
            } else {
                break;
            }
        }
        if (i.immediate != natural)
            throw std::runtime_error("atomic alignment must equal the natural alignment for '" +
                                     op + "'");
        code.push_back(i);
    }
    void call_indirect(const std::vector<Node> &n, std::size_t &p) {
        uint32_t type = 0;
        bool has_type = false;
        bool has_inline_signature = false;
        bool saw_param = false;
        bool saw_result = false;
        FunctionType inline_type;

        while (p < n.size() && n[p].list) {
            const Node &x = n[p];
            if (is_form(x, "type")) {
                if (has_type || saw_param || saw_result)
                    throw std::runtime_error("type clause must precede inline type-use clauses");
                if (x.children.size() != 2)
                    throw std::runtime_error("type use requires one type index");
                type = resolve_type(x.children[1].text);
                has_type = true;
                ++p;
            } else if (is_form(x, "param") || is_form(x, "result")) {
                bool params = is_form(x, "param");
                if (params && saw_result)
                    throw std::runtime_error("parameter clause cannot follow a result clause");
                if (params) {
                    saw_param = true;
                    for (std::size_t k = 1; k < x.children.size();) {
                        if (x.children[k].list)
                            throw std::runtime_error("invalid indirect call parameter type");
                        if (is_name(x.children[k].text))
                            throw std::runtime_error(
                                "call_indirect parameter identifiers are not allowed");
                        inline_type.parameters.push_back(value_type(x.children[k++].text));
                        has_inline_signature = true;
                    }
                } else {
                    saw_result = true;
                    if (x.children.size() > 2)
                        throw std::runtime_error("MVP indirect call type has at most one result");
                    if (x.children.size() == 2) {
                        if (!inline_type.results.empty())
                            throw std::runtime_error(
                                "MVP indirect call type has at most one result");
                        inline_type.results.push_back(value_type(x.children[1].text));
                        has_inline_signature = true;
                    }
                }
                ++p;
            } else if (is_form(x, "table")) {
                throw std::runtime_error("MVP call_indirect uses the default table");
            } else {
                break;
            }
        }

        if (!has_type && has_inline_signature) {
            type = implicit_type(inline_type);
            has_type = true;
        }

        if (!has_type) {
            FunctionType empty_type;
            type = implicit_type(empty_type);
            has_type = true;
        }
        if (has_inline_signature && (module.types[type].parameters != inline_type.parameters ||
                                     module.types[type].results != inline_type.results))
            throw std::runtime_error("call_indirect inline type does not match its declared type");
        if (module.tables.empty())
            throw std::runtime_error("call_indirect requires a table");

        Instruction instruction(0x11);
        instruction.immediate = type;
        instruction.secondary = 0;
        code.push_back(instruction);
    }
    uint32_t implicit_type(const FunctionType &type) const {
        for (std::size_t i = 0; i < module.types.size(); ++i)
            if (module.types[i].parameters == type.parameters &&
                module.types[i].results == type.results)
                return static_cast<uint32_t>(i);
        throw std::runtime_error("unresolved implicit function type");
    }
    void control(const std::string &op, const std::vector<Node> &n, std::size_t &p) {
        std::string label;
        ValueType bt = TYPE_NONE;
        take_header(n, p, label, bt);
        const unsigned char opcode = op == "block" ? 0x02 : op == "loop" ? 0x03 : 0x04;
        Instruction start(opcode);
        start.block_type = bt;
        code.push_back(start);
        push_label(label);
        bool has_else = false;
        while (p < n.size()) {
            if (!n[p].list && n[p].text == "else") {
                if (op != "if" || has_else)
                    throw std::runtime_error("unexpected or duplicate else");
                code.push_back(Instruction(0x05));
                has_else = true;
                ++p;
                if (p < n.size() && !n[p].list && is_name(n[p].text)) {
                    if (n[p].text != label)
                        throw std::runtime_error("else label does not match block label");
                    ++p;
                }
                continue;
            }
            if (!n[p].list && n[p].text == "end") {
                ++p;
                if (p < n.size() && !n[p].list && is_name(n[p].text)) {
                    if (n[p].text != label)
                        throw std::runtime_error("end label does not match block label");
                    ++p;
                }
                labels.pop_back();
                code.push_back(Instruction(0x0b));
                return;
            }
            if (!n[p].list && (n[p].text == "block" || n[p].text == "loop" || n[p].text == "if")) {
                std::string nested = n[p++].text;
                control(nested, n, p);
                continue;
            }
            if (n[p].list) {
                folded(n[p++]);
                continue;
            }
            std::string inner = n[p++].text;
            flat(inner, n, p);
        }
        throw std::runtime_error("unterminated '" + op + "'");
    }
};
void compile_function_job(void *context, std::size_t index) {
    Module &module = *static_cast<Module *>(context);
    Function &function = module.functions[index];
    if (function.imported)
        return;
    function.code.clear();
    Compiler compiler(module, &function, function.code);
    compiler.body(function.raw_body);
}
}

void compile_module_instructions(Module &module, unsigned workers) {
    parallel_for(module.functions.size(), workers, compile_function_job, &module);
    for (std::size_t i = 0; i < module.globals.size(); ++i)
        if (!module.globals[i].imported) {
            module.globals[i].init.clear();
            Compiler c(module, 0, module.globals[i].init);
            c.expression(module.globals[i].raw_init);
        }
    for (std::size_t i = 0; i < module.data.size(); ++i) {
        module.data[i].offset.clear();
        Compiler c(module, 0, module.data[i].offset);
        c.expression(module.data[i].raw_offset);
        if (module.data[i].memory_index >= module.memories.size())
            throw std::runtime_error("invalid data memory index");
    }
    for (std::size_t i = 0; i < module.elements.size(); ++i) {
        module.elements[i].offset.clear();
        Compiler c(module, 0, module.elements[i].offset);
        c.expression(module.elements[i].raw_offset);
        if (module.elements[i].table_index >= module.tables.size())
            throw std::runtime_error("invalid element table index");
        for (std::size_t j = 0; j < module.elements[i].functions.size(); ++j)
            if (module.elements[i].functions[j] >= module.functions.size())
                throw std::runtime_error("invalid element function index");
    }
}
}
