#ifndef WATX_JIT_JIT_H
#define WATX_JIT_JIT_H
#include <watx/export.h>
#include <watx/runtime/runtime.h>
namespace watx {
class WATX_API JitEngine : public RuntimeExecutor {
  public:
    explicit JitEngine(const Module &module);
    ~JitEngine();
    void prepare(unsigned workers);
    virtual void execute(RuntimeFrame &frame);
    static const char *target();

  private:
    JitEngine(const JitEngine &);
    JitEngine &operator=(const JitEngine &);
    struct Impl;
    Impl *impl_;
};
}
#endif
