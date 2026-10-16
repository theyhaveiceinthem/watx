#include <limits>
#include <stdexcept>
#include <stdint.h>
#include <watx/jit/arm64/platform/emitter.h>

namespace watx {
namespace jit_platform {
namespace {
void immediate(std::vector<uint32_t> &w, unsigned r, uint64_t v) {
    w.push_back(0xd2800000U | (static_cast<uint32_t>(v & 65535) << 5) | r);
    for (unsigned shift = 16; shift < 64; shift += 16)
        if ((v >> shift) & 65535)
            w.push_back(0xf2800000U | ((shift / 16) << 21) |
                        (static_cast<uint32_t>((v >> shift) & 65535) << 5) | r);
}
void load(std::vector<uint32_t> &w, unsigned r, unsigned base, int offset) {
    w.push_back(0xf8400000U | ((static_cast<uint32_t>(offset) & 511U) << 12) | (base << 5) | r);
}
void store(std::vector<uint32_t> &w, unsigned r, unsigned base, int offset) {
    w.push_back(0xf8000000U | ((static_cast<uint32_t>(offset) & 511U) << 12) | (base << 5) | r);
}
void type(std::vector<uint32_t> &w, ValueType t, int offset) {
    immediate(w, 17, static_cast<uint32_t>(t));
    w.push_back(0xb8000000U | ((static_cast<uint32_t>(offset) & 511U) << 12) | (23U << 5) | 17U);
}
void branch(std::vector<uint32_t> &w, std::size_t at, std::size_t to) {
    const int64_t delta = static_cast<int64_t>(to) - static_cast<int64_t>(at);
    if (delta < -33554432 || delta > 33554431)
        throw std::runtime_error("ARM64 native branch is out of range");
    w[at] = 0x14000000U | (static_cast<uint32_t>(delta) & 0x03ffffffU);
}
bool native_instruction(std::vector<uint32_t> &w, const Instruction &in) {
    const unsigned op = in.opcode;
    if (op == 0x01)
        return true;
    if (op == 0x1a) {
        w.push_back(0xd10042f7U);
        return true;
    }
    if (op >= 0x41 && op <= 0x44) {
        uint64_t bits = 0;
        for (std::size_t i = 0; i < in.bytes.size(); ++i)
            bits |= static_cast<uint64_t>(in.bytes[i]) << (i * 8);
        immediate(w, 9, bits);
        type(w,
             op == 0x41   ? TYPE_I32
             : op == 0x42 ? TYPE_I64
             : op == 0x43 ? TYPE_F32
                          : TYPE_F64,
             0);
        store(w, 9, 23, 8);
        w.push_back(0x910042f7U);
        return true;
    }
    if (op >= 0x20 && op <= 0x22) {
        immediate(w, 16, static_cast<uint64_t>(in.immediate) * 16);
        w.push_back(0x8b1002d0U);
        if (op == 0x20) {
            load(w, 9, 16, 0);
            load(w, 10, 16, 8);
            store(w, 9, 23, 0);
            store(w, 10, 23, 8);
            w.push_back(0x910042f7U);
        } else {
            load(w, 9, 23, -16);
            load(w, 10, 23, -8);
            store(w, 9, 16, 0);
            store(w, 10, 16, 8);
            if (op == 0x21)
                w.push_back(0xd10042f7U);
        }
        return true;
    }
    if (op == 0x45 || op == 0x50) {
        load(w, 9, 23, -8);
        w.push_back((op == 0x50 ? 0xf100013fU : 0x7100013fU));
        w.push_back(0x1a9f17e9U);
        store(w, 9, 23, -8);
        type(w, TYPE_I32, -16);
        return true;
    }
    if (op == 0x9f || op == 0xb7 || op == 0xb8 || op == 0xa7 || op == 0xac || op == 0xad) {
        load(w, 9, 23, -8);
        if (op == 0x9f) {
            w.push_back(0x9e670120U);
            w.push_back(0x1e61c000U);
            w.push_back(0x9e660009U);
        } else if (op == 0xb7 || op == 0xb8) {
            w.push_back((op == 0xb7 ? 0x1e620120U : 0x1e630120U));
            w.push_back(0x9e660009U);
            type(w, TYPE_F64, -16);
        } else {
            w.push_back(op == 0xac ? 0x93407d29U : 0x2a0903e9U);
            type(w, op == 0xa7 ? TYPE_I32 : TYPE_I64, -16);
        }
        store(w, 9, 23, -8);
        return true;
    }
    const bool compare32 = op >= 0x46 && op <= 0x4f;
    const bool compare64 = op >= 0x51 && op <= 0x5a;
    const bool integer32 = (op >= 0x6a && op <= 0x6c) || (op >= 0x71 && op <= 0x78);
    const bool integer64 = (op >= 0x7c && op <= 0x7e) || (op >= 0x83 && op <= 0x8a);
    const bool floating = op >= 0xa0 && op <= 0xa3;
    if (!compare32 && !compare64 && !integer32 && !integer64 && !floating)
        return false;
    w.push_back(0xd10042f7U);
    load(w, 10, 23, 8);
    load(w, 9, 23, -8);
    if (compare32 || compare64) {
        static const unsigned conditions[] = {0, 1, 11, 3, 12, 8, 13, 9, 10, 2};
        const unsigned condition = conditions[op - (compare64 ? 0x51 : 0x46)];
        w.push_back((compare64 ? 0xeb0a013fU : 0x6b0a013fU));
        w.push_back(0x1a9f07e9U | ((condition ^ 1U) << 12));
        type(w, TYPE_I32, -16);
    } else if (floating) {
        w.push_back(0x9e670120U);
        w.push_back(0x9e670141U);
        static const uint32_t operations[] = {0x1e612800U, 0x1e613800U, 0x1e610800U, 0x1e611800U};
        w.push_back(operations[op - 0xa0]);
        w.push_back(0x9e660009U);
    } else {
        const unsigned narrow = integer64 ? op - 0x12 : op;
        uint32_t instruction = 0;
        switch (narrow) {
        case 0x6a:
            instruction = 0x0b0a0129U;
            break;
        case 0x6b:
            instruction = 0x4b0a0129U;
            break;
        case 0x6c:
            instruction = 0x1b0a7d29U;
            break;
        case 0x71:
            instruction = 0x0a0a0129U;
            break;
        case 0x72:
            instruction = 0x2a0a0129U;
            break;
        case 0x73:
            instruction = 0x4a0a0129U;
            break;
        case 0x74:
            instruction = 0x1aca2129U;
            break;
        case 0x75:
            instruction = 0x1aca2929U;
            break;
        case 0x76:
            instruction = 0x1aca2529U;
            break;
        case 0x77:
            w.push_back(0x4b0a03eaU);
            instruction = 0x1aca2d29U;
            break;
        case 0x78:
            instruction = 0x1aca2d29U;
            break;
        }
        w.push_back(instruction | (integer64 ? 0x80000000U : 0));
    }
    store(w, 9, 23, -8);
    return true;
}
}

void emit_fast_arm64(const std::vector<Instruction> &instructions, StepFunction step,
                     std::vector<unsigned char> &out) {
    std::vector<uint32_t> w;
    std::vector<std::size_t> blocks, dispatch_branches;
    w.push_back(0xa9bf7bfdU);
    w.push_back(0x910003fdU);
    w.push_back(0xa9bf53f3U);
    w.push_back(0xa9bf5bf5U);
    w.push_back(0xa9bf63f7U);
    w.push_back(0xaa0003f3U);
    w.push_back(0xaa0103f4U);
    load(w, 21, 20, 0);
    load(w, 22, 20, 16);
    load(w, 9, 20, 8);
    w.push_back(0x8b0912b7U);
    immediate(w, 24, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(step)));
    for (std::size_t i = 0; i < instructions.size(); ++i) {
        blocks.push_back(w.size());
        if (native_instruction(w, instructions[i]))
            continue;
        w.push_back(0xcb1502e9U);
        w.push_back(0xd344fd29U);
        store(w, 9, 20, 8);
        w.push_back(0xaa1303e0U);
        immediate(w, 1, i);
        w.push_back(0xd63f0300U);
        load(w, 21, 20, 0);
        load(w, 9, 20, 8);
        w.push_back(0x8b0912b7U);
        dispatch_branches.push_back(w.size());
        w.push_back(0x14000000U);
    }
    const std::size_t dispatch = w.size();
    immediate(w, 17, instructions.size());
    w.push_back(0x6b11001fU);
    const std::size_t exit_branch = w.size();
    w.push_back(0x54000002U);
    const std::size_t adrp = w.size();
    w.push_back(0x90000011U);
    const std::size_t add = w.size();
    w.push_back(0x91000231U);
    w.push_back(0xb8a05a30U);
    w.push_back(0x8b100230U);
    w.push_back(0xd61f0200U);
    const std::size_t epilogue = w.size();
    w.push_back(0xa8c163f7U);
    w.push_back(0xa8c15bf5U);
    w.push_back(0xa8c153f3U);
    w.push_back(0xa8c17bfdU);
    w.push_back(0xd65f03c0U);
    const std::size_t table = w.size();
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const int64_t relative =
            (static_cast<int64_t>(blocks[i]) - static_cast<int64_t>(table)) * 4;
        if (relative < std::numeric_limits<int32_t>::min() ||
            relative > std::numeric_limits<int32_t>::max())
            throw std::runtime_error("ARM64 native table offset is out of range");
        w.push_back(static_cast<uint32_t>(relative));
    }
    for (std::size_t i = 0; i < dispatch_branches.size(); ++i)
        branch(w, dispatch_branches[i], dispatch);
    const int64_t page_delta =
        static_cast<int64_t>(table * 4 / 4096) - static_cast<int64_t>(adrp * 4 / 4096);
    if (page_delta < -1048576 || page_delta > 1048575)
        throw std::runtime_error("ARM64 native table page is out of range");
    const uint32_t page = static_cast<uint32_t>(page_delta) & 0x1fffffU;
    w[adrp] = 0x90000011U | ((page & 3U) << 29) | ((page >> 2) << 5);
    w[add] = 0x91000231U | (static_cast<uint32_t>(table * 4 & 4095U) << 10);
    const int64_t distance = static_cast<int64_t>(epilogue) - static_cast<int64_t>(exit_branch);
    if (distance < -262144 || distance > 262143)
        throw std::runtime_error("ARM64 native exit is out of range");
    w[exit_branch] = 0x54000002U | ((static_cast<uint32_t>(distance) & 0x7ffffU) << 5);
    for (std::size_t i = 0; i < w.size(); ++i)
        for (unsigned byte = 0; byte < 4; ++byte)
            out.push_back(static_cast<unsigned char>(w[i] >> (byte * 8)));
}
}
}
