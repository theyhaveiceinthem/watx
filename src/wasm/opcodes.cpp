#include <watx/wasm/opcodes.h>

namespace watx {
namespace {

struct OpcodeName {
    const char *name;
    unsigned char opcode;
};

struct ExtendedOpcodeName {
    const char *name;
    uint32_t opcode;
};

const OpcodeName numeric_opcodes[] = {{"i32.eqz", 0x45},
                                      {"i32.eq", 0x46},
                                      {"i32.ne", 0x47},
                                      {"i32.lt_s", 0x48},
                                      {"i32.lt_u", 0x49},
                                      {"i32.gt_s", 0x4a},
                                      {"i32.gt_u", 0x4b},
                                      {"i32.le_s", 0x4c},
                                      {"i32.le_u", 0x4d},
                                      {"i32.ge_s", 0x4e},
                                      {"i32.ge_u", 0x4f},
                                      {"i64.eqz", 0x50},
                                      {"i64.eq", 0x51},
                                      {"i64.ne", 0x52},
                                      {"i64.lt_s", 0x53},
                                      {"i64.lt_u", 0x54},
                                      {"i64.gt_s", 0x55},
                                      {"i64.gt_u", 0x56},
                                      {"i64.le_s", 0x57},
                                      {"i64.le_u", 0x58},
                                      {"i64.ge_s", 0x59},
                                      {"i64.ge_u", 0x5a},
                                      {"f32.eq", 0x5b},
                                      {"f32.ne", 0x5c},
                                      {"f32.lt", 0x5d},
                                      {"f32.gt", 0x5e},
                                      {"f32.le", 0x5f},
                                      {"f32.ge", 0x60},
                                      {"f64.eq", 0x61},
                                      {"f64.ne", 0x62},
                                      {"f64.lt", 0x63},
                                      {"f64.gt", 0x64},
                                      {"f64.le", 0x65},
                                      {"f64.ge", 0x66},
                                      {"i32.clz", 0x67},
                                      {"i32.ctz", 0x68},
                                      {"i32.popcnt", 0x69},
                                      {"i32.add", 0x6a},
                                      {"i32.sub", 0x6b},
                                      {"i32.mul", 0x6c},
                                      {"i32.div_s", 0x6d},
                                      {"i32.div_u", 0x6e},
                                      {"i32.rem_s", 0x6f},
                                      {"i32.rem_u", 0x70},
                                      {"i32.and", 0x71},
                                      {"i32.or", 0x72},
                                      {"i32.xor", 0x73},
                                      {"i32.shl", 0x74},
                                      {"i32.shr_s", 0x75},
                                      {"i32.shr_u", 0x76},
                                      {"i32.rotl", 0x77},
                                      {"i32.rotr", 0x78},
                                      {"i64.clz", 0x79},
                                      {"i64.ctz", 0x7a},
                                      {"i64.popcnt", 0x7b},
                                      {"i64.add", 0x7c},
                                      {"i64.sub", 0x7d},
                                      {"i64.mul", 0x7e},
                                      {"i64.div_s", 0x7f},
                                      {"i64.div_u", 0x80},
                                      {"i64.rem_s", 0x81},
                                      {"i64.rem_u", 0x82},
                                      {"i64.and", 0x83},
                                      {"i64.or", 0x84},
                                      {"i64.xor", 0x85},
                                      {"i64.shl", 0x86},
                                      {"i64.shr_s", 0x87},
                                      {"i64.shr_u", 0x88},
                                      {"i64.rotl", 0x89},
                                      {"i64.rotr", 0x8a},
                                      {"f32.abs", 0x8b},
                                      {"f32.neg", 0x8c},
                                      {"f32.ceil", 0x8d},
                                      {"f32.floor", 0x8e},
                                      {"f32.trunc", 0x8f},
                                      {"f32.nearest", 0x90},
                                      {"f32.sqrt", 0x91},
                                      {"f32.add", 0x92},
                                      {"f32.sub", 0x93},
                                      {"f32.mul", 0x94},
                                      {"f32.div", 0x95},
                                      {"f32.min", 0x96},
                                      {"f32.max", 0x97},
                                      {"f32.copysign", 0x98},
                                      {"f64.abs", 0x99},
                                      {"f64.neg", 0x9a},
                                      {"f64.ceil", 0x9b},
                                      {"f64.floor", 0x9c},
                                      {"f64.trunc", 0x9d},
                                      {"f64.nearest", 0x9e},
                                      {"f64.sqrt", 0x9f},
                                      {"f64.add", 0xa0},
                                      {"f64.sub", 0xa1},
                                      {"f64.mul", 0xa2},
                                      {"f64.div", 0xa3},
                                      {"f64.min", 0xa4},
                                      {"f64.max", 0xa5},
                                      {"f64.copysign", 0xa6},
                                      {"i32.wrap_i64", 0xa7},
                                      {"i32.trunc_f32_s", 0xa8},
                                      {"i32.trunc_f32_u", 0xa9},
                                      {"i32.trunc_f64_s", 0xaa},
                                      {"i32.trunc_f64_u", 0xab},
                                      {"i64.extend_i32_s", 0xac},
                                      {"i64.extend_i32_u", 0xad},
                                      {"i64.trunc_f32_s", 0xae},
                                      {"i64.trunc_f32_u", 0xaf},
                                      {"i64.trunc_f64_s", 0xb0},
                                      {"i64.trunc_f64_u", 0xb1},
                                      {"f32.convert_i32_s", 0xb2},
                                      {"f32.convert_i32_u", 0xb3},
                                      {"f32.convert_i64_s", 0xb4},
                                      {"f32.convert_i64_u", 0xb5},
                                      {"f32.demote_f64", 0xb6},
                                      {"f64.convert_i32_s", 0xb7},
                                      {"f64.convert_i32_u", 0xb8},
                                      {"f64.convert_i64_s", 0xb9},
                                      {"f64.convert_i64_u", 0xba},
                                      {"f64.promote_f32", 0xbb},
                                      {"i32.reinterpret_f32", 0xbc},
                                      {"i64.reinterpret_f64", 0xbd},
                                      {"f32.reinterpret_i32", 0xbe},
                                      {"f64.reinterpret_i64", 0xbf}};

const OpcodeName memory_opcodes[] = {
    {"i32.load", 0x28},     {"i64.load", 0x29},     {"f32.load", 0x2a},     {"f64.load", 0x2b},
    {"i32.load8_s", 0x2c},  {"i32.load8_u", 0x2d},  {"i32.load16_s", 0x2e}, {"i32.load16_u", 0x2f},
    {"i64.load8_s", 0x30},  {"i64.load8_u", 0x31},  {"i64.load16_s", 0x32}, {"i64.load16_u", 0x33},
    {"i64.load32_s", 0x34}, {"i64.load32_u", 0x35}, {"i32.store", 0x36},    {"i64.store", 0x37},
    {"f32.store", 0x38},    {"f64.store", 0x39},    {"i32.store8", 0x3a},   {"i32.store16", 0x3b},
    {"i64.store8", 0x3c},   {"i64.store16", 0x3d},  {"i64.store32", 0x3e}};

const ExtendedOpcodeName atomic_opcodes[] = {
    {"memory.atomic.notify", 0x00},      {"memory.atomic.wait32", 0x01},
    {"memory.atomic.wait64", 0x02},      {"atomic.fence", 0x03},
    {"i32.atomic.load", 0x10},           {"i64.atomic.load", 0x11},
    {"i32.atomic.load8_u", 0x12},        {"i32.atomic.load16_u", 0x13},
    {"i64.atomic.load8_u", 0x14},        {"i64.atomic.load16_u", 0x15},
    {"i64.atomic.load32_u", 0x16},       {"i32.atomic.store", 0x17},
    {"i64.atomic.store", 0x18},          {"i32.atomic.store8", 0x19},
    {"i32.atomic.store16", 0x1a},        {"i64.atomic.store8", 0x1b},
    {"i64.atomic.store16", 0x1c},        {"i64.atomic.store32", 0x1d},
    {"i32.atomic.rmw.add", 0x1e},        {"i64.atomic.rmw.add", 0x1f},
    {"i32.atomic.rmw8.add_u", 0x20},     {"i32.atomic.rmw16.add_u", 0x21},
    {"i64.atomic.rmw8.add_u", 0x22},     {"i64.atomic.rmw16.add_u", 0x23},
    {"i64.atomic.rmw32.add_u", 0x24},    {"i32.atomic.rmw.sub", 0x25},
    {"i64.atomic.rmw.sub", 0x26},        {"i32.atomic.rmw8.sub_u", 0x27},
    {"i32.atomic.rmw16.sub_u", 0x28},    {"i64.atomic.rmw8.sub_u", 0x29},
    {"i64.atomic.rmw16.sub_u", 0x2a},    {"i64.atomic.rmw32.sub_u", 0x2b},
    {"i32.atomic.rmw.and", 0x2c},        {"i64.atomic.rmw.and", 0x2d},
    {"i32.atomic.rmw8.and_u", 0x2e},     {"i32.atomic.rmw16.and_u", 0x2f},
    {"i64.atomic.rmw8.and_u", 0x30},     {"i64.atomic.rmw16.and_u", 0x31},
    {"i64.atomic.rmw32.and_u", 0x32},    {"i32.atomic.rmw.or", 0x33},
    {"i64.atomic.rmw.or", 0x34},         {"i32.atomic.rmw8.or_u", 0x35},
    {"i32.atomic.rmw16.or_u", 0x36},     {"i64.atomic.rmw8.or_u", 0x37},
    {"i64.atomic.rmw16.or_u", 0x38},     {"i64.atomic.rmw32.or_u", 0x39},
    {"i32.atomic.rmw.xor", 0x3a},        {"i64.atomic.rmw.xor", 0x3b},
    {"i32.atomic.rmw8.xor_u", 0x3c},     {"i32.atomic.rmw16.xor_u", 0x3d},
    {"i64.atomic.rmw8.xor_u", 0x3e},     {"i64.atomic.rmw16.xor_u", 0x3f},
    {"i64.atomic.rmw32.xor_u", 0x40},    {"i32.atomic.rmw.xchg", 0x41},
    {"i64.atomic.rmw.xchg", 0x42},       {"i32.atomic.rmw8.xchg_u", 0x43},
    {"i32.atomic.rmw16.xchg_u", 0x44},   {"i64.atomic.rmw8.xchg_u", 0x45},
    {"i64.atomic.rmw16.xchg_u", 0x46},   {"i64.atomic.rmw32.xchg_u", 0x47},
    {"i32.atomic.rmw.cmpxchg", 0x48},    {"i64.atomic.rmw.cmpxchg", 0x49},
    {"i32.atomic.rmw8.cmpxchg_u", 0x4a}, {"i32.atomic.rmw16.cmpxchg_u", 0x4b},
    {"i64.atomic.rmw8.cmpxchg_u", 0x4c}, {"i64.atomic.rmw16.cmpxchg_u", 0x4d},
    {"i64.atomic.rmw32.cmpxchg_u", 0x4e}};

template <std::size_t N>
bool lookup(const std::string &name, unsigned char &opcode, const OpcodeName (&entries)[N]) {
    for (std::size_t i = 0; i < N; ++i) {
        if (name == entries[i].name) {
            opcode = entries[i].opcode;
            return true;
        }
    }
    return false;
}

template <std::size_t N>
bool lookup(const std::string &name, uint32_t &opcode, const ExtendedOpcodeName (&entries)[N]) {
    for (std::size_t i = 0; i < N; ++i) {
        if (name == entries[i].name) {
            opcode = entries[i].opcode;
            return true;
        }
    }
    return false;
}

}

bool numeric_opcode(const std::string &name, unsigned char &opcode) {
    return lookup(name, opcode, numeric_opcodes);
}

bool memory_opcode(const std::string &name, unsigned char &opcode) {
    return lookup(name, opcode, memory_opcodes);
}

bool atomic_opcode(const std::string &name, uint32_t &subopcode) {
    return lookup(name, subopcode, atomic_opcodes);
}

uint32_t natural_alignment(unsigned char opcode) {
    switch (opcode) {
    case 0x28:
    case 0x2a:
    case 0x36:
    case 0x38:
    case 0x34:
    case 0x35:
    case 0x3e:
        return 2;
    case 0x29:
    case 0x2b:
    case 0x37:
    case 0x39:
        return 3;
    case 0x2e:
    case 0x2f:
    case 0x32:
    case 0x33:
    case 0x3b:
    case 0x3d:
        return 1;
    default:
        return 0;
    }
}

uint32_t atomic_natural_alignment(uint32_t subopcode) {
    if (subopcode == 0x03)
        return 0;
    if (subopcode == 0x00 || subopcode == 0x01 || subopcode == 0x10 || subopcode == 0x17)
        return 2;
    if (subopcode == 0x02 || subopcode == 0x11 || subopcode == 0x18)
        return 3;
    if (subopcode == 0x12 || subopcode == 0x14 || subopcode == 0x19 || subopcode == 0x1b)
        return 0;
    if (subopcode == 0x13 || subopcode == 0x15 || subopcode == 0x1a || subopcode == 0x1c)
        return 1;
    if (subopcode == 0x16 || subopcode == 0x1d)
        return 2;
    if (subopcode >= 0x1e && subopcode <= 0x4e) {
        const uint32_t lane = (subopcode - 0x1e) % 7;
        if (lane == 0)
            return 2;
        if (lane == 1)
            return 3;
        if (lane == 2 || lane == 4)
            return 0;
        if (lane == 3 || lane == 5)
            return 1;
        return 2;
    }
    return 0xffffffffU;
}

}
