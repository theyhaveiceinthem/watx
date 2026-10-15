#include <cstring>
#include <stdexcept>
#include <watx/jit/platform/code_memory.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace watx {
namespace jit_platform {
static std::size_t page_round(std::size_t size) {
#if defined(_WIN32)
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    std::size_t page_size = static_cast<std::size_t>(info.dwPageSize);
#else
    long page = sysconf(_SC_PAGESIZE);
    std::size_t page_size = page > 0 ? static_cast<std::size_t>(page) : 4096U;
#endif
    std::size_t remainder = size % page_size;
    std::size_t padding = remainder ? page_size - remainder : 0;
    const std::size_t maximum = static_cast<std::size_t>(-1);
    if (size > maximum - padding)
        throw std::length_error("JIT code allocation size overflow");
    return size + padding;
}
void *publish_code(const unsigned char *bytes, std::size_t size) {
    if (!bytes || size == 0)
        throw std::runtime_error("cannot publish empty native code");
    const std::size_t allocation_size = page_round(size);
#if defined(_WIN32)
    void *memory = VirtualAlloc(0, allocation_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!memory)
        throw std::runtime_error("VirtualAlloc failed for JIT code");
    std::memcpy(memory, bytes, size);
    DWORD old_protection = 0;
    if (!VirtualProtect(memory, allocation_size, PAGE_EXECUTE_READ, &old_protection)) {
        VirtualFree(memory, 0, MEM_RELEASE);
        throw std::runtime_error("VirtualProtect failed for JIT code");
    }
    if (!FlushInstructionCache(GetCurrentProcess(), memory, size)) {
        VirtualFree(memory, 0, MEM_RELEASE);
        throw std::runtime_error("FlushInstructionCache failed for JIT code");
    }
    return memory;
#elif defined(__APPLE__)
#ifndef MAP_JIT
#error Apple JIT requires MAP_JIT support from the SDK
#endif
    void *memory = mmap(0, allocation_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    if (memory == MAP_FAILED)
        throw std::runtime_error("mmap MAP_JIT failed");
    pthread_jit_write_protect_np(0);
    std::memcpy(memory, bytes, size);
    __builtin___clear_cache(static_cast<char *>(memory), static_cast<char *>(memory) + size);
    pthread_jit_write_protect_np(1);
    return memory;
#else
    void *memory = mmap(0, allocation_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (memory == MAP_FAILED)
        throw std::runtime_error("mmap failed for JIT code");
    std::memcpy(memory, bytes, size);
    __builtin___clear_cache(static_cast<char *>(memory), static_cast<char *>(memory) + size);
    if (mprotect(memory, allocation_size, PROT_READ | PROT_EXEC) != 0) {
        munmap(memory, allocation_size);
        throw std::runtime_error("mprotect failed for JIT code");
    }
    return memory;
#endif
}
void release_code(void *memory, std::size_t size) {
    if (!memory)
        return;
    const std::size_t allocation_size = page_round(size);
#if defined(_WIN32)
    VirtualFree(memory, 0, MEM_RELEASE);
#else
    munmap(memory, allocation_size);
#endif
}
}
}
