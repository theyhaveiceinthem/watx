#ifndef WATX_RUNTIME_SYNC_H
#define WATX_RUNTIME_SYNC_H
#include <stdint.h>
#include <watx/export.h>
namespace watx {
class WATX_API RuntimeCancellation {
  public:
    RuntimeCancellation();
    ~RuntimeCancellation();
    bool cancelled() const;
    void cancel();
    void reset();

  private:
    RuntimeCancellation(const RuntimeCancellation &);
    RuntimeCancellation &operator=(const RuntimeCancellation &);
    mutable void *impl_;
};
class WATX_API RuntimeMutex {
  public:
    RuntimeMutex();
    ~RuntimeMutex();
    void lock();
    void unlock();

  private:
    RuntimeMutex(const RuntimeMutex &);
    RuntimeMutex &operator=(const RuntimeMutex &);
    void *impl_;
    friend class RuntimeCondition;
};
class WATX_API RuntimeCondition {
  public:
    RuntimeCondition();
    ~RuntimeCondition();
    bool wait(RuntimeMutex &mutex, int64_t timeout_nanoseconds);
    void notify_one();
    void notify_all();

  private:
    RuntimeCondition(const RuntimeCondition &);
    RuntimeCondition &operator=(const RuntimeCondition &);
    void *impl_;
};
class WATX_API RuntimeThread {
  public:
    RuntimeThread();
    ~RuntimeThread();
    bool start(void (*entry)(void *), void *argument);
    void join();
    bool valid() const;

  private:
    RuntimeThread(const RuntimeThread &);
    RuntimeThread &operator=(const RuntimeThread &);
    void *impl_;
};
WATX_API void runtime_thread_yield();
WATX_API bool runtime_set_thread_context(void *group, uint32_t id, RuntimeCancellation *cancelled,
                                         uint64_t *fuel_budget = 0);
WATX_API void runtime_clear_thread_context();
WATX_API uint64_t runtime_monotonic_nanoseconds();
WATX_API void *runtime_current_thread_group();
WATX_API uint32_t runtime_current_thread_id();
WATX_API RuntimeCancellation *runtime_current_cancel_flag();
WATX_API uint64_t *runtime_current_fuel_budget();
}
#endif
