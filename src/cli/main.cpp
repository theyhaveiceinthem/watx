#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <watx/binary_reader.h>
#include <watx/binary_writer.h>
#include <watx/cli/options.h>
#include <watx/instructions.h>
#include <watx/jit/jit.h>
#include <watx/parser.h>
#include <watx/platform/threads.h>
#include <watx/runtime/executor.h>
#include <watx/runtime/runtime.h>
#include <watx/source.h>
#include <watx/text/literals.h>
#include <watx/validator.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#ifndef WATX_VERSION
#define WATX_VERSION "0.2.0"
#endif
namespace {
std::vector<unsigned char> read_bytes(const std::string &path) {
    std::ostringstream content;
    if (path == "-") {
#ifdef _WIN32
        if (_setmode(_fileno(stdin), _O_BINARY) == -1)
            throw std::runtime_error("could not put standard input into binary mode");
#endif
        content << std::cin.rdbuf();
        if (std::cin.bad())
            throw std::runtime_error("failed reading standard input");
    } else {
        std::ifstream file(path.c_str(), std::ios::binary);
        if (!file)
            throw std::runtime_error("could not open input file '" + path + "'");
        content << file.rdbuf();
        if (file.bad())
            throw std::runtime_error("failed reading input file '" + path + "'");
    }
    std::string bytes = content.str();
    return std::vector<unsigned char>(bytes.begin(), bytes.end());
}
bool binary_input(const watx::CliOptions &options, const std::vector<unsigned char> &bytes) {
    if (options.input_kind != watx::CliOptions::INPUT_AUTO)
        return options.input_kind == watx::CliOptions::INPUT_WASM;
    const std::string &path = options.inputs[0];
    return (bytes.size() >= 4 && bytes[0] == 0 && bytes[1] == 0x61 && bytes[2] == 0x73 &&
            bytes[3] == 0x6d) ||
           (path.size() >= 5 && path.substr(path.size() - 5) == ".wasm");
}
void append_fragment(const std::vector<watx::Token> &fragment, std::vector<watx::Token> &tokens) {
    if (fragment.size() >= 2 && fragment[0].text == "(" && fragment[1].text == "module") {
        int depth = 0;
        for (std::size_t i = 0; i < fragment.size(); ++i) {
            if (fragment[i].text == "(")
                ++depth;
            else if (fragment[i].text == ")")
                --depth;
            if (depth <= 0 && i + 1 != fragment.size())
                throw std::runtime_error("unexpected text after module");
        }
        if (depth)
            throw std::runtime_error("unterminated module");
        std::size_t first = 2;
        if (first < fragment.size() && !fragment[first].text.empty() &&
            fragment[first].text[0] == '$')
            ++first;
        for (std::size_t i = first; i + 1 < fragment.size(); ++i)
            tokens.push_back(fragment[i]);
    } else
        tokens.insert(tokens.end(), fragment.begin(), fragment.end());
}
watx::Module compile_text(const watx::CliOptions &options,
                          const std::vector<unsigned char> &first_input, unsigned workers) {
    std::vector<std::vector<watx::Token> > fragments(options.inputs.size());
    std::vector<std::string> paths(options.inputs.size());
    std::set<std::string> dependencies;
    for (std::size_t i = 0; i < options.inputs.size(); ++i) {
        if (options.inputs[i] == "-") {
            std::vector<unsigned char> input = i == 0 ? first_input : read_bytes("-");
            watx::append_expanded_source(std::string(input.begin(), input.end()), ".", fragments[i],
                                         &dependencies);
        } else {
            paths[i] = watx::canonical_source_path(options.inputs[i]);
            watx::append_expanded_file(options.inputs[i], fragments[i], &dependencies);
        }
    }
    std::vector<watx::Token> tokens;
    tokens.push_back(watx::Token("("));
    tokens.push_back(watx::Token("module"));
    std::set<std::string> roots;
    for (std::size_t i = 0; i < options.inputs.size(); ++i) {
        if (!paths[i].empty() && (dependencies.count(paths[i]) || !roots.insert(paths[i]).second)) {
            if (options.debug)
                std::cerr << "watx: skipping redundant top-level input '" << options.inputs[i]
                          << "'\n";
            continue;
        }
        append_fragment(fragments[i], tokens);
    }
    tokens.push_back(watx::Token(")"));
    watx::Module module = watx::parse_module(tokens);
    watx::compile_module_instructions(module, workers);
    return module;
}
void write_bytes(const std::string &path, const std::vector<unsigned char> &bytes) {
    if (path == "-") {
#ifdef _WIN32
        if (_setmode(_fileno(stdout), _O_BINARY) == -1)
            throw std::runtime_error("could not put standard output into binary mode");
#endif
        if (!bytes.empty())
            std::cout.write(reinterpret_cast<const char *>(&bytes[0]),
                            static_cast<std::streamsize>(bytes.size()));
        std::cout.flush();
        if (!std::cout)
            throw std::runtime_error("failed writing standard output");
    } else {
        std::ofstream output(path.c_str(), std::ios::binary);
        if (!output)
            throw std::runtime_error("could not open output file '" + path + "'");
        if (!bytes.empty())
            output.write(reinterpret_cast<const char *>(&bytes[0]),
                         static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output)
            throw std::runtime_error("failed writing output file '" + path + "'");
    }
}
const char *type_name(watx::ValueType type) {
    switch (type) {
    case watx::TYPE_I32:
        return "i32";
    case watx::TYPE_I64:
        return "i64";
    case watx::TYPE_F32:
        return "f32";
    case watx::TYPE_F64:
        return "f64";
    default:
        return "void";
    }
}
void dump_module(std::ostream &out, const watx::Module &module) {
    out << "types=" << module.types.size() << " functions=" << module.functions.size()
        << " tables=" << module.tables.size() << " memories=" << module.memories.size()
        << " globals=" << module.globals.size() << " elements=" << module.elements.size()
        << " data=" << module.data.size() << '\n';
    for (std::size_t i = 0; i < module.functions.size(); ++i) {
        const watx::Function &function = module.functions[i];
        const watx::FunctionType &type = module.types[function.type_index];
        out << "func[" << i << "] type[" << function.type_index << "] (";
        for (std::size_t j = 0; j < type.parameters.size(); ++j) {
            if (j)
                out << ' ';
            out << type_name(type.parameters[j]);
        }
        out << ") -> ";
        out << (type.results.empty() ? "void" : type_name(type.results[0]));
        if (function.imported)
            out << " import " << function.import_module << '.' << function.import_name;
        else
            out << " instructions=" << function.code.size();
        out << '\n';
    }
    for (std::size_t i = 0; i < module.exports.size(); ++i)
        out << "export \"" << module.exports[i].name
            << "\" kind=" << static_cast<unsigned>(module.exports[i].kind)
            << " index=" << module.exports[i].index << '\n';
    if (module.has_start)
        out << "start=" << module.start << '\n';
}
void dump_hex(std::ostream &out, const std::vector<unsigned char> &bytes) {
    std::ios::fmtflags flags = out.flags();
    char fill = out.fill();
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i % 16 == 0)
            out << std::hex << std::setw(8) << std::setfill('0') << i << "  ";
        out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(bytes[i]);
        out << (i % 16 == 15 || i + 1 == bytes.size() ? '\n' : ' ');
    }
    out.flags(flags);
    out.fill(fill);
}
std::vector<watx::RuntimeValue> arguments_for(const watx::CliOptions &options,
                                              const watx::Module &module) {
    std::vector<watx::RuntimeValue> arguments;
    for (std::size_t i = 0; i < options.arguments.size(); ++i) {
        const std::string &text = options.arguments[i];
        std::size_t colon = text.find(':');
        if (colon == std::string::npos)
            throw std::runtime_error("expected typed argument such as i32:10");
        watx::ValueType type = watx::value_type(text.substr(0, colon));
        const std::string literal = text.substr(colon + 1);
        uint64_t bits = 0;
        if (type == watx::TYPE_I32 || type == watx::TYPE_I64)
            bits = watx::parse_integer_bits(literal, type == watx::TYPE_I32 ? 32 : 64);
        else {
            std::vector<unsigned char> bytes =
                watx::parse_float_bits(literal, type == watx::TYPE_F32 ? 32 : 64);
            for (std::size_t b = 0; b < bytes.size(); ++b)
                bits |= static_cast<uint64_t>(bytes[b]) << (b * 8);
        }
        arguments.push_back(watx::RuntimeValue(type, bits));
    }
    const watx::FunctionType *signature = 0;
    for (std::size_t i = 0; i < module.exports.size(); ++i)
        if (module.exports[i].name == options.function && module.exports[i].kind == 0)
            signature = &module.types[module.functions[module.exports[i].index].type_index];
    if (!signature)
        throw std::runtime_error("exported function '" + options.function + "' was not found");
    if (signature->parameters.size() != arguments.size())
        throw std::runtime_error("export argument count does not match function signature");
    for (std::size_t i = 0; i < arguments.size(); ++i)
        if (signature->parameters[i] != arguments[i].type)
            throw std::runtime_error("export argument type does not match function signature");
    return arguments;
}
}
int main(int argc, char **argv) {
    bool debug = false;
    std::string stage = "options";
    const std::clock_t started = std::clock();
    try {
        const watx::CliOptions options = watx::parse_cli_options(argc, argv);
        debug = options.debug;
        if (options.help) {
            std::cout << watx::cli_help();
            return 0;
        }
        if (options.version) {
            std::cout << "watx " WATX_VERSION " (C++98, " << watx::JitEngine::target() << ")\n";
            return 0;
        }
        const unsigned execution_threads =
            watx::worker_count(options.jit ? options.jit_threads : options.interpreter_threads);
        const unsigned compilation_workers = 1;
        if (debug)
            std::cerr << "watx: target=" << watx::JitEngine::target()
                      << " engine=" << (options.jit ? "jit" : "interpreter")
                      << " execution_workers=" << execution_threads
                      << " preparation_workers=" << (options.jit ? compilation_workers : 0) << '\n';
        stage = "input";
        std::vector<unsigned char> bytes = read_bytes(options.inputs[0]);
        const bool binary = binary_input(options, bytes);
        if (binary && options.inputs.size() != 1)
            throw std::runtime_error("a binary module must be the only input");
        stage = binary ? "binary decoding" : "text compilation";
        watx::Module module =
            binary ? watx::read_module(bytes) : compile_text(options, bytes, compilation_workers);
        stage = "validation";
        watx::validate_module(module, compilation_workers);
        if (!binary)
            bytes = watx::write_module(module);
        if (debug)
            std::cerr << "watx: input=" << (binary ? "wasm" : "wat") << " bytes=" << bytes.size()
                      << " functions=" << module.functions.size() << '\n';
        std::vector<watx::RuntimeValue> arguments;
        if (options.run)
            arguments = arguments_for(options, module);
        std::auto_ptr<watx::JitEngine> jit;
        if (options.jit) {
            stage = "JIT compilation";
            jit.reset(new watx::JitEngine(module));
            jit->prepare(compilation_workers);
            if (debug)
                std::cerr << "watx: native JIT preparation complete\n";
        }
        stage = "output";
        if (!options.output.empty())
            write_bytes(options.output, bytes);
        std::ostream &details = options.raw_output || options.output == "-" ? std::cerr : std::cout;
        if (options.dump)
            dump_module(details, module);
        if (options.dump_hex)
            dump_hex(details, bytes);
        if (options.run) {
            stage = "execution";
            watx::RuntimeOptions runtime_options;
            runtime_options.trace = options.trace;
            runtime_options.run_start = options.run_start;
            runtime_options.fuel = options.fuel;
            runtime_options.stack_limit = options.stack_limit;
            runtime_options.memory_limit_pages = options.memory_limit_pages;
            watx::ThreadedExecutor executor(execution_threads, jit.get());
            watx::RuntimeInstance instance(module, runtime_options);
            instance.set_executor(&executor);
            instance.initialize();
            std::vector<watx::RuntimeValue> results =
                instance.invoke_export(options.function, arguments);
            for (std::size_t i = 0; i < results.size(); ++i)
                std::cout << watx::format_runtime_value(results[i], options.raw_results) << '\n';
        }
        if (debug)
            std::cerr << "watx: completed cpu_seconds="
                      << static_cast<double>(std::clock() - started) / CLOCKS_PER_SEC << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "watx: " << error.what() << '\n';
        if (debug)
            std::cerr << "watx: failed during " << stage << '\n';
        return stage == "options" ? 2 : 1;
    } catch (...) {
        std::cerr << "watx: unknown failure during " << stage << '\n';
        return 1;
    }
}
