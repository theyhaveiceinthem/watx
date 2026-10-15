#include <watx/cli/options.h>

#include <limits>
#include <stdexcept>

namespace watx {
namespace {

static bool decimal_value(const std::string &text, uint64_t maximum, uint64_t &value) {
    if (text.empty())
        return false;
    uint64_t parsed = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < '0' || c > '9')
            return false;
        unsigned int digit = c - '0';
        if (parsed > maximum / 10 || (parsed == maximum / 10 && digit > maximum % 10))
            throw std::runtime_error("numeric option value is out of range: " + text);
        parsed = parsed * 10 + digit;
    }
    value = parsed;
    return true;
}

static uint64_t required_number(const std::string &text, uint64_t maximum, const char *option) {
    uint64_t value = 0;
    if (!decimal_value(text, maximum, value))
        throw std::runtime_error(std::string("expected an unsigned decimal value for ") + option);
    return value;
}

static std::string option_value(const std::string &arg, const char *name, int &i, int argc,
                                char **argv) {
    const std::string prefix = std::string(name) + "=";
    if (arg.compare(0, prefix.size(), prefix) == 0)
        return arg.substr(prefix.size());
    if (arg == name) {
        if (i + 1 >= argc)
            throw std::runtime_error(std::string("missing value for ") + name);
        return argv[++i];
    }
    return std::string();
}

static bool is_option(const std::string &arg) {
    return arg.size() > 1 && arg[0] == '-';
}

static bool is_information_only(const CliOptions &options) {
    return options.help || options.version;
}

}

CliOptions::CliOptions()
    : input_kind(INPUT_AUTO), help(false), version(false), debug(false), trace(false), run(false),
      jit(false), validate_only(false), dump(false), dump_hex(false), raw_output(false),
      raw_results(false), run_start(true), interpreter_threads(1), jit_threads(1), fuel(0),
      stack_limit(1024), memory_limit_pages(65536) {}

