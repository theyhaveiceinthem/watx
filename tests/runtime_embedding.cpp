#include <iostream>
#include <memory>
#include <stdexcept>
#include <watx/host.h>
#include <watx/instructions.h>
#include <watx/jit/jit.h>
#include <watx/parser.h>
#include <watx/runtime/executor.h>
#include <watx/source.h>
#include <watx/validator.h>

struct Context {
    unsigned calls, ticks;
    Context() : calls(0), ticks(0) {}
};
static std::vector<watx::RuntimeValue> add(void *context,
                                           const std::vector<watx::RuntimeValue> &args) {
    if (args.size() != 2 || args[0].as_u32() != 10 || args[1].as_u32() != 5)
        throw std::runtime_error("host argument order changed");
    ++static_cast<Context *>(context)->calls;
    return std::vector<watx::RuntimeValue>(1, watx::RuntimeValue::i32(15));
}
static std::vector<watx::RuntimeValue> tick(void *context,
                                            const std::vector<watx::RuntimeValue> &args) {
    if (!args.empty())
        throw std::runtime_error("void import received arguments");
    ++static_cast<Context *>(context)->ticks;
    return std::vector<watx::RuntimeValue>();
}
static std::vector<watx::RuntimeValue> bad(void *, const std::vector<watx::RuntimeValue> &) {
    return std::vector<watx::RuntimeValue>(1, watx::RuntimeValue::f64(1));
}
static watx::Module parse(const std::string &source) {
    std::vector<watx::Token> tokens;
    watx::append_expanded_source(source, ".", tokens);
    watx::Module module = watx::parse_module(tokens);
    watx::compile_module_instructions(module);
    watx::validate_module(module);
    return module;
}
static void expect(watx::RuntimeInstance &instance, const char *name, uint32_t expected) {
    std::vector<watx::RuntimeValue> args;
    args.push_back(watx::RuntimeValue::i32(10));
    args.push_back(watx::RuntimeValue::i32(5));
    const std::vector<watx::RuntimeValue> values = instance.invoke_export(name, args);
    if (values.size() != 1 || values[0].type != watx::TYPE_I32 || values[0].as_u32() != expected)
        throw std::runtime_error("embedding result mismatch");
}
int main() {
    try {
        const watx::Module module =
            parse("(module (type $binary (func (param i32 i32) (result i32)))"
                  "(import \"math\" \"add\" (func $add (type $binary)))"
                  "(import \"math\" \"tick\" (func $tick))"
                  "(import \"math\" \"bad\" (func $bad (result i32)))"
                  "(table 1 funcref) (elem (i32.const 0) $add)"
                  "(func $start call $tick) (start $start)"
                  "(func (export \"direct\") (param i32 i32) (result i32)"
                  " i32.const 100 local.get 0 local.get 1 call $add i32.add)"
                  "(func (export \"indirect\") (param i32 i32) (result i32)"
                  " i32.const 200 local.get 0 local.get 1 i32.const 0 call_indirect (type $binary) "
                  "i32.add)"
                  "(func (export \"bad\") (result i32) call $bad)"
                  "(func (export \"void\") call $tick) (export \"raw_host\" (func $add)))");
        for (unsigned variant = 0; variant < 6; ++variant) {
            Context context;
            watx::FunctionType binary, nothing, result;
            binary.parameters.assign(2, watx::TYPE_I32);
            binary.results.push_back(watx::TYPE_I32);
            result.results.push_back(watx::TYPE_I32);
            watx::HostModule math("math");
            math.add_function("add", binary, &add, &context);
            math.add_function("tick", nothing, &tick, &context);
            math.add_function("bad", result, &bad);
            watx::Host host;
            host.add_module(math);
            std::auto_ptr<watx::JitEngine> jit;
            if (variant & 1) {
                jit.reset(new watx::JitEngine(module));
                jit->prepare(1);
            }
            watx::ThreadedExecutor executor(variant >= 2 && variant < 4 ? 2 : 1, jit.get());
            watx::RuntimeOptions options;
            if (variant >= 4)
                options.fuel = 10000;
            watx::RuntimeInstance instance(module, options, host.bind(module));
            instance.set_executor(&executor);
            instance.initialize();
            if (variant == 1) {
                const watx::Module other = module;
                bool rejected = false;
                try {
                    watx::RuntimeInstance mismatch(other, options, host.bind(other));
                    mismatch.set_executor(&executor);
                    mismatch.initialize();
                } catch (const std::runtime_error &e) {
                    rejected = std::string(e.what()).find("different modules") != std::string::npos;
                }
                if (!rejected)
                    throw std::runtime_error("JIT accepted a different module instance");
            }
            if (context.ticks != 1)
                throw std::runtime_error("start import did not execute once");
            expect(instance, "direct", 115);
            expect(instance, "indirect", 215);
            expect(instance, "raw_host", 15);
            bool trapped = false;
            try {
                instance.invoke_export("bad", std::vector<watx::RuntimeValue>());
            } catch (const std::runtime_error &e) {
                trapped = std::string(e.what()).find("type mismatch") != std::string::npos;
            }
            if (!trapped)
                throw std::runtime_error("host result type mismatch did not trap");
            expect(instance, "direct", 115);
            if (!instance.invoke_export("void", std::vector<watx::RuntimeValue>()).empty() ||
                context.ticks != 2 || context.calls != 4)
                throw std::runtime_error("host callback state mismatch");
        }
        watx::Module invalid = module;
        invalid.functions.back().code[0] = watx::Instruction(0x1a);
        bool rejected = false;
        try {
            watx::JitEngine engine(invalid);
            engine.prepare(1);
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("JIT accepted an invalid operand stack");
        std::cout
            << "Embedding imports, argument order, traps, fuel, and execution workers passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
