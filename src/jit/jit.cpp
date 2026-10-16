#include <algorithm>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <stdint.h>
#include <string>
#include <vector>
#include <watx/jit/jit.h>
#include <watx/jit/platform/code_memory.h>
#include <watx/platform/threads.h>
#include <watx/validator.h>
#if defined(__x86_64__) || defined(_M_X64)
#include <watx/jit/x86_64/platform/emitter.h>
#elif defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
#include <watx/jit/arm64/platform/emitter.h>
#endif

namespace watx {
typedef uint32_t (*NativeStep)(RuntimeFrame *, uint32_t);
static uint32_t jit_step_gateway(RuntimeFrame *frame, uint32_t pc) {
    try {
        return runtime_step(frame, pc);
    } catch (const std::exception &error) {
        if (frame) {
            frame->failed = true;
            frame->finished = true;
            try {
                frame->error = error.what();
            } catch (...) {
            }
        }
        return 0xffffffffU;
    } catch (...) {
        if (frame) {
            frame->failed = true;
            frame->finished = true;
            try {
                frame->error = "unknown exception in runtime instruction";
            } catch (...) {
            }
        }
        return 0xffffffffU;
    }
}
struct JitEngine::Impl {
    struct Compiled {
        void (*entry)(RuntimeFrame *);
        void (*fast_entry)(RuntimeFrame *, RuntimeNativeFrame *);
        Compiled() : entry(0), fast_entry(0) {}
    };
    const Module &module;
    std::vector<unsigned> instruction_counts;
    std::vector<unsigned char> imported;
    std::vector<Compiled> code;
    std::vector<unsigned> defined_functions;
    std::vector<std::vector<unsigned char> > emitted;
    std::vector<std::size_t> fast_offsets;
    void *memory;
    std::size_t memory_size;
    bool prepared;
    Impl(const Module &m) : module(m), memory(0), memory_size(0), prepared(false) {
        instruction_counts.resize(module.functions.size());
        imported.resize(module.functions.size());
        code.resize(module.functions.size());
        emitted.resize(module.functions.size());
        fast_offsets.resize(module.functions.size());
        for (std::size_t i = 0; i < module.functions.size(); ++i) {
            instruction_counts[i] = static_cast<unsigned>(module.functions[i].code.size());
            imported[i] = module.functions[i].imported ? 1 : 0;
            if (!module.functions[i].imported)
                defined_functions.push_back(static_cast<unsigned>(i));
        }
    }
    ~Impl() {
        jit_platform::release_code(memory, memory_size);
    }
    struct Job {
        Impl *impl;
    };
    static void compile_one(void *context, std::size_t index) {
        Job *job = static_cast<Job *>(context);
        Impl *impl = job->impl;
        unsigned function = impl->defined_functions[index];
        std::vector<unsigned> instructions;
        std::vector<unsigned char> bytes;
        std::vector<unsigned char> fast;
        const unsigned count = impl->instruction_counts[function];
        instructions.resize(count);
        for (unsigned pc = 0; pc < count; ++pc)
            instructions[pc] = pc;
#if defined(__x86_64__) || defined(_M_X64)
        jit_platform::emit_x86_64(instructions, &jit_step_gateway, bytes);
        jit_platform::emit_fast_x86_64(impl->module.functions[function].code, &jit_step_gateway,
                                       fast);
#elif defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
        jit_platform::emit_arm64(instructions, &jit_step_gateway, bytes);
        jit_platform::emit_fast_arm64(impl->module.functions[function].code, &jit_step_gateway,
                                      fast);
#else
        throw std::runtime_error("native JIT is not supported on this processor");
#endif
        const std::size_t padding = (4096U - bytes.size() % 4096U) % 4096U;
        if (bytes.size() > static_cast<std::size_t>(-1) - padding)
            throw std::length_error("JIT function code size overflow");
        bytes.resize(bytes.size() + padding, 0);
        impl->fast_offsets[function] = bytes.size();
        bytes.insert(bytes.end(), fast.begin(), fast.end());
        impl->emitted[function].swap(bytes);
    }
};
JitEngine::JitEngine(const Module &module) : impl_(new Impl(module)) {}
JitEngine::~JitEngine() {
    delete impl_;
}
void JitEngine::prepare(unsigned workers) {
    if (impl_->prepared)
        return;
    validate_module(impl_->module, workers);
    if (impl_->defined_functions.empty()) {
        impl_->prepared = true;
        return;
    }
    Impl::Job job;
    job.impl = impl_;
    try {
        parallel_for(impl_->defined_functions.size(), workers, &Impl::compile_one, &job);
        std::vector<std::size_t> offsets(impl_->code.size(), 0);
        std::size_t total = 0;
        const std::size_t maximum = static_cast<std::size_t>(-1);
        for (std::size_t i = 0; i < impl_->defined_functions.size(); ++i) {
            unsigned function = impl_->defined_functions[i];
            std::size_t remainder = total % 4096U;
            std::size_t padding = remainder ? 4096U - remainder : 0;
            if (total > maximum - padding)
                throw std::length_error("JIT module code size overflow");
            total += padding;
            offsets[function] = total;
            if (impl_->emitted[function].size() > maximum - total)
                throw std::length_error("JIT module code size overflow");
            total += impl_->emitted[function].size();
        }
        std::vector<unsigned char> module_code(total, 0);
        for (std::size_t i = 0; i < impl_->defined_functions.size(); ++i) {
            unsigned function = impl_->defined_functions[i];
            const std::vector<unsigned char> &function_code = impl_->emitted[function];
            std::copy(function_code.begin(), function_code.end(),
                      module_code.begin() + offsets[function]);
        }
        impl_->memory_size = total;
        impl_->memory = jit_platform::publish_code(&module_code[0], module_code.size());
        unsigned char *base = static_cast<unsigned char *>(impl_->memory);
        for (std::size_t i = 0; i < impl_->defined_functions.size(); ++i) {
            unsigned function = impl_->defined_functions[i];
            void *address = base + offsets[function];
            if (sizeof(impl_->code[function].entry) != sizeof(address))
                throw std::runtime_error("native function pointer size is unsupported");
            std::memcpy(&impl_->code[function].entry, &address, sizeof(address));
            address = base + offsets[function] + impl_->fast_offsets[function];
            if (sizeof(impl_->code[function].fast_entry) != sizeof(address))
                throw std::runtime_error("native function pointer size is unsupported");
            std::memcpy(&impl_->code[function].fast_entry, &address, sizeof(address));
        }
        for (std::size_t i = 0; i < impl_->emitted.size(); ++i) {
            std::vector<unsigned char> empty;
            impl_->emitted[i].swap(empty);
        }
    } catch (...) {
        jit_platform::release_code(impl_->memory, impl_->memory_size);
        impl_->memory = 0;
        impl_->memory_size = 0;
        for (std::size_t i = 0; i < impl_->code.size(); ++i)
            impl_->code[i] = Impl::Compiled();
        for (std::size_t i = 0; i < impl_->emitted.size(); ++i) {
            std::vector<unsigned char> empty;
            impl_->emitted[i].swap(empty);
        }
        throw;
    }
    impl_->prepared = true;
}
void JitEngine::execute(RuntimeFrame &frame) {
    if (!impl_->prepared)
        throw std::runtime_error("JIT engine has not been prepared");
    if (!frame.instance || &frame.instance->module() != &impl_->module)
        throw std::runtime_error("JIT engine and runtime instance use different modules");
    if (frame.function_index >= impl_->code.size())
        throw std::runtime_error("JIT function index is out of range");
    if (impl_->imported[frame.function_index])
        throw std::runtime_error("JIT cannot execute an imported function directly");
    void (*entry)(RuntimeFrame *) = impl_->code[frame.function_index].entry;
    if (!entry)
        throw std::runtime_error("JIT function has no compiled entry point");
    RuntimeNativeFrame *native = runtime_native_frame(&frame);
    if (native && impl_->code[frame.function_index].fast_entry)
        impl_->code[frame.function_index].fast_entry(&frame, native);
    else
        entry(&frame);
    if (!frame.finished) {
        frame.failed = true;
        frame.finished = true;
        try {
            frame.error = "JIT exited without finishing the runtime frame";
        } catch (...) {
        }
        throw std::runtime_error("JIT exited without finishing the runtime frame");
    }
}
const char *JitEngine::target() {
#if defined(__x86_64__) || defined(_M_X64)
#if defined(_WIN32)
    return "x86_64-windows-x64";
#elif defined(__APPLE__)
    return "x86_64-macos-sysv";
#elif defined(__linux__)
    return "x86_64-linux-sysv";
#else
    return "x86_64-sysv";
#endif
#elif defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
#if defined(_WIN32)
    return "arm64-windows-aapcs64";
#elif defined(__APPLE__)
    return "arm64-macos-aapcs64";
#elif defined(__linux__)
    return "arm64-linux-aapcs64";
#else
    return "arm64-aapcs64";
#endif
#else
    return "unsupported-native-jit-target";
#endif
}
}