CliOptions parse_cli_options(int argc, char **argv) {
    CliOptions options;
    bool end_options = false;
    bool output_seen = false;
    bool function_seen = false;
    bool threads_seen = false;
    bool jit_threads_seen = false;
    bool jit_seen = false;
    bool interpreter_seen = false;
    bool fuel_seen = false;
    bool stack_seen = false;
    bool memory_seen = false;
    bool input_kind_seen = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i] ? argv[i] : "");
        if (end_options) {
            options.arguments.push_back(arg);
            continue;
        }
        if (arg == "--") {
            end_options = true;
            continue;
        }
        if (arg == "-h" || arg == "--help") {
            options.help = true;
            return options;
        } else if (arg == "--version") {
            options.version = true;
            return options;

        } else if (arg == "--run") {
            options.run = true;
        } else if (arg == "--function" || arg.compare(0, 11, "--function=") == 0) {
            if (function_seen)
                throw std::runtime_error("--function may be specified only once");
            options.function = option_value(arg, "--function", i, argc, argv);
            function_seen = true;
        } else if (arg == "--jit" || arg.compare(0, 6, "--jit=") == 0) {
            if (interpreter_seen)
                throw std::runtime_error("--jit conflicts with --interpreter");
            options.jit = true;
            jit_seen = true;
            std::string count;
            if (arg.compare(0, 6, "--jit=") == 0) {
                count = arg.substr(6);
                if (count.empty())
                    throw std::runtime_error("--jit requires an unsigned thread count after '='");
            } else if (i + 1 < argc) {
                uint64_t parsed = 0;
                std::string next(argv[i + 1] ? argv[i + 1] : "");
                if (decimal_value(next, std::numeric_limits<unsigned>::max(), parsed)) {
                    count = next;
                    ++i;
                }
            }
            if (!count.empty()) {
                if (jit_threads_seen)
                    throw std::runtime_error("JIT thread count may be specified only once");
                options.jit_threads = static_cast<unsigned>(
                    required_number(count, std::numeric_limits<unsigned>::max(), "--jit"));
                jit_threads_seen = true;
            }
        } else if (arg == "--interpreter") {
            if (jit_seen)
                throw std::runtime_error("--interpreter conflicts with --jit");
            options.jit = false;
            interpreter_seen = true;
        } else if (arg == "--threads" || arg.compare(0, 10, "--threads=") == 0 || arg == "-j" ||
                   (arg.size() > 2 && arg.compare(0, 2, "-j") == 0)) {
            std::string value;
            if (arg == "-j") {
                if (i + 1 >= argc)
                    throw std::runtime_error("missing value for -j");
                value = argv[++i];
            } else if (arg.compare(0, 3, "-j=") == 0) {
                value = arg.substr(3);
            } else if (arg.size() > 2 && arg.compare(0, 2, "-j") == 0) {
                value = arg.substr(2);
            } else {
                value = option_value(arg, "--threads", i, argc, argv);
            }
            if (threads_seen)
                throw std::runtime_error("interpreter thread count may be specified only once");
            options.interpreter_threads = static_cast<unsigned>(
                required_number(value, std::numeric_limits<unsigned>::max(), "--threads"));
            threads_seen = true;
        } else if (arg == "--debug" || arg == "-g") {
            options.debug = true;
        } else if (arg == "--trace") {
            options.trace = true;
        } else if (arg == "--validate" || arg == "--validate-only") {
            options.validate_only = true;
        } else if (arg == "--dump") {
            options.dump = true;
        } else if (arg == "--dump-hex") {
            options.dump_hex = true;
        } else if (arg == "--wat") {
            if (input_kind_seen && options.input_kind != CliOptions::INPUT_WAT)
                throw std::runtime_error("--wat conflicts with --wasm/--raw-input");
            options.input_kind = CliOptions::INPUT_WAT;
            input_kind_seen = true;
        } else if (arg == "--wasm" || arg == "--raw-input") {
            if (input_kind_seen && options.input_kind != CliOptions::INPUT_WASM)
                throw std::runtime_error("--wasm/--raw-input conflicts with --wat");
            options.input_kind = CliOptions::INPUT_WASM;
            input_kind_seen = true;
        } else if (arg == "--raw-output") {
            if (output_seen && options.output != "-")
                throw std::runtime_error("--raw-output conflicts with a named output file");
            options.raw_output = true;
            options.output = "-";
            output_seen = true;
        } else if (arg == "--raw-results" || arg == "--raw") {
            options.raw_results = true;
        } else if (arg == "--no-start") {
            options.run_start = false;
        } else if (arg == "--fuel" || arg.compare(0, 7, "--fuel=") == 0) {
            if (fuel_seen)
                throw std::runtime_error("--fuel may be specified only once");
            std::string value = option_value(arg, "--fuel", i, argc, argv);
            options.fuel = required_number(value, std::numeric_limits<uint64_t>::max(), "--fuel");
            fuel_seen = true;
        } else if (arg == "--stack-limit" || arg.compare(0, 14, "--stack-limit=") == 0) {
            if (stack_seen)
                throw std::runtime_error("--stack-limit may be specified only once");
            std::string value = option_value(arg, "--stack-limit", i, argc, argv);
            uint64_t parsed =
                required_number(value, std::numeric_limits<uint32_t>::max(), "--stack-limit");
            if (parsed == 0)
                throw std::runtime_error("--stack-limit must be positive");
            options.stack_limit = static_cast<uint32_t>(parsed);
            stack_seen = true;
        } else if (arg == "--memory-limit" || arg.compare(0, 15, "--memory-limit=") == 0) {
            if (memory_seen)
                throw std::runtime_error("--memory-limit may be specified only once");
            std::string value = option_value(arg, "--memory-limit", i, argc, argv);
            options.memory_limit_pages =
                static_cast<uint32_t>(required_number(value, 65536, "--memory-limit"));
            memory_seen = true;
        } else if (arg == "--output" || arg.compare(0, 9, "--output=") == 0 || arg == "-o") {
            if (output_seen)
                throw std::runtime_error("output may be specified only once");
            if (arg == "-o") {
                if (i + 1 >= argc)
                    throw std::runtime_error("missing value for -o");
                options.output = argv[++i];
            } else {
                options.output = option_value(arg, "--output", i, argc, argv);
            }
            if (options.output.empty())
                throw std::runtime_error("output path must not be empty");
            output_seen = true;
            if (options.output == "-")
                options.raw_output = true;
        } else if (arg == "-") {
            options.inputs.push_back(arg);
        } else if (is_option(arg)) {
            throw std::runtime_error("unknown option '" + arg + "'");
        } else {
            options.inputs.push_back(arg);
        }
    }

    if (options.inputs.empty() && !is_information_only(options))
        throw std::runtime_error(options.run ? "--run requires an input file, for example: watx "
                                               "module.wasm --run --function NAME"
                                             : "an input path is required");
    unsigned stdin_count = 0;
    for (std::size_t i = 0; i < options.inputs.size(); ++i)
        if (options.inputs[i] == "-")
            ++stdin_count;
    if (stdin_count > 1)
        throw std::runtime_error("standard input may be specified only once");
    if (options.jit && !options.run)
        throw std::runtime_error("--jit requires --run");
    if (threads_seen && options.jit)
        throw std::runtime_error(
            "--threads controls interpreter threads; use --jit N for JIT threads");
    if (threads_seen && !options.run)
        throw std::runtime_error("--threads requires --run");
    if (options.run && !function_seen)
        throw std::runtime_error("--run requires --function <export>");
    if (function_seen && !options.run)
        throw std::runtime_error("--function is only valid with --run");
    if (options.trace && !options.run)
        throw std::runtime_error("--trace requires --run");
    if (options.raw_results && !options.run)
        throw std::runtime_error("--raw-results is only valid with --run");
    if (!options.run_start && !options.run)
        throw std::runtime_error("--no-start is only valid with --run");
    if (options.run && options.validate_only)
        throw std::runtime_error("--run cannot be combined with --validate-only");
    if (options.validate_only && output_seen)
        throw std::runtime_error("--validate-only cannot be combined with an output path");
    if (options.raw_output && options.run)
        throw std::runtime_error("--raw-output cannot be combined with --run");
    if (options.raw_output && !options.output.empty() && options.output != "-")
        throw std::runtime_error("--raw-output cannot be combined with a named output file");
    if (!options.arguments.empty() && !options.run)
        throw std::runtime_error("typed arguments after '--' require --run");

    const bool non_compiling_operation =
        options.run || options.jit || options.validate_only || options.dump || options.dump_hex;
    if (options.output.empty() && !non_compiling_operation && !is_information_only(options))
        throw std::runtime_error("compiler mode requires --output/-o <file>");

    return options;
}

