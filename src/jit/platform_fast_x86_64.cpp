#include <limits>
#include <stdexcept>
#include <stdint.h>
#include <watx/jit/x86_64/platform/emitter.h>

namespace watx {
namespace jit_platform {
namespace {
void bytes(std::vector<unsigned char> &out, const unsigned char *values, std::size_t count) {
    out.insert(out.end(), values, values + count);
}
void b(std::vector<unsigned char> &out, unsigned a) {
    out.push_back(static_cast<unsigned char>(a));
}
void word(std::vector<unsigned char> &out, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        b(out, value >> (i * 8));
}
void quad(std::vector<unsigned char> &out, uint64_t value) {
    word(out, static_cast<uint32_t>(value));
    word(out, static_cast<uint32_t>(value >> 32));
}
int32_t relative(std::size_t target, std::size_t base) {
    const int64_t distance = static_cast<int64_t>(target) - static_cast<int64_t>(base);
    if (distance < std::numeric_limits<int32_t>::min() ||
        distance > std::numeric_limits<int32_t>::max())
        throw std::runtime_error("x86_64 native branch is out of range");
    return static_cast<int32_t>(distance);
}
void patch(std::vector<unsigned char> &out, std::size_t at, std::size_t target) {
    const uint32_t value = static_cast<uint32_t>(relative(target, at + 4));
    for (unsigned i = 0; i < 4; ++i)
        out[at + i] = static_cast<unsigned char>(value >> (i * 8));
}
void type(std::vector<unsigned char> &out, ValueType t, unsigned offset) {
    b(out, 0xc7);
    b(out, 0x43);
    b(out, offset);
    word(out, static_cast<uint32_t>(t));
}
bool native_instruction(std::vector<unsigned char> &out, const Instruction &in) {
    const unsigned op = in.opcode;
    if (op == 0x01)
        return true;
    if (op == 0x1a) {
        const unsigned char code[] = {0x48, 0x83, 0xeb, 0x10};
        bytes(out, code, sizeof(code));
        return true;
    }
    if (op >= 0x41 && op <= 0x44) {
        uint64_t value = 0;
        for (std::size_t i = 0; i < in.bytes.size(); ++i)
            value |= static_cast<uint64_t>(in.bytes[i]) << (i * 8);
        b(out, 0x48);
        b(out, 0xb8);
        quad(out, value);
        type(out,
             op == 0x41   ? TYPE_I32
             : op == 0x42 ? TYPE_I64
             : op == 0x43 ? TYPE_F32
                          : TYPE_F64,
             0);
        const unsigned char code[] = {0x48, 0x89, 0x43, 0x08, 0x48, 0x83, 0xc3, 0x10};
        bytes(out, code, sizeof(code));
        return true;
    }
    if (op >= 0x20 && op <= 0x22) {
        if (in.immediate > 0x07ffffffU)
            return false;
        b(out, 0x49);
        b(out, 0x8d);
        b(out, 0x97);
        word(out, in.immediate * 16U);
        if (op == 0x20) {
            const unsigned char code[] = {0x48, 0x8b, 0x02, 0x48, 0x8b, 0x4a, 0x08, 0x48, 0x89,
                                          0x03, 0x48, 0x89, 0x4b, 0x08, 0x48, 0x83, 0xc3, 0x10};
            bytes(out, code, sizeof(code));
        } else {
            const unsigned char code[] = {0x48, 0x8b, 0x43, 0xf0, 0x48, 0x8b, 0x4b, 0xf8,
                                          0x48, 0x89, 0x02, 0x48, 0x89, 0x4a, 0x08};
            bytes(out, code, sizeof(code));
            if (op == 0x21) {
                const unsigned char pop[] = {0x48, 0x83, 0xeb, 0x10};
                bytes(out, pop, sizeof(pop));
            }
        }
        return true;
    }
    if (op == 0x45 || op == 0x50 || op == 0x9f || op == 0xb7 || op == 0xb8 || op == 0xa7 ||
        op == 0xac || op == 0xad) {
        if (op == 0x50 || op == 0x9f)
            b(out, 0x48);
        b(out, 0x8b);
        b(out, 0x43);
        b(out, 0xf8);
        if (op == 0x45 || op == 0x50) {
            if (op == 0x50)
                b(out, 0x48);
            b(out, 0x85);
            b(out, 0xc0);
            const unsigned char equal[] = {0x0f, 0x94, 0xc0, 0x0f, 0xb6, 0xc0};
            bytes(out, equal, sizeof(equal));
            type(out, TYPE_I32, 0xf0);
        } else if (op == 0x9f) {
            const unsigned char square_root[] = {0x66, 0x48, 0x0f, 0x6e, 0xc0, 0xf2, 0x0f,
                                                 0x51, 0xc0, 0x66, 0x48, 0x0f, 0x7e, 0xc0};
            bytes(out, square_root, sizeof(square_root));
        } else if (op == 0xb7 || op == 0xb8) {
            if (op == 0xb7) {
                b(out, 0x48);
                b(out, 0x63);
                b(out, 0xc0);
            }
            const unsigned char convert[] = {0xf2, 0x48, 0x0f, 0x2a, 0xc0,
                                             0x66, 0x48, 0x0f, 0x7e, 0xc0};
            bytes(out, convert, sizeof(convert));
            type(out, TYPE_F64, 0xf0);
        } else {
            if (op == 0xac) {
                b(out, 0x48);
                b(out, 0x63);
                b(out, 0xc0);
            }
            type(out, op == 0xa7 ? TYPE_I32 : TYPE_I64, 0xf0);
        }
        const unsigned char save[] = {0x48, 0x89, 0x43, 0xf8};
        bytes(out, save, sizeof(save));
        return true;
    }
    const bool compare32 = op >= 0x46 && op <= 0x4f;
    const bool compare64 = op >= 0x51 && op <= 0x5a;
    const bool integer32 = (op >= 0x6a && op <= 0x6c) || (op >= 0x71 && op <= 0x78);
    const bool integer64 = (op >= 0x7c && op <= 0x7e) || (op >= 0x83 && op <= 0x8a);
    const bool floating = op >= 0xa0 && op <= 0xa3;
    if (!compare32 && !compare64 && !integer32 && !integer64 && !floating)
        return false;
    const unsigned char pop[] = {0x48, 0x83, 0xeb, 0x10};
    bytes(out, pop, sizeof(pop));
    const bool wide = compare64 || integer64 || floating;
    if (wide)
        b(out, 0x48);
    b(out, 0x8b);
    b(out, 0x4b);
    b(out, 0x08);
    if (wide)
        b(out, 0x48);
    b(out, 0x8b);
    b(out, 0x43);
    b(out, 0xf8);
    if (compare32 || compare64) {
        static const unsigned conditions[] = {0x94, 0x95, 0x9c, 0x92, 0x9f,
                                              0x97, 0x9e, 0x96, 0x9d, 0x93};
        if (wide)
            b(out, 0x48);
        b(out, 0x39);
        b(out, 0xc8);
        b(out, 0x0f);
        b(out, conditions[op - (compare64 ? 0x51 : 0x46)]);
        b(out, 0xc0);
        b(out, 0x0f);
        b(out, 0xb6);
        b(out, 0xc0);
        type(out, TYPE_I32, 0xf0);
    } else if (floating) {
        const unsigned char move[] = {0x66, 0x48, 0x0f, 0x6e, 0xc0, 0x66, 0x48, 0x0f, 0x6e, 0xc9};
        bytes(out, move, sizeof(move));
        static const unsigned operations[] = {0x58, 0x5c, 0x59, 0x5e};
        b(out, 0xf2);
        b(out, 0x0f);
        b(out, operations[op - 0xa0]);
        b(out, 0xc1);
        const unsigned char save_float[] = {0x66, 0x48, 0x0f, 0x7e, 0xc0};
        bytes(out, save_float, sizeof(save_float));
    } else {
        const unsigned narrow = integer64 ? op - 0x12 : op;
        if (wide)
            b(out, 0x48);
        switch (narrow) {
        case 0x6a:
            b(out, 0x01);
            b(out, 0xc8);
            break;
        case 0x6b:
            b(out, 0x29);
            b(out, 0xc8);
            break;
        case 0x6c:
            b(out, 0x0f);
            b(out, 0xaf);
            b(out, 0xc1);
            break;
        case 0x71:
            b(out, 0x21);
            b(out, 0xc8);
            break;
        case 0x72:
            b(out, 0x09);
            b(out, 0xc8);
            break;
        case 0x73:
            b(out, 0x31);
            b(out, 0xc8);
            break;
        case 0x74:
            b(out, 0xd3);
            b(out, 0xe0);
            break;
        case 0x75:
            b(out, 0xd3);
            b(out, 0xf8);
            break;
        case 0x76:
            b(out, 0xd3);
            b(out, 0xe8);
            break;
        case 0x77:
            b(out, 0xd3);
            b(out, 0xc0);
            break;
        case 0x78:
            b(out, 0xd3);
            b(out, 0xc8);
            break;
        }
    }
    const unsigned char save[] = {0x48, 0x89, 0x43, 0xf8};
    bytes(out, save, sizeof(save));
    return true;
}
}

void emit_fast_x86_64(const std::vector<Instruction> &instructions, StepFunction step,
                      std::vector<unsigned char> &out) {
    std::vector<std::size_t> blocks, dispatches;
    const unsigned char save[] = {0x53, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
    bytes(out, save, sizeof(save));
#ifdef _WIN32
    const unsigned char arguments[] = {0x49, 0x89, 0xcc, 0x49, 0x89, 0xd5, 0x48, 0x83, 0xec, 0x28};
#else
    const unsigned char arguments[] = {0x49, 0x89, 0xfc, 0x49, 0x89, 0xf5, 0x48, 0x83, 0xec, 0x08};
#endif
    bytes(out, arguments, sizeof(arguments));
    const unsigned char initialize[] = {0x4d, 0x8b, 0x75, 0x00, 0x4d, 0x8b, 0x7d, 0x10,
                                        0x49, 0x8b, 0x4d, 0x08, 0x48, 0xc1, 0xe1, 0x04,
                                        0x49, 0x8d, 0x1c, 0x0e, 0x48, 0xbd};
    bytes(out, initialize, sizeof(initialize));
    quad(out, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(step)));
    for (std::size_t i = 0; i < instructions.size(); ++i) {
        blocks.push_back(out.size());
        if (native_instruction(out, instructions[i]))
            continue;
        const unsigned char sync[] = {0x48, 0x89, 0xd9, 0x4c, 0x29, 0xf1, 0x48,
                                      0xc1, 0xe9, 0x04, 0x49, 0x89, 0x4d, 0x08};
        bytes(out, sync, sizeof(sync));
#ifdef _WIN32
        b(out, 0x4c);
        b(out, 0x89);
        b(out, 0xe1);
        b(out, 0xba);
#else
        b(out, 0x4c);
        b(out, 0x89);
        b(out, 0xe7);
        b(out, 0xbe);
#endif
        word(out, static_cast<uint32_t>(i));
        b(out, 0xff);
        b(out, 0xd5);
        const unsigned char reload[] = {0x4d, 0x8b, 0x75, 0x00, 0x49, 0x8b, 0x4d, 0x08,
                                        0x48, 0xc1, 0xe1, 0x04, 0x49, 0x8d, 0x1c, 0x0e};
        bytes(out, reload, sizeof(reload));
        b(out, 0xe9);
        dispatches.push_back(out.size());
        word(out, 0);
    }
    const std::size_t dispatch = out.size();
    b(out, 0x3d);
    word(out, static_cast<uint32_t>(instructions.size()));
    b(out, 0x0f);
    b(out, 0x83);
    const std::size_t exit_branch = out.size();
    word(out, 0);
    b(out, 0x4c);
    b(out, 0x8d);
    b(out, 0x1d);
    const std::size_t table_address = out.size();
    word(out, 0);
    const unsigned char jump[] = {0x4d, 0x63, 0x14, 0x83, 0x4d, 0x01, 0xda, 0x41, 0xff, 0xe2};
    bytes(out, jump, sizeof(jump));
    const std::size_t epilogue = out.size();
    b(out, 0x48);
    b(out, 0x83);
    b(out, 0xc4);
#ifdef _WIN32
    b(out, 0x28);
#else
    b(out, 0x08);
#endif
    const unsigned char restore[] = {0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d,
                                     0x41, 0x5c, 0x5d, 0x5b, 0xc3};
    bytes(out, restore, sizeof(restore));
    const std::size_t table = out.size();
    for (std::size_t i = 0; i < blocks.size(); ++i)
        word(out, static_cast<uint32_t>(relative(blocks[i], table)));
    for (std::size_t i = 0; i < dispatches.size(); ++i)
        patch(out, dispatches[i], dispatch);
    patch(out, exit_branch, epilogue);
    patch(out, table_address, table);
}
}
}
