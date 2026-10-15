#ifndef WATX_RUNTIME_EXECUTOR_H
#define WATX_RUNTIME_EXECUTOR_H
#include <watx/export.h>
#include <watx/runtime/runtime.h>
namespace watx {
class WATX_API InterpreterExecutor : public RuntimeExecutor {
  public:
    virtual void execute(RuntimeFrame &frame);
};
class WATX_API ThreadedExecutor : public RuntimeExecutor {
  public:
    explicit ThreadedExecutor(unsigned threads = 1, RuntimeExecutor *backend = 0);
    ~ThreadedExecutor();
    virtual void execute(RuntimeFrame &frame);
    unsigned thread_count() const;

  private:
    ThreadedExecutor(const ThreadedExecutor &);
    ThreadedExecutor &operator=(const ThreadedExecutor &);
    struct Impl;
    Impl *impl_;
};
}
#endif