std::string cli_help() {
    return "Usage: watx [options] <input...> [-- <typed arguments...>]\n"
           "Compile WebAssembly text to a WebAssembly binary, inspect a module, or run an export.\n"
           "\n"
           "Inputs and output:\n"
           "  <input...>                 Input files; '-' reads standard input\n"
           "  -o, --output PATH          Write compiled binary to PATH\n"
           "  --raw-output               Write binary bytes to standard output\n"
           "  --wat                      Treat inputs as WebAssembly text\n"
           "  --wasm, --raw-input        Treat input as a WebAssembly binary\n"
           "\n"
           "Run and validation options:\n"
           "  --run --function NAME      Run an exported function\n"
           "  --jit[=N], --jit N         Use JIT; optionally set execution workers\n"
           "  --interpreter              Use the interpreter\n"
           "  -j N, -jN, --threads N     Set interpreter execution workers (default: 1)\n"
           "  --validate, --validate-only Validate without writing output\n"
           "  --no-start                 Do not run the module start function\n"
           "  --fuel N                   Set instruction fuel (0 means unlimited)\n"
           "  --stack-limit N            Set stack limit (default: 1024)\n"
           "  --memory-limit PAGES       Set memory limit in pages (default: 65536)\n"
           "  --raw-results, --raw       Omit return type prefixes\n"
           "  --trace                    Trace a run\n"
           "  --debug, -g                Enable debug output\n"
           "\n"
           "Other options:\n"
           "  --dump                     Dump the parsed module\n"
           "  --dump-hex                 Dump binary bytes as hexadecimal\n"
           "  --version                  Show version\n"
           "  -h, --help                 Show this help\n"
           "  --                         Pass all following tokens as typed arguments\n"
           "\n"
           "Worker counts apply only to execution; 0 uses all hardware threads.\n"
           "These options do not change WAT syntax or repeat the selected function.\n"
           "\n"
           "Examples:\n"
           "  watx module.wat -o module.wasm\n"
           "  watx --validate-only module.wasm\n"
           "  watx --run --function add module.wasm -- i32:3 i32:4\n";
}

}
