#include <limits>
#include <stdexcept>
#include <stdint.h>
#include <watx/jit/arm64/platform/emitter.h>

namespace watx {
namespace jit_platform {
static void emit32(std::vector<unsigned char> &out, uint32_t value) {
    out.push_back(static_cast<unsigned char>(value));
    out.push_back(static_cast<unsigned char>(value >> 8));
    out.push_back(static_cast<unsigned char>(value >> 16));
    out.push_back(static_cast<unsigned char>(value >> 24));
}
static uint32_t movz_w(unsigned reg, uint16_t value) {
    return 0x52800000U | (static_cast<uint32_t>(value) << 5) | reg;
}
static uint32_t movk_w(unsigned reg, uint16_t value) {
    return 0x72a00000U | (static_cast<uint32_t>(value) << 5) | reg;
}
static uint32_t movz_x(unsigned reg, uint16_t value, unsigned shift) {
    return (shift == 0    ? 0xd2800000U
            : shift == 16 ? 0xf2a00000U
            : shift == 32 ? 0xf2c00000U
                          : 0xf2e00000U) |
           (static_cast<uint32_t>(value) << 5) | reg;
}
static void patch_branch(std::vector<uint32_t> &words, std::size_t from, std::size_t to) {
    int64_t delta = static_cast<int64_t>(to) - static_cast<int64_t>(from);
    if (delta < -33554432 || delta > 33554431)
        throw std::runtime_error("ARM64 JIT branch is out of range");
    words[from] = (words[from] & 0xfc000000U) | (static_cast<uint32_t>(delta) & 0x03ffffffU);
}
void emit_arm64(const std::vector<unsigned> &instructions, StepFunction step,
                std::vector<unsigned char> &out) {
    std::vector<uint32_t> words;
    std::vector<std::size_t> blocks, dispatch_branches;
    const uint64_t helper = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(step));
    std::size_t i;
    if (instructions.empty())
        throw std::runtime_error("cannot emit an empty function");
    words.push_back(0xa9bf7bfdU);
    words.push_back(0x910003fdU);
    words.push_back(0xa9bf7ff3U);
    words.push_back(0xaa0003f3U);
    for (i = 0; i < instructions.size(); ++i) {
        blocks.push_back(words.size());
        words.push_back(0xaa1303e0U);
        words.push_back(movz_w(1, static_cast<uint16_t>(i)));
        if (i > 0xffffU)
            words.push_back(movk_w(1, static_cast<uint16_t>(i >> 16)));
        words.push_back(movz_x(16, static_cast<uint16_t>(helper), 0));
        words.push_back(movz_x(16, static_cast<uint16_t>(helper >> 16), 16));
        words.push_back(movz_x(16, static_cast<uint16_t>(helper >> 32), 32));
        words.push_back(movz_x(16, static_cast<uint16_t>(helper >> 48), 48));
        words.push_back(0xd63f0200U);
        dispatch_branches.push_back(words.size());
        words.push_back(0x14000000U);
    }
    std::size_t dispatch = words.size();
    words.push_back(movz_w(17, static_cast<uint16_t>(instructions.size())));
    if (instructions.size() > 0xffffU)
        words.push_back(movk_w(17, static_cast<uint16_t>(instructions.size() >> 16)));
    words.push_back(0x6b11001fU);
    std::size_t out_of_range = words.size();
    words.push_back(0x54000002U);
    const std::size_t adrp = words.size();
    words.push_back(0x90000011U);
    const std::size_t table_add = words.size();
    words.push_back(0x91000231U);
    words.push_back(0xb8a05800U | (17U << 5) | 16U);
    words.push_back(0x8b100230U);
    words.push_back(0xd61f0200U);
    std::size_t epilogue = words.size();
    words.push_back(0xa8c17ff3U);
    words.push_back(0xa8c17bfdU);
    words.push_back(0xd65f03c0U);
    const std::size_t table = words.size();
    for (i = 0; i < instructions.size(); ++i) {
        int64_t relative = static_cast<int64_t>(blocks[i] * 4) - static_cast<int64_t>(table * 4);
        if (relative < std::numeric_limits<int32_t>::min() ||
            relative > std::numeric_limits<int32_t>::max())
            throw std::runtime_error("ARM64 JIT jump table offset is out of range");
        words.push_back(static_cast<uint32_t>(static_cast<int32_t>(relative)));
    }
    for (i = 0; i < dispatch_branches.size(); ++i)
        patch_branch(words, dispatch_branches[i], dispatch);
    int64_t page_delta =
        static_cast<int64_t>((table * 4) / 4096) - static_cast<int64_t>((adrp * 4) / 4096);
    if (page_delta < -1048576 || page_delta > 1048575)
        throw std::runtime_error("ARM64 JIT jump table is out of range");
    uint32_t page_imm = static_cast<uint32_t>(page_delta) & 0x1fffffU;
    words[adrp] = 0x90000011U | ((page_imm & 3U) << 29) | ((page_imm >> 2) << 5);
    words[table_add] = 0x91000231U | (static_cast<uint32_t>((table * 4) & 0xfffU) << 10);
    int64_t cond_delta = static_cast<int64_t>(epilogue) - static_cast<int64_t>(out_of_range);
    words[out_of_range] = 0x54000002U | ((static_cast<uint32_t>(cond_delta) & 0x7ffffU) << 5);
    for (i = 0; i < words.size(); ++i)
        emit32(out, words[i]);
}
}
}
