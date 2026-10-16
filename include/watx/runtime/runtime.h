#ifndef WATX_RUNTIME_RUNTIME_H
#define WATX_RUNTIME_RUNTIME_H
#include <map>
#include <stdint.h>
#include <string>
#include <vector>
#include <watx/export.h>
#include <watx/model.h>
#include <watx/runtime/native_frame.h>
#include <watx/runtime/sync.h>
namespace watx {
struct WATX_API RuntimeValue {
    ValueType type;
    uint64_t bits;
    RuntimeValue() : type(TYPE_I32), bits(0) {}
    RuntimeValue(ValueType t, uint64_t b) : type(t), bits(b) {}
    uint32_t as_u32() const;
    int32_t as_i32() const;
    uint64_t as_u64() const;
    int64_t as_i64() const;
    float as_f32() const;
    double as_f64() const;
    static RuntimeValue i32(uint32_t value);
    static RuntimeValue i64(uint64_t value);
    static RuntimeValue f32(float value);
    static RuntimeValue f64(double value);
};
struct RuntimeOptions {
    bool trace, run_start;
    uint64_t fuel;
    uint32_t stack_limit, memory_limit_pages;
    RuntimeOptions()
        : trace(false), run_start(true), fuel(0), stack_limit(1024), memory_limit_pages(65536) {}
};
class RuntimeInstance;
struct RuntimeFrame {
    RuntimeInstance *instance;
    uint32_t function_index;
    void *state;
    bool finished, failed;
    std::string error;
    RuntimeFrame(RuntimeInstance *i, uint32_t f, void *s)
        : instance(i), function_index(f), state(s), finished(false), failed(false) {}
};
struct WATX_API RuntimeMemory {
    std::vector<unsigned char> bytes;
    uint32_t maximum_pages;
    bool has_maximum, shared;
    RuntimeMutex mutex;
    RuntimeMemory() : maximum_pages(0), has_maximum(false), shared(false) {}
    ~RuntimeMemory();

  private:
    RuntimeMemory(const RuntimeMemory &);
    RuntimeMemory &operator=(const RuntimeMemory &);

  public:
    struct WaitQueue {
        struct Waiter {
            RuntimeCondition condition;
            bool notified;
            Waiter() : notified(false) {}
        };
        std::vector<Waiter *> waiters;
    };
    std::map<uint64_t, WaitQueue *> waiters;
    void wake_all();
};
struct RuntimeFunctionRef {
    RuntimeInstance *instance;
    uint32_t function_index;
    RuntimeFunctionRef() : instance(0), function_index(0) {}
    RuntimeFunctionRef(RuntimeInstance *i, uint32_t f) : instance(i), function_index(f) {}
};
struct RuntimeTable {
    std::vector<RuntimeFunctionRef> functions;
    uint32_t maximum_size;
    bool has_maximum;
    RuntimeMutex mutex;
    RuntimeTable() : maximum_size(0), has_maximum(false) {}
};
struct RuntimeGlobal {
    RuntimeValue value;
    bool mutable_;
    RuntimeMutex mutex;
    RuntimeGlobal() : mutable_(false) {}

  private:
    RuntimeGlobal(const RuntimeGlobal &);
    RuntimeGlobal &operator=(const RuntimeGlobal &);
};
struct RuntimeFunctionImport {
    typedef std::vector<RuntimeValue> (*Callback)(void *, const std::vector<RuntimeValue> &);
    Callback callback;
    void *context;
    RuntimeFunctionImport() : callback(0), context(0) {}
    RuntimeFunctionImport(Callback c, void *p = 0) : callback(c), context(p) {}
};
WATX_API uint32_t runtime_step(RuntimeFrame *frame, uint32_t pc);
WATX_API void runtime_interpret(RuntimeFrame *frame);
class WATX_API RuntimeExecutor {
  public:
    virtual ~RuntimeExecutor() {}
    virtual void execute(RuntimeFrame &frame) = 0;
};
struct RuntimeImports;
class WATX_API RuntimeInstance {
  public:
    struct Impl;
    RuntimeInstance(const Module &module, const RuntimeOptions &options = RuntimeOptions());
    RuntimeInstance(const Module &module, const RuntimeOptions &options,
                    const RuntimeImports &imports);
    ~RuntimeInstance();
    void set_executor(RuntimeExecutor *executor);
    void set_import(const std::string &module, const std::string &name,
                    const RuntimeFunctionImport &function);
    RuntimeMemory *memory(uint32_t index = 0);
    RuntimeTable *table(uint32_t index = 0);
    RuntimeGlobal *global(uint32_t index);
    void initialize();
    std::vector<RuntimeValue> invoke_export(const std::string &name,
                                            const std::vector<RuntimeValue> &arguments);
    std::vector<RuntimeValue> invoke(uint32_t function, const std::vector<RuntimeValue> &arguments);
    const Module &module() const;

  private:
    RuntimeInstance(const RuntimeInstance &);
    RuntimeInstance &operator=(const RuntimeInstance &);
    Impl *impl_;
    friend uint32_t runtime_step(RuntimeFrame *, uint32_t);
    friend void runtime_interpret(RuntimeFrame *);
    friend RuntimeNativeFrame *runtime_native_frame(RuntimeFrame *);
};
WATX_API std::string format_runtime_value(const RuntimeValue &value, bool raw = false);
}
#endif
