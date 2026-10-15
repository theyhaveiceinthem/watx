#ifndef WATX_PLATFORM_THREADS_H
#define WATX_PLATFORM_THREADS_H
#include <cstddef>
#include <watx/export.h>
namespace watx {
WATX_API unsigned hardware_threads();
WATX_API unsigned worker_count(unsigned requested);
typedef void (*ParallelJob)(void *, std::size_t);
WATX_API void parallel_for(std::size_t count, unsigned workers, ParallelJob job, void *context);
}
#endif
