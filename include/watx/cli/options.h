#ifndef WATX_CLI_OPTIONS_H
#define WATX_CLI_OPTIONS_H
#include <stdint.h>
#include <string>
#include <vector>
namespace watx {
struct CliOptions {
    enum InputKind { INPUT_AUTO, INPUT_WAT, INPUT_WASM };
    InputKind input_kind;
    std::vector<std::string> inputs, arguments;
    std::string output, function;
    bool help, version, debug, trace, run, jit, validate_only;
    bool dump, dump_hex, raw_output, raw_results, run_start;
    unsigned interpreter_threads, jit_threads;
    uint64_t fuel;
    uint32_t stack_limit, memory_limit_pages;
    CliOptions();
};
CliOptions parse_cli_options(int argc, char **argv);
std::string cli_help();
}
#endif
