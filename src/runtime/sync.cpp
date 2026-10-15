#include <new>
#include <stdexcept>
#include <vector>
#include <watx/runtime/sync.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#endif
namespace watx {
namespace {
struct ContextValue {
    void *group;
    uint32_t id;
    RuntimeCancellation *cancelled;
    uint64_t *fuel_budget;
    ContextValue() : group(0), id(0), cancelled(0), fuel_budget(0) {}
};
struct ThreadContext : ContextValue {
    std::vector<ContextValue> previous;
    bool active;
    ThreadContext() : active(false) {}
};
void delete_context(ThreadContext *value) {
    delete value;
}
#if defined(_WIN32)
DWORD context_key = FLS_OUT_OF_INDEXES;
INIT_ONCE context_once = INIT_ONCE_STATIC_INIT;
VOID CALLBACK delete_context_callback(PVOID value) {
    delete_context(static_cast<ThreadContext *>(value));
}
BOOL CALLBACK make_context_key(PINIT_ONCE, PVOID, PVOID *) {
    context_key = FlsAlloc(delete_context_callback);
    return context_key != FLS_OUT_OF_INDEXES;
}
DWORD error_code() {
    DWORD value = GetLastError();
    return value ? value : ERROR_GEN_FAILURE;
}
ThreadContext *thread_context(bool create) {
    if (!InitOnceExecuteOnce(&context_once, make_context_key, 0, 0) ||
        context_key == FLS_OUT_OF_INDEXES)
        return 0;
    ThreadContext *value = static_cast<ThreadContext *>(FlsGetValue(context_key));
    if (!value && create) {
        value = new (std::nothrow) ThreadContext;
        if (!value)
            return 0;
        if (!FlsSetValue(context_key, value)) {
            delete value;
            return 0;
        }
    }
    return value;
}
struct MutexImpl {
    CRITICAL_SECTION value;
    MutexImpl() {
        if (!InitializeCriticalSectionEx(&value, 0, 0))
            throw std::runtime_error("mutex initialization failed");
    }
    ~MutexImpl() {
        DeleteCriticalSection(&value);
    }
};
struct CancellationImpl {
    RuntimeMutex mutex;
    bool value;
    CancellationImpl() : value(false) {}
};
struct ConditionImpl {
    CONDITION_VARIABLE value;
    ConditionImpl() {
        InitializeConditionVariable(&value);
    }
};
struct ThreadImpl {
    HANDLE value;
    ThreadImpl() : value(0) {}
};
struct StartData {
    void (*entry)(void *);
    void *argument;
};
DWORD WINAPI thread_entry(LPVOID raw) {
    StartData *data = static_cast<StartData *>(raw);
    void (*entry)(void *) = data->entry;
    void *argument = data->argument;
    delete data;
    try {
        entry(argument);
    } catch (...) {
    }
    return 0;
}
void assign_processor_group(uint32_t index) {
    DWORD total = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (!total)
        return;
    DWORD ordinal = index % total;
    WORD groups = GetActiveProcessorGroupCount();
    for (WORD group = 0; group < groups; ++group) {
        DWORD count = GetActiveProcessorCount(group);
        if (ordinal >= count) {
            ordinal -= count;
            continue;
        }
        GROUP_AFFINITY affinity;
        affinity.Group = group;
        affinity.Mask = static_cast<KAFFINITY>(1) << ordinal;
        for (unsigned i = 0; i < sizeof(affinity.Reserved) / sizeof(affinity.Reserved[0]); ++i)
            affinity.Reserved[i] = 0;
        SetThreadGroupAffinity(GetCurrentThread(), &affinity, 0);
        return;
    }
}
#else
pthread_key_t context_key;
pthread_once_t context_once = PTHREAD_ONCE_INIT;
int context_key_status = -1;
void delete_context_callback(void *value) {
    delete_context(static_cast<ThreadContext *>(value));
}
void make_context_key() {
    context_key_status = pthread_key_create(&context_key, delete_context_callback);
}
ThreadContext *thread_context(bool create) {
    if (pthread_once(&context_once, make_context_key) != 0 || context_key_status != 0)
        return 0;
    ThreadContext *value = static_cast<ThreadContext *>(pthread_getspecific(context_key));
    if (!value && create) {
        value = new (std::nothrow) ThreadContext;
        if (!value)
            return 0;
        if (pthread_setspecific(context_key, value) != 0) {
            delete value;
            return 0;
        }
    }
    return value;
}
struct MutexImpl {
    pthread_mutex_t value;
    MutexImpl() {
        if (pthread_mutex_init(&value, 0) != 0)
            throw std::runtime_error("mutex initialization failed");
    }
    ~MutexImpl() {
        pthread_mutex_destroy(&value);
    }
};
struct CancellationImpl {
    RuntimeMutex mutex;
    bool value;
    CancellationImpl() : value(false) {}
};
struct ConditionImpl {
    pthread_cond_t value;
    ConditionImpl() {
#if defined(__APPLE__)
        if (pthread_cond_init(&value, 0) != 0)
            throw std::runtime_error("condition initialization failed");
#else
        pthread_condattr_t attributes;
        if (pthread_condattr_init(&attributes) != 0)
            throw std::runtime_error("condition attribute initialization failed");
        int status = pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
        if (status == 0)
            status = pthread_cond_init(&value, &attributes);
        pthread_condattr_destroy(&attributes);
        if (status != 0)
            throw std::runtime_error("condition initialization failed");
#endif
    }
    ~ConditionImpl() {
        pthread_cond_destroy(&value);
    }
};
struct ThreadImpl {
    pthread_t value;
    bool valid;
    ThreadImpl() : valid(false) {}
};
struct StartData {
    void (*entry)(void *);
    void *argument;
};
void *thread_entry(void *raw) {
    StartData *data = static_cast<StartData *>(raw);
    void (*entry)(void *) = data->entry;
    void *argument = data->argument;
    delete data;
    try {
        entry(argument);
    } catch (...) {
    }
    return 0;
}
#endif
uint64_t deadline_from_timeout(int64_t timeout) {
    uint64_t now = runtime_monotonic_nanoseconds();
    uint64_t maximum = ~static_cast<uint64_t>(0);
    uint64_t amount = static_cast<uint64_t>(timeout);
    return amount > maximum - now ? maximum : now + amount;
}
uint64_t remaining_until(uint64_t deadline) {
    uint64_t now = runtime_monotonic_nanoseconds();
    return now >= deadline ? 0 : deadline - now;
}
#if !defined(_WIN32) && !defined(__APPLE__)
struct timespec monotonic_deadline(uint64_t deadline) {
    uint64_t seconds = deadline / 1000000000;
    if (seconds > static_cast<uint64_t>((time_t)-1) / 2)
        throw std::runtime_error("condition timeout is out of range");
    struct timespec value;
    value.tv_sec = static_cast<time_t>(seconds);
    value.tv_nsec = static_cast<long>(deadline % 1000000000);
    return value;
}
#endif
#if defined(__APPLE__)
struct timespec relative_timeout(uint64_t nanoseconds) {
    struct timespec value;
    value.tv_sec = static_cast<time_t>(nanoseconds / 1000000000);
    value.tv_nsec = static_cast<long>(nanoseconds % 1000000000);
    return value;
}
#endif
}
RuntimeCancellation::RuntimeCancellation() : impl_(new CancellationImpl) {}
RuntimeCancellation::~RuntimeCancellation() {
    delete static_cast<CancellationImpl *>(impl_);
}
bool RuntimeCancellation::cancelled() const {
    CancellationImpl *value = static_cast<CancellationImpl *>(impl_);
    value->mutex.lock();
    bool result = value->value;
    value->mutex.unlock();
    return result;
}
void RuntimeCancellation::cancel() {
    CancellationImpl *value = static_cast<CancellationImpl *>(impl_);
    value->mutex.lock();
    value->value = true;
    value->mutex.unlock();
}
void RuntimeCancellation::reset() {
    CancellationImpl *value = static_cast<CancellationImpl *>(impl_);
    value->mutex.lock();
    value->value = false;
    value->mutex.unlock();
}
RuntimeMutex::RuntimeMutex() : impl_(new MutexImpl) {}
RuntimeMutex::~RuntimeMutex() {
    delete static_cast<MutexImpl *>(impl_);
}
void RuntimeMutex::lock() {
#if defined(_WIN32)
    EnterCriticalSection(&static_cast<MutexImpl *>(impl_)->value);
#else
    int status = pthread_mutex_lock(&static_cast<MutexImpl *>(impl_)->value);
    if (status != 0)
        throw std::runtime_error("mutex lock failed");
#endif
}
void RuntimeMutex::unlock() {
#if defined(_WIN32)
    LeaveCriticalSection(&static_cast<MutexImpl *>(impl_)->value);
#else
    int status = pthread_mutex_unlock(&static_cast<MutexImpl *>(impl_)->value);
    if (status != 0)
        throw std::runtime_error("mutex unlock failed");
#endif
}
RuntimeCondition::RuntimeCondition() : impl_(new ConditionImpl) {}
RuntimeCondition::~RuntimeCondition() {
    delete static_cast<ConditionImpl *>(impl_);
}
bool RuntimeCondition::wait(RuntimeMutex &mutex, int64_t timeout) {
#if defined(_WIN32)
    if (timeout < 0) {
        if (SleepConditionVariableCS(&static_cast<ConditionImpl *>(impl_)->value,
                                     &static_cast<MutexImpl *>(mutex.impl_)->value, INFINITE))
            return true;
        throw std::runtime_error("condition wait failed");
    }
    uint64_t deadline = deadline_from_timeout(timeout);
    for (;;) {
        uint64_t remaining = remaining_until(deadline);
        uint64_t milliseconds = remaining / 1000000;
        if (remaining % 1000000)
            ++milliseconds;
        DWORD duration =
            milliseconds > 0xfffffffe ? 0xfffffffeUL : static_cast<DWORD>(milliseconds);
        if (SleepConditionVariableCS(&static_cast<ConditionImpl *>(impl_)->value,
                                     &static_cast<MutexImpl *>(mutex.impl_)->value, duration))
            return true;
        DWORD error = error_code();
        if (error != ERROR_TIMEOUT)
            throw std::runtime_error("condition wait failed");
        if (remaining_until(deadline) == 0)
            return false;
    }
#else
    if (timeout < 0) {
        int status = pthread_cond_wait(&static_cast<ConditionImpl *>(impl_)->value,
                                       &static_cast<MutexImpl *>(mutex.impl_)->value);
        if (status != 0)
            throw std::runtime_error("condition wait failed");
        return true;
    }
    uint64_t deadline = deadline_from_timeout(timeout);
    for (;;) {
        uint64_t remaining = remaining_until(deadline);
        struct timespec until;
#if defined(__APPLE__)
        until = relative_timeout(remaining);
        int status = pthread_cond_timedwait_relative_np(
            &static_cast<ConditionImpl *>(impl_)->value,
            &static_cast<MutexImpl *>(mutex.impl_)->value, &until);
#else
        until = monotonic_deadline(deadline);
        int status = pthread_cond_timedwait(&static_cast<ConditionImpl *>(impl_)->value,
                                            &static_cast<MutexImpl *>(mutex.impl_)->value, &until);
#endif
        if (status == 0)
            return true;
        if (status != ETIMEDOUT)
            throw std::runtime_error("condition wait failed");
        if (remaining_until(deadline) == 0)
            return false;
    }
#endif
}
void RuntimeCondition::notify_one() {
#if defined(_WIN32)
    WakeConditionVariable(&static_cast<ConditionImpl *>(impl_)->value);
#else
    if (pthread_cond_signal(&static_cast<ConditionImpl *>(impl_)->value) != 0)
        throw std::runtime_error("condition notification failed");
#endif
}
void RuntimeCondition::notify_all() {
#if defined(_WIN32)
    WakeAllConditionVariable(&static_cast<ConditionImpl *>(impl_)->value);
#else
    if (pthread_cond_broadcast(&static_cast<ConditionImpl *>(impl_)->value) != 0)
        throw std::runtime_error("condition notification failed");
#endif
}
RuntimeThread::RuntimeThread() : impl_(new ThreadImpl) {}
RuntimeThread::~RuntimeThread() {
    if (valid()) {
        try {
            join();
        } catch (...) {
        }
    }
    delete static_cast<ThreadImpl *>(impl_);
}
bool RuntimeThread::start(void (*entry)(void *), void *argument) {
    if (!entry || valid())
        return false;
    StartData *data = new (std::nothrow) StartData;
    if (!data)
        return false;
    data->entry = entry;
    data->argument = argument;
#if defined(_WIN32)
    HANDLE handle = CreateThread(0, 8u * 1024u * 1024u, thread_entry, data,
                                 STACK_SIZE_PARAM_IS_A_RESERVATION, 0);
    if (!handle) {
        delete data;
        return false;
    }
    static_cast<ThreadImpl *>(impl_)->value = handle;
#else
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0) {
        delete data;
        return false;
    }
    size_t stack_size = 8u * 1024u * 1024u;
    if (stack_size < PTHREAD_STACK_MIN)
        stack_size = PTHREAD_STACK_MIN;
    if (pthread_attr_setstacksize(&attributes, stack_size) != 0) {
        pthread_attr_destroy(&attributes);
        delete data;
        return false;
    }
    int status =
        pthread_create(&static_cast<ThreadImpl *>(impl_)->value, &attributes, thread_entry, data);
    pthread_attr_destroy(&attributes);
    if (status != 0) {
        delete data;
        return false;
    }
    static_cast<ThreadImpl *>(impl_)->valid = true;
#endif
    return true;
}
void RuntimeThread::join() {
    if (!valid())
        return;
#if defined(_WIN32)
    ThreadImpl *value = static_cast<ThreadImpl *>(impl_);
    if (WaitForSingleObject(value->value, INFINITE) != WAIT_OBJECT_0)
        throw std::runtime_error("thread join failed");
    if (!CloseHandle(value->value))
        throw std::runtime_error("thread handle close failed");
    value->value = 0;
#else
    ThreadImpl *value = static_cast<ThreadImpl *>(impl_);
    int status = pthread_join(value->value, 0);
    if (status != 0)
        throw std::runtime_error("thread join failed");
    value->valid = false;
#endif
}
bool RuntimeThread::valid() const {
#if defined(_WIN32)
    return static_cast<ThreadImpl *>(impl_)->value != 0;
#else
    return static_cast<ThreadImpl *>(impl_)->valid;
#endif
}
void runtime_thread_yield() {
#if defined(_WIN32)
    SwitchToThread();
#else
    sched_yield();
#endif
}
bool runtime_set_thread_context(void *group, uint32_t id, RuntimeCancellation *cancelled,
                                uint64_t *fuel_budget) {
    ThreadContext *value = thread_context(false);
    bool created = false;
    if (!value) {
        value = thread_context(true);
        if (!value)
            return false;
        created = true;
    }
    try {
        if (value->active) {
            ContextValue previous;
            previous.group = value->group;
            previous.id = value->id;
            previous.cancelled = value->cancelled;
            previous.fuel_budget = value->fuel_budget;
            value->previous.push_back(previous);
        }
    } catch (...) {
        if (created) {
#if defined(_WIN32)
            FlsSetValue(context_key, 0);
#else
            pthread_setspecific(context_key, 0);
#endif
            delete value;
        }
        return false;
    }
    value->group = group;
    value->id = id;
    value->cancelled = cancelled;
    value->fuel_budget = fuel_budget;
    value->active = true;
#if defined(_WIN32)
    if (id != 0)
        assign_processor_group(id - 1);
#endif
    return true;
}
void runtime_clear_thread_context() {
    ThreadContext *value = thread_context(false);
    if (!value)
        return;
    if (!value->previous.empty()) {
        ContextValue previous = value->previous.back();
        value->previous.pop_back();
        value->group = previous.group;
        value->id = previous.id;
        value->cancelled = previous.cancelled;
        value->fuel_budget = previous.fuel_budget;
        value->active = true;
        return;
    }
#if defined(_WIN32)
    if (FlsSetValue(context_key, 0))
        delete value;
    else {
        value->group = 0;
        value->id = 0;
        value->cancelled = 0;
        value->fuel_budget = 0;
        value->active = false;
    }
#else
    if (pthread_setspecific(context_key, 0) == 0)
        delete value;
    else {
        value->group = 0;
        value->id = 0;
        value->cancelled = 0;
        value->fuel_budget = 0;
        value->active = false;
    }
#endif
}
uint64_t runtime_monotonic_nanoseconds() {
#if defined(_WIN32)
    uint64_t ticks = static_cast<uint64_t>(GetTickCount64());
    uint64_t maximum = ~static_cast<uint64_t>(0);
    return ticks > maximum / 1000000 ? maximum : ticks * 1000000;
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        throw std::runtime_error("could not read monotonic clock");
    uint64_t seconds = static_cast<uint64_t>(now.tv_sec);
    uint64_t maximum = ~static_cast<uint64_t>(0);
    if (seconds > maximum / 1000000000)
        return maximum;
    return seconds * 1000000000 + static_cast<uint64_t>(now.tv_nsec);
#endif
}
void *runtime_current_thread_group() {
    ThreadContext *value = thread_context(false);
    return value && value->active ? value->group : 0;
}
uint32_t runtime_current_thread_id() {
    ThreadContext *value = thread_context(false);
    return value && value->active ? value->id : 0;
}
RuntimeCancellation *runtime_current_cancel_flag() {
    ThreadContext *value = thread_context(false);
    return value && value->active ? value->cancelled : 0;
}
uint64_t *runtime_current_fuel_budget() {
    ThreadContext *value = thread_context(false);
    return value && value->active ? value->fuel_budget : 0;
}
}
