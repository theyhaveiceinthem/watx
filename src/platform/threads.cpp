#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <watx/platform/threads.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

namespace watx {
unsigned hardware_threads() {
#if defined(_WIN32)
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    return count ? static_cast<unsigned>(count)
                 : (info.dwNumberOfProcessors ? static_cast<unsigned>(info.dwNumberOfProcessors)
                                              : 1U);
#else
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? static_cast<unsigned>(count) : 1U;
#endif
}
unsigned worker_count(unsigned requested) {
    unsigned result = requested ? requested : hardware_threads();
    return result ? result : 1U;
}
struct WorkState {
    ParallelJob job;
    void *context;
#if defined(_WIN32)
    CRITICAL_SECTION lock;
#else
    pthread_mutex_t lock;
#endif
    bool failed;
    char message[512];
    WorkState(ParallelJob j, void *c) : job(j), context(c), failed(false) {
        message[0] = 0;
#if defined(_WIN32)
        InitializeCriticalSection(&lock);
#else
        pthread_mutex_init(&lock, 0);
#endif
    }
    ~WorkState() {
#if defined(_WIN32)
        DeleteCriticalSection(&lock);
#else
        pthread_mutex_destroy(&lock);
#endif
    }
    void record(const char *text) {
#if defined(_WIN32)
        EnterCriticalSection(&lock);
#else
        pthread_mutex_lock(&lock);
#endif
        if (!failed) {
            failed = true;
            std::strncpy(message, text ? text : "parallel worker failed", sizeof(message) - 1);
            message[sizeof(message) - 1] = 0;
        }
#if defined(_WIN32)
        LeaveCriticalSection(&lock);
#else
        pthread_mutex_unlock(&lock);
#endif
    }
};
struct WorkRange {
    WorkState *state;
    std::size_t begin, end;
    unsigned worker_index;
};
static void run_range(WorkRange *range) {
    try {
        for (std::size_t i = range->begin; i < range->end; ++i)
            range->state->job(range->state->context, i);
    } catch (const std::exception &error) {
        range->state->record(error.what());
    } catch (...) {
        range->state->record("unknown exception in parallel worker");
    }
}
#if defined(_WIN32)
static DWORD WINAPI worker_entry(LPVOID value) {
    WorkRange *range = static_cast<WorkRange *>(value);
    DWORD total = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    WORD groups = GetActiveProcessorGroupCount();
    if (total && groups > 1) {
        DWORD ordinal = range->worker_index % total;
        for (WORD group = 0; group < groups; ++group) {
            DWORD count = GetActiveProcessorCount(group);
            if (ordinal >= count) {
                ordinal -= count;
                continue;
            }
            GROUP_AFFINITY affinity;
            std::memset(&affinity, 0, sizeof(affinity));
            affinity.Group = group;
            affinity.Mask = count >= sizeof(KAFFINITY) * 8
                                ? ~static_cast<KAFFINITY>(0)
                                : (static_cast<KAFFINITY>(1) << count) - 1;
            if (!SetThreadGroupAffinity(GetCurrentThread(), &affinity, 0)) {
                range->state->record("could not assign worker to a processor group");
                return 0;
            }
            break;
        }
    }
    run_range(range);
    return 0;
}
#else
static void *worker_entry(void *value) {
    run_range(static_cast<WorkRange *>(value));
    return 0;
}
#endif
void parallel_for(std::size_t count, unsigned workers, ParallelJob job, void *context) {
    if (!job)
        throw std::invalid_argument("parallel_for requires a job");
    if (count == 0)
        return;
    unsigned amount = worker_count(workers);
    if (count < static_cast<std::size_t>(amount))
        amount = static_cast<unsigned>(count);
    if (amount == 0)
        amount = 1;
    if (amount == 1) {
        for (std::size_t i = 0; i < count; ++i)
            job(context, i);
        return;
    }
    WorkState state(job, context);
    std::vector<WorkRange> ranges(amount);
#if defined(_WIN32)
    std::vector<HANDLE> handles(amount, static_cast<HANDLE>(0));
#else
    std::vector<pthread_t> handles(amount);
    std::vector<bool> started(amount, false);
#endif
    std::size_t base = count / amount;
    std::size_t extra = count % amount;
    std::size_t offset = 0;
    for (unsigned i = 0; i < amount; ++i) {
        std::size_t length = base + (i < extra ? 1 : 0);
        ranges[i].state = &state;
        ranges[i].begin = offset;
        ranges[i].end = offset + length;
        ranges[i].worker_index = i;
        offset += length;
    }
    for (unsigned i = 0; i < amount; ++i) {
#if defined(_WIN32)
        handles[i] = CreateThread(0, 0, worker_entry, &ranges[i], 0, 0);
        if (!handles[i])
            run_range(&ranges[i]);
#else
        if (pthread_create(&handles[i], 0, worker_entry, &ranges[i]) == 0)
            started[i] = true;
        else
            run_range(&ranges[i]);
#endif
    }
#if defined(_WIN32)
    for (unsigned i = 0; i < amount; ++i) {
        if (handles[i]) {
            WaitForSingleObject(handles[i], INFINITE);
            CloseHandle(handles[i]);
        }
    }
#else
    for (unsigned i = 0; i < amount; ++i)
        if (started[i])
            pthread_join(handles[i], 0);
#endif
    if (state.failed)
        throw std::runtime_error(state.message);
}
}
