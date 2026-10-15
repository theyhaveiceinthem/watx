#include <limits>
#include <stdexcept>
#include <stdint.h>
#include <watx/jit/x86_64/platform/emitter.h>

namespace watx {
namespace jit_platform {
static void byte(std::vector<unsigned char> &out, unsigned char value) {
    out.push_back(value);
}
static void word32(std::vector<unsigned char> &out, uint32_t value) {
    byte(out, static_cast<unsigned char>(value));
    byte(out, static_cast<unsigned char>(value >> 8));
    byte(out, static_cast<unsigned char>(value >> 16));
    byte(out, static_cast<unsigned char>(value >> 24));
}
static void word64(std::vector<unsigned char> &out, uint64_t value) {
    word32(out, static_cast<uint32_t>(value));
    word32(out, static_cast<uint32_t>(value >> 32));
}
static int32_t signed_offset(std::size_t target, std::size_t base) {
    if (target >= base) {
        std::size_t distance = target - base;
        if (distance > static_cast<std::size_t>(std::numeric_limits<int32_t>::max()))
            throw std::runtime_error("x86_64 JIT relative offset is out of range");
        return static_cast<int32_t>(distance);
    }
    std::size_t distance = base - target;
    std::size_t negative_limit = static_cast<std::size_t>(std::numeric_limits<int32_t>::max()) + 1U;
    if (distance > negative_limit)
        throw std::runtime_error("x86_64 JIT relative offset is out of range");
    if (distance == negative_limit)
        return std::numeric_limits<int32_t>::min();
    return -static_cast<int32_t>(distance);
}
static void patch32(std::vector<unsigned char> &out, std::size_t at, std::size_t target) {
    uint32_t value = static_cast<uint32_t>(signed_offset(target, at + 4));
    out[at] = static_cast<unsigned char>(value);
    out[at + 1] = static_cast<unsigned char>(value >> 8);
    out[at + 2] = static_cast<unsigned char>(value >> 16);
    out[at + 3] = static_cast<unsigned char>(value >> 24);
}
void emit_x86_64(const std::vector<unsigned> &instructions, StepFunction step,
                 std::vector<unsigned char> &out) {
    std::vector<std::size_t> blocks;
    std::vector<std::size_t> dispatch_jumps;
    std::size_t i;
    if (instructions.empty())
        throw std::runtime_error("cannot emit an empty function");
    byte(out, 0x53);
#if defined(_WIN32)
    byte(out, 0x48);
    byte(out, 0x89);
    byte(out, 0xcb);
    byte(out, 0x48);
    byte(out, 0x83);
    byte(out, 0xec);
    byte(out, 0x20);
#else
    byte(out, 0x48);
    byte(out, 0x89);
    byte(out, 0xfb);
#endif
    for (i = 0; i < instructions.size(); ++i) {
        blocks.push_back(out.size());
#if defined(_WIN32)
        byte(out, 0x48);
        byte(out, 0x89);
        byte(out, 0xd9);
        byte(out, 0xba);
        word32(out, static_cast<uint32_t>(i));
#else
        byte(out, 0x48);
        byte(out, 0x89);
        byte(out, 0xdf);
        byte(out, 0xbe);
        word32(out, static_cast<uint32_t>(i));
#endif
        byte(out, 0x48);
        byte(out, 0xb8);
        word64(out, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(step)));
        byte(out, 0xff);
        byte(out, 0xd0);
        byte(out, 0xe9);
        dispatch_jumps.push_back(out.size());
        word32(out, 0);
    }
    std::size_t dispatch = out.size();
    byte(out, 0x3d);
    word32(out, static_cast<uint32_t>(instructions.size()));
    byte(out, 0x0f);
    byte(out, 0x83);
    std::size_t out_of_range = out.size();
    word32(out, 0);
    byte(out, 0x4c);
    byte(out, 0x8d);
    byte(out, 0x1d);
    std::size_t table_lea = out.size();
    word32(out, 0);
    byte(out, 0x4d);
    byte(out, 0x63);
    byte(out, 0x14);
    byte(out, 0x83);
    byte(out, 0x4d);
    byte(out, 0x01);
    byte(out, 0xda);
    byte(out, 0x41);
    byte(out, 0xff);
    byte(out, 0xe2);
    std::size_t epilogue = out.size();
#if defined(_WIN32)
    byte(out, 0x48);
    byte(out, 0x83);
    byte(out, 0xc4);
    byte(out, 0x20);
#endif
    byte(out, 0x5b);
    byte(out, 0xc3);
    std::size_t table = out.size();
    for (i = 0; i < instructions.size(); ++i)
        word32(out, static_cast<uint32_t>(signed_offset(blocks[i], table)));
    for (i = 0; i < dispatch_jumps.size(); ++i)
        patch32(out, dispatch_jumps[i], dispatch);
    patch32(out, out_of_range, epilogue);
    patch32(out, table_lea, table);
}
}
}
