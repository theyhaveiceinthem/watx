#include <watx/validator.h>

#include <iostream>
#include <stdexcept>
#include <string>

using namespace watx;

static Instruction op(unsigned char code) {
    return Instruction(code);
}
static Instruction constant(unsigned char code, std::size_t width) {
    Instruction i(code);
    i.bytes.resize(width, 0);
    return i;
}

static Module module_with_function(ValueType result, const std::vector<Instruction> &code) {
    Module m;
    FunctionType t;
    if (result != TYPE_NONE)
        t.results.push_back(result);
    m.types.push_back(t);
    Function f;
    f.type_index = 0;
    f.code = code;
    m.functions.push_back(f);
    return m;
}

static void good(const char *label, const Module &m) {
    try {
        validate_module(m);
    } catch (const std::exception &e) {
        std::cerr << "FAIL (expected valid): " << label << ": " << e.what() << "\n";
        throw;
    }
}

static void bad(const char *label, const Module &m) {
    try {
        validate_module(m);
        std::cerr << "FAIL (expected invalid): " << label << "\n";
        throw std::runtime_error("validator accepted invalid smoke case");
    } catch (const std::runtime_error &e) {
        if (std::string(e.what()) == "validator accepted invalid smoke case")
            throw;
    }
}

int main() {
    try {

        std::vector<Instruction> code;
        code.push_back(constant(0x41, 4));
        Instruction if_i32 = op(0x04);
        if_i32.block_type = TYPE_I32;
        code.push_back(if_i32);
        code.push_back(constant(0x41, 4));
        code.push_back(op(0x05));
        code.push_back(constant(0x41, 4));
        code.push_back(op(0x0b));
        code.push_back(op(0x0b));
        good("if/else result control frames", module_with_function(TYPE_I32, code));

        code.clear();
        code.push_back(op(0x00));
        code.push_back(op(0x6a));
        code.push_back(op(0x0b));
        good("unreachable polymorphism", module_with_function(TYPE_I32, code));

        code.clear();
        code.push_back(constant(0x41, 4));
        code.push_back(constant(0x41, 4));
        code.push_back(constant(0x41, 4));
        code.push_back(op(0x1b));
        code.push_back(op(0x0b));
        good("select consumes two like-typed values and an i32 condition",
             module_with_function(TYPE_I32, code));

        code.clear();
        Instruction block_i32 = op(0x02);
        block_i32.block_type = TYPE_I32;
        Instruction br_if = op(0x0d);
        br_if.immediate = 0;
        code.push_back(block_i32);
        code.push_back(constant(0x41, 4));
        code.push_back(constant(0x41, 4));
        code.push_back(br_if);
        code.push_back(op(0x0b));
        code.push_back(op(0x0b));
        good("br_if preserves target values on fallthrough", module_with_function(TYPE_I32, code));

        code.clear();
        Instruction result_if = op(0x04);
        result_if.block_type = TYPE_I32;
        code.push_back(constant(0x41, 4));
        code.push_back(result_if);
        code.push_back(constant(0x41, 4));
        code.push_back(op(0x0b));
        bad("result-producing if without else", module_with_function(TYPE_I32, code));

        code.clear();
        code.push_back(op(0x01));
        bad("missing final function end", module_with_function(TYPE_NONE, code));

        code.clear();
        code.push_back(op(0x04));
        code.push_back(op(0x05));
        code.push_back(op(0x05));
        code.push_back(op(0x0b));
        code.push_back(op(0x0b));
        bad("duplicate else", module_with_function(TYPE_NONE, code));

        Module m = module_with_function(TYPE_NONE, std::vector<Instruction>(1, op(0x0b)));
        Global g;
        g.type = TYPE_I32;
        m.globals.push_back(g);
        bad("empty global initializer", m);

        m = module_with_function(TYPE_NONE, std::vector<Instruction>(1, op(0x0b)));
        g = Global();
        g.type = TYPE_I32;
        g.init.push_back(constant(0x41, 4));
        g.init.push_back(op(0x0b));
        m.globals.push_back(g);
        good("i32 constant global initializer", m);

        m = module_with_function(TYPE_NONE, std::vector<Instruction>(1, op(0x0b)));
        g = Global();
        g.type = TYPE_I32;
        g.init.push_back(constant(0x41, 4));
        m.globals.push_back(g);
        bad("constant initializer missing final end", m);

        m = module_with_function(TYPE_NONE, std::vector<Instruction>(1, op(0x0b)));
        g = Global();
        g.type = TYPE_I32;
        g.mutable_ = true;
        g.imported = true;
        g.import_module = "env";
        g.import_name = "mutable";
        m.globals.push_back(g);
        Export ex;
        ex.name = "mutable";
        ex.kind = 3;
        ex.index = 0;
        m.exports.push_back(ex);
        good("mutable global import and export in wg-1.0", m);

        m = module_with_function(TYPE_NONE, std::vector<Instruction>(1, op(0x0b)));
        m.tables.push_back(Table());
        m.tables.push_back(Table());
        bad("multiple MVP tables", m);
        m = module_with_function(TYPE_NONE, std::vector<Instruction>(1, op(0x0b)));
        m.memories.push_back(Memory());
        m.memories[0].limits.minimum = 65537;
        bad("memory limit above 65536 pages", m);

        code.clear();
        Instruction call = op(0x10);
        call.immediate = 1;
        code.push_back(call);
        code.push_back(op(0x0b));
        bad("call function index out of range", module_with_function(TYPE_NONE, code));

        code.clear();
        Instruction indirect = op(0x11);
        indirect.immediate = 0;
        indirect.secondary = 1;
        code.push_back(indirect);
        code.push_back(op(0x0b));
        m = module_with_function(TYPE_NONE, code);
        m.tables.push_back(Table());
        bad("call_indirect reserved table index", m);

        m = module_with_function(TYPE_NONE, std::vector<Instruction>(1, op(0x0b)));
        ElementSegment elem;
        elem.table_index = 0;
        m.elements.push_back(elem);
        bad("element table index out of range", m);

        std::cout << "validator smoke checks passed\n";
        return 0;
    } catch (const std::exception &) {
        return 1;
    }
}
