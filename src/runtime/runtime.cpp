#include "inline_vector.h"
#include "value_stack.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <watx/runtime/imports.h>
#include <watx/runtime/runtime.h>
#include <watx/validator.h>

namespace watx {
namespace {
struct Control {
    uint32_t start, end, after;
    unsigned char kind;
    std::size_t height;
    ValueType result;
};
struct ExecutionContext {
    uint64_t local_fuel;
    uint64_t *fuel;
    uint32_t depth, thread_id;
    RuntimeCancellation *cancelled;
    ExecutionContext(uint64_t f, uint32_t id, RuntimeCancellation *c, uint64_t *budget)
        : local_fuel(f), fuel(budget ? budget : &local_fuel), depth(0), thread_id(id),
          cancelled(c) {}
};
struct State {
    RuntimeNativeFrame native;
    ValueStack stack;
    InlineVector<RuntimeValue, 8> locals;
    InlineVector<Control, 8> controls;
    uint32_t pc;
    ExecutionContext *execution;
    State() : stack(native), pc(0), execution(0) {}
};
struct CallDepth {
    uint32_t &depth;
    explicit CallDepth(uint32_t &value) : depth(value) {
        ++depth;
    }
    ~CallDepth() {
        --depth;
    }

  private:
    CallDepth(const CallDepth &);
    CallDepth &operator=(const CallDepth &);
};
struct RuntimeLock {
    RuntimeMutex &mutex;
    explicit RuntimeLock(RuntimeMutex &m) : mutex(m) {
        mutex.lock();
    }
    ~RuntimeLock() {
        mutex.unlock();
    }

  private:
    RuntimeLock(const RuntimeLock &);
    RuntimeLock &operator=(const RuntimeLock &);
};
struct RuntimeOptionalLock {
    RuntimeMutex &mutex;
    bool active;
    RuntimeOptionalLock(RuntimeMutex &m, bool enabled) : mutex(m), active(enabled) {
        if (active)
            mutex.lock();
    }
    ~RuntimeOptionalLock() {
        if (active)
            mutex.unlock();
    }

  private:
    RuntimeOptionalLock(const RuntimeOptionalLock &);
    RuntimeOptionalLock &operator=(const RuntimeOptionalLock &);
};
static RuntimeImportKey resource_key(const std::string &m, const std::string &n) {
    return RuntimeImports::key(m, n);
}
static bool sign_f(float x) {
    uint32_t b;
    std::memcpy(&b, &x, 4);
    return (b >> 31) != 0;
}
static bool sign_d(double x) {
    uint64_t b;
    std::memcpy(&b, &x, 8);
    return (b >> 63) != 0;
}
static float copy_sign_f(float x, float y) {
    uint32_t a, b;
    std::memcpy(&a, &x, 4);
    std::memcpy(&b, &y, 4);
    a = (a & 0x7fffffffu) | (b & 0x80000000u);
    std::memcpy(&x, &a, 4);
    return x;
}
static double copy_sign_d(double x, double y) {
    uint64_t a, b;
    std::memcpy(&a, &x, 8);
    std::memcpy(&b, &y, 8);
    a = (a & ~((uint64_t)1 << 63)) | (b & ((uint64_t)1 << 63));
    std::memcpy(&x, &a, 8);
    return x;
}
static bool finite_d(double x) {
    return x == x && x <= std::numeric_limits<double>::max() &&
           x >= -std::numeric_limits<double>::max();
}
static double nearest_even(double x) {
    if (x != x || x > std::numeric_limits<double>::max() ||
        x < -std::numeric_limits<double>::max() || x == 0.0)
        return x;
    double lo = std::floor(x), d = x - lo, r;
    if (d < 0.5)
        r = lo;
    else if (d > 0.5)
        r = lo + 1.0;
    else
        r = std::fmod(std::fabs(lo), 2.0) == 0.0 ? lo : lo + 1.0;
    return r == 0.0 ? copy_sign_d(0.0, x) : r;
}
static uint32_t ashr32(uint32_t x, uint32_t n) {
    n &= 31;
    if (!n)
        return x;
    uint32_t r = x >> n;
    if (x & 0x80000000u)
        r |= (~(uint32_t)0) << (32 - n);
    return r;
}
static uint64_t ashr64(uint64_t x, uint32_t n) {
    n &= 63;
    if (!n)
        return x;
    uint64_t r = x >> n;
    if (x & ((uint64_t)1 << 63))
        r |= (~(uint64_t)0) << (64 - n);
    return r;
}
static uint32_t read_u32(const std::vector<unsigned char> &b, std::size_t p) {
    if (p + 4 > b.size())
        throw std::runtime_error("unexpected end of constant expression");
    return (uint32_t)b[p] | ((uint32_t)b[p + 1] << 8) | ((uint32_t)b[p + 2] << 16) |
           ((uint32_t)b[p + 3] << 24);
}
static uint64_t read_u64(const std::vector<unsigned char> &b, std::size_t p) {
    return (uint64_t)read_u32(b, p) | ((uint64_t)read_u32(b, p + 4) << 32);
}
static RuntimeValue pop(State &s) {
    if (s.stack.empty())
        throw std::runtime_error("operand stack underflow");
    RuntimeValue v = s.stack.back();
    s.stack.pop_back();
    return v;
}
static void expect_type(const RuntimeValue &v, ValueType t) {
    if (v.type != t)
        throw std::runtime_error("runtime value type mismatch");
}
static uint32_t u32(const RuntimeValue &v) {
    expect_type(v, TYPE_I32);
    return v.as_u32();
}
static uint64_t u64(const RuntimeValue &v) {
    expect_type(v, TYPE_I64);
    return v.as_u64();
}
static int32_t i32(const RuntimeValue &v) {
    expect_type(v, TYPE_I32);
    return v.as_i32();
}
static int64_t i64(const RuntimeValue &v) {
    expect_type(v, TYPE_I64);
    return v.as_i64();
}
static float f32(const RuntimeValue &v) {
    expect_type(v, TYPE_F32);
    return v.as_f32();
}
static double f64(const RuntimeValue &v) {
    expect_type(v, TYPE_F64);
    return v.as_f64();
}
static RuntimeValue boolv(bool b) {
    return RuntimeValue::i32(b ? 1u : 0u);
}
static bool truth(uint32_t x) {
    return x != 0;
}
static const uint32_t invalid_pc = ~(uint32_t)0;
static RuntimeMutex atomic_order_mutex;
static bool same_type(const FunctionType &a, const FunctionType &b) {
    return a.parameters == b.parameters && a.results == b.results;
}
static uint32_t clz32(uint32_t x) {
    uint32_t n = 0;
    if (!x)
        return 32;
    for (uint32_t m = 0x80000000u; !(x & m); m >>= 1)
        ++n;
    return n;
}
static uint32_t ctz32(uint32_t x) {
    uint32_t n = 0;
    if (!x)
        return 32;
    while (!(x & 1)) {
        ++n;
        x >>= 1;
    }
    return n;
}
static uint32_t pop32(uint32_t x) {
    uint32_t n = 0;
    while (x) {
        n += x & 1;
        x >>= 1;
    }
    return n;
}
static uint32_t rotl32(uint32_t x, uint32_t n) {
    n &= 31;
    return (x << n) | (x >> ((32 - n) & 31));
}
static uint32_t rotr32(uint32_t x, uint32_t n) {
    n &= 31;
    return (x >> n) | (x << ((32 - n) & 31));
}
static uint64_t clz64(uint64_t x) {
    uint64_t n = 0;
    if (!x)
        return 64;
    for (uint64_t m = ((uint64_t)1 << 63); !(x & m); m >>= 1)
        ++n;
    return n;
}
static uint64_t ctz64(uint64_t x) {
    uint64_t n = 0;
    if (!x)
        return 64;
    while (!(x & 1)) {
        ++n;
        x >>= 1;
    }
    return n;
}
static uint64_t pop64(uint64_t x) {
    uint64_t n = 0;
    while (x) {
        n += x & 1;
        x >>= 1;
    }
    return n;
}
static uint64_t rotl64(uint64_t x, uint32_t n) {
    n &= 63;
    return (x << n) | (x >> ((64 - n) & 63));
}
static uint64_t rotr64(uint64_t x, uint32_t n) {
    n &= 63;
    return (x >> n) | (x << ((64 - n) & 63));
}
static uint32_t div_s32(uint32_t x, uint32_t y) {
    bool nx = (x >> 31) != 0, ny = (y >> 31) != 0;
    uint32_t ax = nx ? 0u - x : x, ay = ny ? 0u - y : y;
    uint32_t q = ax / ay;
    return nx != ny ? 0u - q : q;
}
static uint32_t rem_s32(uint32_t x, uint32_t y) {
    bool nx = (x >> 31) != 0;
    uint32_t ax = nx ? 0u - x : x, ay = (y >> 31) ? 0u - y : y;
    uint32_t r = ax % ay;
    return nx ? 0u - r : r;
}
static uint64_t div_s64(uint64_t x, uint64_t y) {
    bool nx = (x >> 63) != 0, ny = (y >> 63) != 0;
    uint64_t ax = nx ? 0 - x : x, ay = ny ? 0 - y : y;
    uint64_t q = ax / ay;
    return nx != ny ? 0 - q : q;
}
static uint64_t rem_s64(uint64_t x, uint64_t y) {
    bool nx = (x >> 63) != 0;
    uint64_t ax = nx ? 0 - x : x, ay = (y >> 63) ? 0 - y : y;
    uint64_t r = ax % ay;
    return nx ? 0 - r : r;
}
static RuntimeValue numeric(unsigned char op, State &s) {
    RuntimeValue a, b;
    switch (op) {
    case 0x45:
        return boolv(u32(pop(s)) == 0);
    case 0x50:
        return boolv(u64(pop(s)) == 0);
    case 0x46:
    case 0x47:
    case 0x48:
    case 0x49:
    case 0x4a:
    case 0x4b:
    case 0x4c:
    case 0x4d:
    case 0x4e:
    case 0x4f:
        b = pop(s);
        a = pop(s);
        {
            uint32_t x = u32(a), y = u32(b);
            bool r = op == 0x46   ? x == y
                     : op == 0x47 ? x != y
                     : op == 0x48 ? i32(a) < i32(b)
                     : op == 0x49 ? x < y
                     : op == 0x4a ? i32(a) > i32(b)
                     : op == 0x4b ? x > y
                     : op == 0x4c ? i32(a) <= i32(b)
                     : op == 0x4d ? x <= y
                     : op == 0x4e ? i32(a) >= i32(b)
                                  : x >= y;
            return boolv(r);
        }
    case 0x51:
    case 0x52:
    case 0x53:
    case 0x54:
    case 0x55:
    case 0x56:
    case 0x57:
    case 0x58:
    case 0x59:
    case 0x5a:
        b = pop(s);
        a = pop(s);
        {
            uint64_t x = u64(a), y = u64(b);
            bool r = op == 0x51   ? x == y
                     : op == 0x52 ? x != y
                     : op == 0x53 ? i64(a) < i64(b)
                     : op == 0x54 ? x < y
                     : op == 0x55 ? i64(a) > i64(b)
                     : op == 0x56 ? x > y
                     : op == 0x57 ? i64(a) <= i64(b)
                     : op == 0x58 ? x <= y
                     : op == 0x59 ? i64(a) >= i64(b)
                                  : x >= y;
            return boolv(r);
        }
    case 0x5b:
    case 0x5c:
    case 0x5d:
    case 0x5e:
    case 0x5f:
    case 0x60:
        b = pop(s);
        a = pop(s);
        {
            float x = f32(a), y = f32(b);
            return boolv(op == 0x5b   ? x == y
                         : op == 0x5c ? x != y
                         : op == 0x5d ? x < y
                         : op == 0x5e ? x > y
                         : op == 0x5f ? x <= y
                                      : x >= y);
        }
    case 0x61:
    case 0x62:
    case 0x63:
    case 0x64:
    case 0x65:
    case 0x66:
        b = pop(s);
        a = pop(s);
        {
            double x = f64(a), y = f64(b);
            return boolv(op == 0x61   ? x == y
                         : op == 0x62 ? x != y
                         : op == 0x63 ? x < y
                         : op == 0x64 ? x > y
                         : op == 0x65 ? x <= y
                                      : x >= y);
        }
    case 0x67:
        return RuntimeValue::i32(clz32(u32(pop(s))));
    case 0x68:
        return RuntimeValue::i32(ctz32(u32(pop(s))));
    case 0x69:
        return RuntimeValue::i32(pop32(u32(pop(s))));
    case 0x79:
        return RuntimeValue::i64(clz64(u64(pop(s))));
    case 0x7a:
        return RuntimeValue::i64(ctz64(u64(pop(s))));
    case 0x7b:
        return RuntimeValue::i64(pop64(u64(pop(s))));
    case 0x6a:
    case 0x6b:
    case 0x6c:
    case 0x6d:
    case 0x6e:
    case 0x6f:
    case 0x70:
    case 0x71:
    case 0x72:
    case 0x73:
    case 0x74:
    case 0x75:
    case 0x76:
    case 0x77:
    case 0x78:
        b = pop(s);
        a = pop(s);
        {
            uint32_t x = u32(a), y = u32(b), r = 0;
            switch (op) {
            case 0x6a:
                r = x + y;
                break;
            case 0x6b:
                r = x - y;
                break;
            case 0x6c:
                r = x * y;
                break;
            case 0x6d:
                if (!y)
                    throw std::runtime_error("integer divide by zero");
                if (x == 0x80000000u && y == 0xffffffffu)
                    throw std::runtime_error("integer overflow");
                r = div_s32(x, y);
                break;
            case 0x6e:
                if (!y)
                    throw std::runtime_error("integer divide by zero");
                r = x / y;
                break;
            case 0x6f:
                if (!y)
                    throw std::runtime_error("integer divide by zero");
                r = rem_s32(x, y);
                break;
            case 0x70:
                if (!y)
                    throw std::runtime_error("integer divide by zero");
                r = x % y;
                break;
            case 0x71:
                r = x & y;
                break;
            case 0x72:
                r = x | y;
                break;
            case 0x73:
                r = x ^ y;
                break;
            case 0x74:
                r = x << (y & 31);
                break;
            case 0x75:
                r = ashr32(x, y);
                break;
            case 0x76:
                r = x >> (y & 31);
                break;
            case 0x77:
                r = rotl32(x, y);
                break;
            case 0x78:
                r = rotr32(x, y);
                break;
            }
            return RuntimeValue::i32(r);
        }
    case 0x7c:
    case 0x7d:
    case 0x7e:
    case 0x7f:
    case 0x80:
    case 0x81:
    case 0x82:
    case 0x83:
    case 0x84:
    case 0x85:
    case 0x86:
    case 0x87:
    case 0x88:
    case 0x89:
    case 0x8a:
        b = pop(s);
        a = pop(s);
        {
            uint64_t x = u64(a), y = u64(b), r = 0;
            switch (op) {
            case 0x7c:
                r = x + y;
                break;
            case 0x7d:
                r = x - y;
                break;
            case 0x7e:
                r = x * y;
                break;
            case 0x7f:
                if (!y)
                    throw std::runtime_error("integer divide by zero");
                if (x == ((uint64_t)1 << 63) && y == ~(uint64_t)0)
                    throw std::runtime_error("integer overflow");
                r = div_s64(x, y);
                break;
            case 0x80:
                if (!y)
                    throw std::runtime_error("integer divide by zero");
                r = x / y;
                break;
            case 0x81:
                if (!y)
                    throw std::runtime_error("integer divide by zero");
                r = rem_s64(x, y);
                break;
            case 0x82:
                if (!y)
                    throw std::runtime_error("integer divide by zero");
                r = x % y;
                break;
            case 0x83:
                r = x & y;
                break;
            case 0x84:
                r = x | y;
                break;
            case 0x85:
                r = x ^ y;
                break;
            case 0x86:
                r = x << (y & 63);
                break;
            case 0x87:
                r = ashr64(x, (uint32_t)y);
                break;
            case 0x88:
                r = x >> (y & 63);
                break;
            case 0x89:
                r = rotl64(x, (uint32_t)y);
                break;
            case 0x8a:
                r = rotr64(x, (uint32_t)y);
                break;
            }
            return RuntimeValue::i64(r);
        }
    default:
        break;
    }
    if (op >= 0x8b && op <= 0x98) {
        a = pop(s);
        if (op == 0x8b || op == 0x8c) {
            expect_type(a, TYPE_F32);
            uint32_t bits = a.as_u32();
            return RuntimeValue(TYPE_F32, op == 0x8b ? bits & 0x7fffffffU : bits ^ 0x80000000U);
        }
        if (op <= 0x91) {
            float x = f32(a), r;
            switch (op) {
            case 0x8b:
                r = (float)std::fabs((double)x);
                break;
            case 0x8c:
                r = -x;
                break;
            case 0x8d:
                r = (float)std::ceil((double)x);
                break;
            case 0x8e:
                r = (float)std::floor((double)x);
                break;
            case 0x8f:
                r = (float)(x < 0.0f ? std::ceil((double)x) : std::floor((double)x));
                break;
            case 0x90:
                r = (float)nearest_even(x);
                break;
            default:
                r = (float)std::sqrt((double)x);
                break;
            }
            return RuntimeValue::f32(r);
        }
        b = pop(s);
        float x = f32(b), y = f32(a), r;
        switch (op) {
        case 0x92:
            r = x + y;
            break;
        case 0x93:
            r = x - y;
            break;
        case 0x94:
            r = x * y;
            break;
        case 0x95:
            r = x / y;
            break;
        case 0x96:
            r = (x != x || y != y)
                    ? std::numeric_limits<float>::quiet_NaN()
                    : (x == 0.0f && y == 0.0f ? (sign_f(x) || sign_f(y) ? -0.0f : 0.0f)
                                              : (x < y ? x : y));
            break;
        case 0x97:
            r = (x != x || y != y)
                    ? std::numeric_limits<float>::quiet_NaN()
                    : (x == 0.0f && y == 0.0f ? (sign_f(x) && sign_f(y) ? -0.0f : 0.0f)
                                              : (x > y ? x : y));
            break;
        default:
            r = copy_sign_f(x, y);
            break;
        }
        return RuntimeValue::f32(r);
    }
    if (op >= 0x99 && op <= 0xa6) {
        a = pop(s);
        if (op == 0x99 || op == 0x9a) {
            expect_type(a, TYPE_F64);
            uint64_t sign = static_cast<uint64_t>(1) << 63;
            return RuntimeValue(TYPE_F64, op == 0x99 ? a.bits & ~sign : a.bits ^ sign);
        }
        if (op <= 0x9f) {
            double x = f64(a), r;
            switch (op) {
            case 0x99:
                r = std::fabs(x);
                break;
            case 0x9a:
                r = -x;
                break;
            case 0x9b:
                r = std::ceil(x);
                break;
            case 0x9c:
                r = std::floor(x);
                break;
            case 0x9d:
                r = x < 0.0 ? std::ceil(x) : std::floor(x);
                break;
            case 0x9e:
                r = nearest_even(x);
                break;
            default:
                r = std::sqrt(x);
                break;
            }
            return RuntimeValue::f64(r);
        }
        b = pop(s);
        double x = f64(b), y = f64(a), r;
        switch (op) {
        case 0xa0:
            r = x + y;
            break;
        case 0xa1:
            r = x - y;
            break;
        case 0xa2:
            r = x * y;
            break;
        case 0xa3:
            r = x / y;
            break;
        case 0xa4:
            r = (x != x || y != y) ? std::numeric_limits<double>::quiet_NaN()
                                   : (x == 0.0 && y == 0.0 ? (sign_d(x) || sign_d(y) ? -0.0 : 0.0)
                                                           : (x < y ? x : y));
            break;
        case 0xa5:
            r = (x != x || y != y) ? std::numeric_limits<double>::quiet_NaN()
                                   : (x == 0.0 && y == 0.0 ? (sign_d(x) && sign_d(y) ? -0.0 : 0.0)
                                                           : (x > y ? x : y));
            break;
        default:
            r = copy_sign_d(x, y);
            break;
        }
        return RuntimeValue::f64(r);
    }
    return RuntimeValue();
}
}

uint32_t RuntimeValue::as_u32() const {
    return (uint32_t)bits;
}
int32_t RuntimeValue::as_i32() const {
    uint32_t x = (uint32_t)bits;
    return x <= 0x7fffffffu ? (int32_t)x : (int32_t)(-1 - (int32_t)(~x));
}
uint64_t RuntimeValue::as_u64() const {
    return bits;
}
int64_t RuntimeValue::as_i64() const {
    return bits <= (((uint64_t)0x7fffffffU << 32) | 0xffffffffU) ? (int64_t)bits
                                                                 : (int64_t)(-1 - (int64_t)(~bits));
}
float RuntimeValue::as_f32() const {
    uint32_t x = (uint32_t)bits;
    float v;
    std::memcpy(&v, &x, 4);
    return v;
}
double RuntimeValue::as_f64() const {
    double v;
    std::memcpy(&v, &bits, 8);
    return v;
}
RuntimeValue RuntimeValue::i32(uint32_t v) {
    return RuntimeValue(TYPE_I32, v);
}
RuntimeValue RuntimeValue::i64(uint64_t v) {
    return RuntimeValue(TYPE_I64, v);
}
RuntimeValue RuntimeValue::f32(float v) {
    uint32_t x;
    std::memcpy(&x, &v, 4);
    return RuntimeValue(TYPE_F32, x);
}
RuntimeValue RuntimeValue::f64(double v) {
    uint64_t x;
    std::memcpy(&x, &v, 8);
    return RuntimeValue(TYPE_F64, x);
}
RuntimeMemory::~RuntimeMemory() {
    for (std::map<uint64_t, WaitQueue *>::iterator it = waiters.begin(); it != waiters.end(); ++it)
        delete it->second;
}
void RuntimeMemory::wake_all() {
    RuntimeLock lock(mutex);
    for (std::map<uint64_t, WaitQueue *>::iterator it = waiters.begin(); it != waiters.end();
         ++it) {
        for (std::size_t i = 0; i < it->second->waiters.size(); ++i)
            it->second->waiters[i]->condition.notify_all();
    }
}

struct RuntimeInstance::Impl {
    struct ControlTarget {
        uint32_t end, alternate;
        uint32_t immediate;
        unsigned char opcode;
        RuntimeValue constant;
        ControlTarget() : end(invalid_pc), alternate(invalid_pc), immediate(0), opcode(0) {}
    };
    const Module &mod;
    RuntimeOptions options;
    RuntimeExecutor *executor;
    RuntimeInstance *owner;
    RuntimeImports imports;
    std::vector<RuntimeMemory *> memories;
    std::vector<RuntimeTable *> tables;
    std::vector<RuntimeGlobal *> globals;
    std::vector<RuntimeMemory *> owned_memories;
    std::vector<RuntimeTable *> owned_tables;
    std::vector<RuntimeGlobal *> owned_globals;
    std::vector<std::vector<ControlTarget> > control_targets;
    std::vector<std::size_t> stack_limits;
    bool initialized, initialization_started;
    Impl(const Module &m, const RuntimeOptions &o, const RuntimeImports *im)
        : mod(m), options(o), executor(0), owner(0), initialized(false),
          initialization_started(false) {
        if (im)
            imports = *im;
        control_targets.resize(m.functions.size());
        stack_limits.resize(m.functions.size());
        for (std::size_t f = 0; f < m.functions.size(); ++f) {
            stack_limits[f] = analyze_function_stack(m, f);
            const std::vector<Instruction> &code = m.functions[f].code;
            std::vector<ControlTarget> &targets = control_targets[f];
            targets.resize(code.size());
            std::vector<uint32_t> nesting;
            for (std::size_t pc = 0; pc < code.size(); ++pc) {
                const unsigned char op = code[pc].opcode;
                targets[pc].opcode = op;
                targets[pc].immediate = code[pc].immediate;
                if (op >= 0x41 && op <= 0x44) {
                    const ValueType type = op == 0x41   ? TYPE_I32
                                           : op == 0x42 ? TYPE_I64
                                           : op == 0x43 ? TYPE_F32
                                                        : TYPE_F64;
                    targets[pc].constant =
                        RuntimeValue(type, op == 0x41 || op == 0x43 ? read_u32(code[pc].bytes, 0)
                                                                    : read_u64(code[pc].bytes, 0));
                }
                if (op == 0x02 || op == 0x03 || op == 0x04)
                    nesting.push_back(static_cast<uint32_t>(pc));
                else if (op == 0x05 && !nesting.empty())
                    targets[nesting.back()].alternate = static_cast<uint32_t>(pc);
                else if (op == 0x0b && !nesting.empty()) {
                    targets[nesting.back()].end = static_cast<uint32_t>(pc);
                    nesting.pop_back();
                }
            }
        }
    }
    ~Impl() {
        for (std::size_t i = 0; i < owned_memories.size(); ++i)
            delete owned_memories[i];
        for (std::size_t i = 0; i < owned_tables.size(); ++i)
            delete owned_tables[i];
        for (std::size_t i = 0; i < owned_globals.size(); ++i)
            delete owned_globals[i];
    }
};

static RuntimeValue eval_const(const std::vector<Instruction> &code,
                               const std::vector<RuntimeGlobal *> &globals) {
    if (code.empty())
        throw std::runtime_error("empty constant expression");
    const Instruction &i = code[0];
    if (i.opcode == 0x41)
        return RuntimeValue::i32(read_u32(i.bytes, 0));
    if (i.opcode == 0x42)
        return RuntimeValue::i64(read_u64(i.bytes, 0));
    if (i.opcode == 0x43)
        return RuntimeValue(TYPE_F32, read_u32(i.bytes, 0));
    if (i.opcode == 0x44)
        return RuntimeValue(TYPE_F64, read_u64(i.bytes, 0));
    if (i.opcode == 0x23 && i.immediate < globals.size()) {
        RuntimeGlobal *g = globals[i.immediate];
        RuntimeLock lock(g->mutex);
        return g->value;
    }
    throw std::runtime_error("unsupported constant expression");
}

static bool invoke_internal(RuntimeInstance::Impl *impl, uint32_t function,
                            const RuntimeValue *args, std::size_t argument_count,
                            RuntimeValue &result, ExecutionContext &execution);

RuntimeNativeFrame *runtime_native_frame(RuntimeFrame *frame) {
    if (!frame || !frame->instance || !frame->state)
        return 0;
    RuntimeInstance::Impl *impl = frame->instance->impl_;
    State *state = static_cast<State *>(frame->state);
    if (impl->options.trace || impl->options.fuel || !state->execution ||
        state->execution->cancelled || frame->function_index >= impl->mod.functions.size())
        return 0;
    RuntimeValue value;
    if (sizeof(value) != 16 || sizeof(ValueType) != 4 || sizeof(std::size_t) != 8 ||
        offsetof(RuntimeNativeFrame, stack) != 0 || offsetof(RuntimeNativeFrame, size) != 8 ||
        offsetof(RuntimeNativeFrame, locals) != 16 ||
        reinterpret_cast<char *>(&value.bits) - reinterpret_cast<char *>(&value) != 8 ||
        reinterpret_cast<char *>(&value.type) != reinterpret_cast<char *>(&value))
        return 0;
    state->native.locals = state->locals.empty() ? 0 : &state->locals[0];
    return &state->native;
}

void runtime_interpret(RuntimeFrame *frame) {
    if (!frame)
        throw std::runtime_error("invalid runtime frame");
    RuntimeNativeFrame *native = runtime_native_frame(frame);
    uint32_t pc = 0;
    if (!native) {
        while (!frame->finished) {
            pc = runtime_step(frame, pc);
            if (pc == invalid_pc && !frame->finished)
                throw std::runtime_error("interpreter stopped without finishing the frame");
        }
        return;
    }
    const std::vector<RuntimeInstance::Impl::ControlTarget> &code =
        frame->instance->impl_->control_targets[frame->function_index];
    RuntimeValue *values = native->stack;
    RuntimeValue *locals = native->locals;
    std::size_t size = native->size;
    while (!frame->finished) {
        if (pc >= code.size()) {
            native->size = size;
            runtime_step(frame, pc);
            break;
        }
        const RuntimeInstance::Impl::ControlTarget &in = code[pc];
        const unsigned op = in.opcode;
        switch (op) {
        case 0x01:
            break;
        case 0x41:
        case 0x42:
        case 0x43:
        case 0x44:
            values[size++] = in.constant;
            break;
        case 0x20:
            values[size++] = locals[in.immediate];
            break;
        case 0x21:
            locals[in.immediate] = values[--size];
            break;
        case 0x22:
            locals[in.immediate] = values[size - 1];
            break;
        case 0x1a:
            --size;
            break;
        case 0x45:
            values[size - 1] = boolv(static_cast<uint32_t>(values[size - 1].bits) == 0);
            break;
        case 0x50:
            values[size - 1] = boolv(values[size - 1].bits == 0);
            break;
        case 0x46:
        case 0x47:
        case 0x48:
        case 0x49:
        case 0x4a:
        case 0x4b:
        case 0x4c:
        case 0x4d:
        case 0x4e:
        case 0x4f:
        case 0x6a:
        case 0x6b:
        case 0x6c:
        case 0x71:
        case 0x72:
        case 0x73:
        case 0x74:
        case 0x75:
        case 0x76:
        case 0x77:
        case 0x78: {
            const RuntimeValue b = values[--size], a = values[size - 1];
            const uint32_t x = static_cast<uint32_t>(a.bits), y = static_cast<uint32_t>(b.bits);
            uint32_t result = 0;
            switch (op) {
            case 0x46:
                result = x == y;
                break;
            case 0x47:
                result = x != y;
                break;
            case 0x48:
                result = a.as_i32() < b.as_i32();
                break;
            case 0x49:
                result = x < y;
                break;
            case 0x4a:
                result = a.as_i32() > b.as_i32();
                break;
            case 0x4b:
                result = x > y;
                break;
            case 0x4c:
                result = a.as_i32() <= b.as_i32();
                break;
            case 0x4d:
                result = x <= y;
                break;
            case 0x4e:
                result = a.as_i32() >= b.as_i32();
                break;
            case 0x4f:
                result = x >= y;
                break;
            case 0x6a:
                result = x + y;
                break;
            case 0x6b:
                result = x - y;
                break;
            case 0x6c:
                result = x * y;
                break;
            case 0x71:
                result = x & y;
                break;
            case 0x72:
                result = x | y;
                break;
            case 0x73:
                result = x ^ y;
                break;
            case 0x74:
                result = x << (y & 31);
                break;
            case 0x75:
                result = ashr32(x, y);
                break;
            case 0x76:
                result = x >> (y & 31);
                break;
            case 0x77:
                result = rotl32(x, y);
                break;
            case 0x78:
                result = rotr32(x, y);
                break;
            }
            values[size - 1] = RuntimeValue::i32(result);
            break;
        }
        case 0x51:
        case 0x52:
        case 0x53:
        case 0x54:
        case 0x55:
        case 0x56:
        case 0x57:
        case 0x58:
        case 0x59:
        case 0x5a:
        case 0x7c:
        case 0x7d:
        case 0x7e:
        case 0x83:
        case 0x84:
        case 0x85:
        case 0x86:
        case 0x87:
        case 0x88:
        case 0x89:
        case 0x8a: {
            const RuntimeValue b = values[--size], a = values[size - 1];
            const uint64_t x = a.bits, y = b.bits;
            uint64_t result = 0;
            switch (op) {
            case 0x51:
                result = x == y;
                break;
            case 0x52:
                result = x != y;
                break;
            case 0x53:
                result = a.as_i64() < b.as_i64();
                break;
            case 0x54:
                result = x < y;
                break;
            case 0x55:
                result = a.as_i64() > b.as_i64();
                break;
            case 0x56:
                result = x > y;
                break;
            case 0x57:
                result = a.as_i64() <= b.as_i64();
                break;
            case 0x58:
                result = x <= y;
                break;
            case 0x59:
                result = a.as_i64() >= b.as_i64();
                break;
            case 0x5a:
                result = x >= y;
                break;
            case 0x7c:
                result = x + y;
                break;
            case 0x7d:
                result = x - y;
                break;
            case 0x7e:
                result = x * y;
                break;
            case 0x83:
                result = x & y;
                break;
            case 0x84:
                result = x | y;
                break;
            case 0x85:
                result = x ^ y;
                break;
            case 0x86:
                result = x << (y & 63);
                break;
            case 0x87:
                result = ashr64(x, static_cast<uint32_t>(y));
                break;
            case 0x88:
                result = x >> (y & 63);
                break;
            case 0x89:
                result = rotl64(x, static_cast<uint32_t>(y));
                break;
            case 0x8a:
                result = rotr64(x, static_cast<uint32_t>(y));
                break;
            }
            values[size - 1] = RuntimeValue(op <= 0x5a ? TYPE_I32 : TYPE_I64, result);
            break;
        }
        case 0xa0:
        case 0xa1:
        case 0xa2:
        case 0xa3: {
            const double y = values[--size].as_f64(), x = values[size - 1].as_f64();
            const double result = op == 0xa0   ? x + y
                                  : op == 0xa1 ? x - y
                                  : op == 0xa2 ? x * y
                                               : x / y;
            values[size - 1] = RuntimeValue::f64(result);
            break;
        }
        case 0x9f:
            values[size - 1] = RuntimeValue::f64(std::sqrt(values[size - 1].as_f64()));
            break;
        case 0xb7:
            values[size - 1] = RuntimeValue::f64(static_cast<double>(values[size - 1].as_i32()));
            break;
        case 0xb8:
            values[size - 1] = RuntimeValue::f64(static_cast<double>(values[size - 1].as_u32()));
            break;
        case 0xa7:
            values[size - 1] = RuntimeValue::i32(static_cast<uint32_t>(values[size - 1].bits));
            break;
        case 0xac:
            values[size - 1] = RuntimeValue::i64(
                static_cast<uint64_t>(static_cast<int64_t>(values[size - 1].as_i32())));
            break;
        case 0xad:
            values[size - 1] = RuntimeValue::i64(values[size - 1].as_u32());
            break;
        default:
            native->size = size;
            pc = runtime_step(frame, pc);
            size = native->size;
            values = native->stack;
            if (pc == invalid_pc && !frame->finished)
                throw std::runtime_error("interpreter stopped without finishing the frame");
            continue;
        }
        ++pc;
    }
    native->size = size;
}

uint32_t runtime_step(RuntimeFrame *frame, uint32_t pc) {
    if (!frame || !frame->instance)
        return invalid_pc;
    try {
        RuntimeInstance::Impl *impl = frame->instance->impl_;
        State *s = static_cast<State *>(frame->state);
        if (!s)
            throw std::runtime_error("invalid runtime frame");
        if (!s->execution)
            throw std::runtime_error("invalid runtime execution context");
        if (s->execution->cancelled && s->execution->cancelled->cancelled())
            throw std::runtime_error("guest thread cancelled");
        if (frame->function_index >= impl->mod.functions.size())
            throw std::runtime_error("invalid function index");
        const Function &fn = impl->mod.functions[frame->function_index];
        if (fn.imported)
            throw std::runtime_error("cannot step imported function");
        if (fn.type_index >= impl->mod.types.size())
            throw std::runtime_error("invalid function type");
        if (pc >= fn.code.size())
            throw std::runtime_error("program counter out of bounds");
        if (impl->options.fuel) {
            if (*s->execution->fuel == 0)
                throw std::runtime_error("fuel exhausted");
            --*s->execution->fuel;
        }
        const Instruction &in = fn.code[pc];
        if (impl->options.trace)
            std::fprintf(stderr, "wasm f%u pc%u op%02x stack%lu\n", frame->function_index, pc,
                         (unsigned)in.opcode, (unsigned long)s->stack.size());
        s->pc = pc + 1;
        switch (in.opcode) {
        case 0x00:
            throw std::runtime_error("unreachable executed");
        case 0x01:
            break;
        case 0x41:
            s->stack.push_back(RuntimeValue::i32(read_u32(in.bytes, 0)));
            break;
        case 0x42:
            s->stack.push_back(RuntimeValue::i64(read_u64(in.bytes, 0)));
            break;
        case 0x43:
            s->stack.push_back(RuntimeValue(TYPE_F32, read_u32(in.bytes, 0)));
            break;
        case 0x44:
            s->stack.push_back(RuntimeValue(TYPE_F64, read_u64(in.bytes, 0)));
            break;
        case 0x02:
        case 0x03:
        case 0x04: {
            bool enter = true;
            if (in.opcode == 0x04)
                enter = truth(u32(pop(*s)));
            const RuntimeInstance::Impl::ControlTarget &target =
                impl->control_targets[frame->function_index][pc];
            uint32_t end = target.end, els = target.alternate;
            if (end >= fn.code.size())
                throw std::runtime_error("malformed control instruction");
            Control c;
            c.start = pc;
            c.end = end;
            c.after = end + 1;
            c.kind = in.opcode;
            c.height = s->stack.size();
            c.result = in.block_type;
            s->controls.push_back(c);
            if (in.opcode == 0x04 && !enter)
                s->pc = els == invalid_pc ? end : els + 1;
            break;
        }
        case 0x05: {
            if (s->controls.empty())
                throw std::runtime_error("else without if");
            Control c = s->controls.back();
            s->controls.pop_back();
            RuntimeValue v;
            bool has = c.result != TYPE_NONE;
            if (has)
                v = pop(*s);
            s->stack.resize(c.height);
            if (has)
                s->stack.push_back(v);
            s->pc = c.after;
            break;
        }
        case 0x0b: {
            if (s->controls.empty()) {
                frame->finished = true;
                return invalid_pc;
            }
            Control c = s->controls.back();
            s->controls.pop_back();
            RuntimeValue v;
            bool has = c.result != TYPE_NONE;
            if (has) {
                v = pop(*s);
            }
            if (s->stack.size() < c.height)
                throw std::runtime_error("invalid control stack height");
            s->stack.resize(c.height);
            if (has)
                s->stack.push_back(v);
            if (c.kind == 0xff) {
                frame->finished = true;
                return invalid_pc;
            }
            break;
        }
        case 0x0c:
        case 0x0d:
        case 0x0e: {
            bool take = true;
            uint32_t d = in.immediate;
            if (in.opcode == 0x0d)
                take = truth(u32(pop(*s)));
            if (in.opcode == 0x0e) {
                if (in.labels.empty())
                    throw std::runtime_error("empty branch table");
                uint32_t n = u32(pop(*s));
                d = (std::size_t)n < in.labels.size() - 1 ? in.labels[n] : in.labels.back();
            }
            if (take) {
                if (d >= s->controls.size())
                    throw std::runtime_error("invalid branch depth");
                std::size_t ti = s->controls.size() - 1 - d;
                Control c = s->controls[ti];
                bool has = c.kind != 0x03 && c.result != TYPE_NONE;
                RuntimeValue v;
                if (has)
                    v = pop(*s);
                s->stack.resize(c.height);
                if (has)
                    s->stack.push_back(v);
                s->controls.resize(ti + 1);
                if (c.kind == 0x03)
                    s->pc = c.start + 1;
                else if (c.kind == 0xff) {
                    frame->finished = true;
                    return invalid_pc;
                } else {
                    s->controls.pop_back();
                    s->pc = c.after;
                }
            }
            break;
        }
        case 0x0f: {
            const FunctionType &ft = impl->mod.types[fn.type_index];
            RuntimeValue v;
            if (!ft.results.empty())
                v = pop(*s);
            s->stack.clear();
            if (!ft.results.empty())
                s->stack.push_back(v);
            frame->finished = true;
            return invalid_pc;
        }
        case 0x10: {
            if (in.immediate >= impl->mod.functions.size())
                throw std::runtime_error("invalid function index");
            const Function &callee = impl->mod.functions[in.immediate];
            if (callee.type_index >= impl->mod.types.size())
                throw std::runtime_error("invalid function type");
            const FunctionType &ty = impl->mod.types[callee.type_index];
            if (s->stack.size() < ty.parameters.size())
                throw std::runtime_error("operand stack underflow");
            const std::size_t base = s->stack.size() - ty.parameters.size();
            RuntimeValue result;
            const bool has_result = invoke_internal(impl, in.immediate, s->native.stack + base,
                                                    ty.parameters.size(), result, *s->execution);
            s->stack.resize(base);
            if (has_result)
                s->stack.push_back(result);
            break;
        }
        case 0x11: {
            if (impl->tables.empty())
                throw std::runtime_error("table index out of bounds");
            if (in.immediate >= impl->mod.types.size())
                throw std::runtime_error("invalid indirect call type");
            uint32_t idx = u32(pop(*s));
            RuntimeTable *tab = impl->tables[0];
            if (idx >= tab->functions.size())
                throw std::runtime_error("undefined element");
            RuntimeFunctionRef ref;
            {
                RuntimeLock lock(tab->mutex);
                ref = tab->functions[idx];
            }
            if (!ref.instance)
                throw std::runtime_error("uninitialized element");
            RuntimeInstance::Impl *target_impl = ref.instance->impl_;
            if (ref.function_index >= target_impl->mod.functions.size())
                throw std::runtime_error("uninitialized element");
            const Function &target = target_impl->mod.functions[ref.function_index];
            if (target.type_index >= target_impl->mod.types.size() ||
                !same_type(target_impl->mod.types[target.type_index],
                           impl->mod.types[in.immediate]))
                throw std::runtime_error("indirect call type mismatch");
            const FunctionType &ty = impl->mod.types[in.immediate];
            if (s->stack.size() < ty.parameters.size())
                throw std::runtime_error("operand stack underflow");
            const std::size_t base = s->stack.size() - ty.parameters.size();
            RuntimeValue result;
            const bool has_result =
                invoke_internal(target_impl, ref.function_index, s->native.stack + base,
                                ty.parameters.size(), result, *s->execution);
            s->stack.resize(base);
            if (has_result)
                s->stack.push_back(result);
            break;
        }
        case 0x1a:
            pop(*s);
            break;
        case 0x1b: {
            uint32_t cond = u32(pop(*s));
            RuntimeValue v2 = pop(*s), v1 = pop(*s);
            expect_type(v1, v2.type);
            s->stack.push_back(cond ? v1 : v2);
            break;
        }
        case 0x20:
            if (in.immediate >= s->locals.size())
                throw std::runtime_error("invalid local index");
            s->stack.push_back(s->locals[in.immediate]);
            break;
        case 0x21:
        case 0x22: {
            if (in.immediate >= s->locals.size())
                throw std::runtime_error("invalid local index");
            RuntimeValue v = pop(*s);
            expect_type(v, s->locals[in.immediate].type);
            s->locals[in.immediate] = v;
            if (in.opcode == 0x22)
                s->stack.push_back(v);
            break;
        }
        case 0x23:
            if (in.immediate >= impl->globals.size())
                throw std::runtime_error("invalid global index");
            {
                RuntimeGlobal *g = impl->globals[in.immediate];
                RuntimeLock lock(g->mutex);
                s->stack.push_back(g->value);
            }
            break;
        case 0x24: {
            if (in.immediate >= impl->globals.size())
                throw std::runtime_error("invalid global index");
            RuntimeValue v = pop(*s);
            RuntimeGlobal *g = impl->globals[in.immediate];
            RuntimeLock lock(g->mutex);
            if (!g->mutable_)
                throw std::runtime_error("immutable global");
            expect_type(v, g->value.type);
            g->value = v;
            break;
        }
        case 0x3f: {
            if (impl->memories.empty())
                throw std::runtime_error("memory index out of bounds");
            RuntimeMemory *memory = impl->memories[0];
            RuntimeLock lock(memory->mutex);
            s->stack.push_back(RuntimeValue::i32((uint32_t)(memory->bytes.size() / 65536)));
            break;
        }
        case 0x40: {
            if (impl->memories.empty())
                throw std::runtime_error("memory index out of bounds");
            RuntimeMemory *m = impl->memories[0];
            RuntimeLock lock(m->mutex);
            uint32_t old = (uint32_t)(m->bytes.size() / 65536), n = u32(pop(*s));
            uint64_t total = (uint64_t)old + n;
            uint32_t cap =
                impl->options.memory_limit_pages < 65536 ? impl->options.memory_limit_pages : 65536;
            uint32_t max = m->has_maximum ? m->maximum_pages : cap;
            if (total > max || total > cap)
                s->stack.push_back(RuntimeValue::i32(0xffffffffu));
            else {
                try {
                    m->bytes.resize((std::size_t)total * 65536);
                    s->stack.push_back(RuntimeValue::i32(old));
                } catch (...) {
                    s->stack.push_back(RuntimeValue::i32(0xffffffffu));
                }
            }
            break;
        }
        case 0xfe: {
            uint32_t sub = in.extended_opcode;
            if (sub == 3) {
                RuntimeLock barrier(atomic_order_mutex);
                break;
            }
            if (impl->memories.empty())
                throw std::runtime_error("atomic instruction requires memory");
            RuntimeMemory *m = impl->memories[0];
            RuntimeValue value, expected, replacement;
            uint32_t address = 0;
            uint64_t effective = 0;
            uint64_t old = 0;
            unsigned width = 0;
            ValueType value_type = TYPE_I32;
            bool load = false, store = false, wait = false, notify = false;
            uint32_t operation = 0;
            int64_t timeout_value = 0;
            uint32_t notify_count = 0;
            if (sub == 0) {
                notify_count = u32(pop(*s));
                address = u32(pop(*s));
                width = 4;
                notify = true;
            } else if (sub == 1 || sub == 2) {
                timeout_value = i64(pop(*s));
                expected = pop(*s);
                value_type = sub == 1 ? TYPE_I32 : TYPE_I64;
                expect_type(expected, value_type);
                address = u32(pop(*s));
                width = sub == 1 ? 4 : 8;
                wait = true;
            } else if (sub >= 16 && sub <= 22) {
                load = true;
                if (sub == 16 || sub == 17)
                    width = sub == 16 ? 4 : 8;
                else if (sub == 18 || sub == 20)
                    width = 1;
                else if (sub == 19 || sub == 21)
                    width = 2;
                else
                    width = 4;
                value_type = (sub == 17 || sub >= 20) ? TYPE_I64 : TYPE_I32;
                address = u32(pop(*s));
            } else if (sub >= 23 && sub <= 29) {
                store = true;
                value_type = (sub == 24 || sub >= 27) ? TYPE_I64 : TYPE_I32;
                width = sub == 23   ? 4
                        : sub == 24 ? 8
                                    : (sub == 25 || sub == 27   ? 1
                                       : sub == 26 || sub == 28 ? 2
                                                                : 4);
                value = pop(*s);
                expect_type(value, value_type);
                address = u32(pop(*s));
            } else if (sub >= 30 && sub <= 78) {
                operation = (sub - 30) / 7;
                uint32_t kind = (sub - 30) % 7;
                width = kind == 0                ? 4
                        : kind == 1              ? 8
                        : kind == 2 || kind == 4 ? 1
                        : kind == 3 || kind == 5 ? 2
                                                 : 4;
                value_type = kind == 1 || kind >= 4 ? TYPE_I64 : TYPE_I32;
                if (operation == 6) {
                    replacement = pop(*s);
                    expected = pop(*s);
                    expect_type(replacement, value_type);
                    expect_type(expected, value_type);
                } else {
                    value = pop(*s);
                    expect_type(value, value_type);
                }
                address = u32(pop(*s));
            } else
                throw std::runtime_error("unsupported atomic instruction");
            if (wait && (!m->shared))
                throw std::runtime_error("atomic.wait requires shared memory");
            effective = (uint64_t)address + in.secondary;
            if (effective % width)
                throw std::runtime_error("unaligned atomic access");
            RuntimeOptionalLock order_lock(atomic_order_mutex, !wait);
            RuntimeLock lock(m->mutex);
            if (effective + width > m->bytes.size())
                throw std::runtime_error("out of bounds atomic access");
            for (unsigned q = 0; q < width; ++q)
                old |= (uint64_t)m->bytes[(std::size_t)effective + q] << (q * 8);
            if (notify) {
                std::map<uint64_t, RuntimeMemory::WaitQueue *>::iterator it =
                    m->waiters.find(effective);
                uint32_t awakened = 0;
                if (it != m->waiters.end()) {
                    for (std::size_t i = 0;
                         i < it->second->waiters.size() && awakened < notify_count; ++i)
                        if (!it->second->waiters[i]->notified) {
                            it->second->waiters[i]->notified = true;
                            it->second->waiters[i]->condition.notify_one();
                            ++awakened;
                        }
                }
                s->stack.push_back(RuntimeValue::i32(awakened));
            } else if (wait) {
                uint64_t expected_bits = expected.bits;
                if (width == 4)
                    expected_bits &= 0xffffffffu;
                if (old != expected_bits) {
                    s->stack.push_back(RuntimeValue::i32(1));
                } else {
                    std::map<uint64_t, RuntimeMemory::WaitQueue *>::iterator it =
                        m->waiters.find(effective);
                    bool created_queue = false;
                    if (it == m->waiters.end()) {
                        RuntimeMemory::WaitQueue *created = new RuntimeMemory::WaitQueue;
                        try {
                            it = m->waiters.insert(std::make_pair(effective, created)).first;
                            created_queue = true;
                        } catch (...) {
                            delete created;
                            throw;
                        }
                    }
                    RuntimeMemory::WaitQueue *queue = it->second;
                    RuntimeMemory::WaitQueue::Waiter waiter;
                    try {
                        queue->waiters.push_back(&waiter);
                    } catch (...) {
                        if (created_queue) {
                            m->waiters.erase(effective);
                            delete queue;
                        }
                        throw;
                    }
                    bool notified = false;
                    bool timed_out = false;
                    RuntimeCancellation *cancelled = s->execution->cancelled;
                    uint64_t deadline = 0;
                    if (timeout_value >= 0) {
                        uint64_t now = runtime_monotonic_nanoseconds();
                        uint64_t duration = (uint64_t)timeout_value;
                        deadline = duration > ~(uint64_t)0 - now ? ~(uint64_t)0 : now + duration;
                    }
                    try {
                        while (!waiter.notified && !(cancelled && cancelled->cancelled())) {
                            int64_t remaining = -1;
                            bool polling = false;
                            if (timeout_value >= 0) {
                                uint64_t now = runtime_monotonic_nanoseconds();
                                if (now >= deadline) {
                                    timed_out = true;
                                    break;
                                }
                                uint64_t delta = deadline - now;
                                uint64_t maximum_duration = ((uint64_t)1 << 63) - 1;
                                remaining = delta > maximum_duration ? (int64_t)maximum_duration
                                                                     : (int64_t)delta;
                            }
                            if (cancelled && (remaining < 0 || remaining > 50000000)) {
                                remaining = 50000000;
                                polling = true;
                            }
                            bool signalled = waiter.condition.wait(m->mutex, remaining);
                            if (!signalled && timeout_value >= 0 && !polling) {
                                timed_out = true;
                                break;
                            }
                        }
                        notified = waiter.notified;
                    } catch (...) {
                        queue->waiters.erase(
                            std::remove(queue->waiters.begin(), queue->waiters.end(), &waiter),
                            queue->waiters.end());
                        if (queue->waiters.empty()) {
                            m->waiters.erase(effective);
                            delete queue;
                        }
                        throw;
                    }
                    queue->waiters.erase(
                        std::remove(queue->waiters.begin(), queue->waiters.end(), &waiter),
                        queue->waiters.end());
                    if (queue->waiters.empty()) {
                        m->waiters.erase(effective);
                        delete queue;
                    }
                    if (cancelled && cancelled->cancelled())
                        throw std::runtime_error("guest thread cancelled");
                    s->stack.push_back(RuntimeValue::i32(notified ? 0u : timed_out ? 2u : 2u));
                }
            } else if (load) {
                s->stack.push_back(RuntimeValue(value_type, old));
            } else if (store) {
                for (unsigned q = 0; q < width; ++q)
                    m->bytes[(std::size_t)effective + q] = (unsigned char)(value.bits >> (q * 8));
            } else {
                uint64_t mask = width == 8 ? ~(uint64_t)0 : (((uint64_t)1 << (width * 8)) - 1);
                uint64_t operand = value.bits & mask;
                uint64_t result = old;
                if (operation == 0)
                    result = old + operand;
                else if (operation == 1)
                    result = old - operand;
                else if (operation == 2)
                    result = old & operand;
                else if (operation == 3)
                    result = old | operand;
                else if (operation == 4)
                    result = old ^ operand;
                else if (operation == 5)
                    result = operand;
                else
                    result = old == (expected.bits & mask) ? replacement.bits : old;
                result &= mask;
                for (unsigned q = 0; q < width; ++q)
                    m->bytes[(std::size_t)effective + q] = (unsigned char)(result >> (q * 8));
                s->stack.push_back(RuntimeValue(value_type, old));
            }
            break;
        }
        default:
            if (in.opcode >= 0x28 && in.opcode <= 0x3e) {
                if (impl->memories.empty())
                    throw std::runtime_error("memory index out of bounds");
                RuntimeMemory *m = impl->memories[0];
                RuntimeLock lock(m->mutex);
                unsigned bytes = (in.opcode == 0x28 || in.opcode == 0x2a || in.opcode == 0x36 ||
                                  in.opcode == 0x38)
                                     ? 4
                                 : (in.opcode == 0x29 || in.opcode == 0x2b || in.opcode == 0x37 ||
                                    in.opcode == 0x39)
                                     ? 8
                                 : (in.opcode == 0x2c || in.opcode == 0x2d || in.opcode == 0x30 ||
                                    in.opcode == 0x31 || in.opcode == 0x3a || in.opcode == 0x3c)
                                     ? 1
                                 : (in.opcode == 0x2e || in.opcode == 0x2f || in.opcode == 0x32 ||
                                    in.opcode == 0x33 || in.opcode == 0x3b || in.opcode == 0x3d)
                                     ? 2
                                     : 4;
                uint64_t addr;
                if (in.opcode >= 0x36) {
                    RuntimeValue v = pop(*s);
                    ValueType vt = (in.opcode == 0x36 || in.opcode == 0x3a || in.opcode == 0x3b)
                                       ? TYPE_I32
                                   : (in.opcode == 0x37 || in.opcode >= 0x3c) ? TYPE_I64
                                   : in.opcode == 0x38                        ? TYPE_F32
                                                                              : TYPE_F64;
                    expect_type(v, vt);
                    uint32_t base = u32(pop(*s));
                    addr = (uint64_t)base + in.secondary;
                    if (addr + bytes > m->bytes.size())
                        throw std::runtime_error("out of bounds memory access");
                    uint64_t x = v.bits;
                    for (unsigned q = 0; q < bytes; ++q)
                        m->bytes[(std::size_t)addr + q] = (unsigned char)(x >> (q * 8));
                } else {
                    uint32_t base = u32(pop(*s));
                    addr = (uint64_t)base + in.secondary;
                    if (addr + bytes > m->bytes.size())
                        throw std::runtime_error("out of bounds memory access");
                    uint64_t x = 0;
                    for (unsigned q = 0; q < bytes; ++q)
                        x |= (uint64_t)m->bytes[(std::size_t)addr + q] << (q * 8);
                    bool sign = in.opcode == 0x2c || in.opcode == 0x2e || in.opcode == 0x30 ||
                                in.opcode == 0x32 || in.opcode == 0x34;
                    if (sign && bytes < 8 && (x & ((uint64_t)1 << (bytes * 8 - 1))))
                        x |= (~(uint64_t)0) << (bytes * 8);
                    if (in.opcode == 0x2a)
                        s->stack.push_back(RuntimeValue(TYPE_F32, x));
                    else if (in.opcode == 0x2b)
                        s->stack.push_back(RuntimeValue(TYPE_F64, x));
                    else if (in.opcode == 0x29 || in.opcode >= 0x30)
                        s->stack.push_back(RuntimeValue::i64(x));
                    else
                        s->stack.push_back(RuntimeValue::i32((uint32_t)x));
                }
                break;
            }
            if (in.opcode >= 0x45 && in.opcode <= 0xbf) {
                if (in.opcode >= 0xa7) {
                    RuntimeValue v = pop(*s);
                    switch (in.opcode) {
                    case 0xa7:
                        s->stack.push_back(RuntimeValue::i32((uint32_t)u64(v)));
                        break;
                    case 0xac:
                        s->stack.push_back(RuntimeValue::i64((uint64_t)(int64_t)i32(v)));
                        break;
                    case 0xad:
                        s->stack.push_back(RuntimeValue::i64(u32(v)));
                        break;
                    case 0xb2:
                        s->stack.push_back(RuntimeValue::f32((float)i32(v)));
                        break;
                    case 0xb3:
                        s->stack.push_back(RuntimeValue::f32((float)u32(v)));
                        break;
                    case 0xb4:
                        s->stack.push_back(RuntimeValue::f32((float)i64(v)));
                        break;
                    case 0xb5:
                        s->stack.push_back(RuntimeValue::f32((float)u64(v)));
                        break;
                    case 0xb6:
                        s->stack.push_back(RuntimeValue::f32((float)f64(v)));
                        break;
                    case 0xb7:
                        s->stack.push_back(RuntimeValue::f64((double)i32(v)));
                        break;
                    case 0xb8:
                        s->stack.push_back(RuntimeValue::f64((double)u32(v)));
                        break;
                    case 0xb9:
                        s->stack.push_back(RuntimeValue::f64((double)i64(v)));
                        break;
                    case 0xba:
                        s->stack.push_back(RuntimeValue::f64((double)u64(v)));
                        break;
                    case 0xbb:
                        s->stack.push_back(RuntimeValue::f64((double)f32(v)));
                        break;
                    case 0xbc:
                        expect_type(v, TYPE_F32);
                        s->stack.push_back(RuntimeValue(TYPE_I32, v.bits));
                        break;
                    case 0xbd:
                        expect_type(v, TYPE_F64);
                        s->stack.push_back(RuntimeValue(TYPE_I64, v.bits));
                        break;
                    case 0xbe:
                        expect_type(v, TYPE_I32);
                        s->stack.push_back(RuntimeValue(TYPE_F32, v.bits));
                        break;
                    case 0xbf:
                        expect_type(v, TYPE_I64);
                        s->stack.push_back(RuntimeValue(TYPE_F64, v.bits));
                        break;
                    case 0xa8:
                    case 0xa9:
                    case 0xaa:
                    case 0xab:
                    case 0xae:
                    case 0xaf:
                    case 0xb0:
                    case 0xb1: {
                        bool source_f32 = in.opcode == 0xa8 || in.opcode == 0xa9 ||
                                          in.opcode == 0xae || in.opcode == 0xaf;
                        double d = source_f32 ? (double)f32(v) : f64(v);
                        if (!finite_d(d))
                            throw std::runtime_error("invalid conversion to integer");
                        bool wide = in.opcode >= 0xae;
                        bool uns = (in.opcode == 0xa9 || in.opcode == 0xab || in.opcode == 0xaf ||
                                    in.opcode == 0xb1);
                        double t = d < 0.0 ? std::ceil(d) : std::floor(d);
                        if (wide) {
                            if (uns) {
                                if (t < 0.0 || t >= 18446744073709551616.0)
                                    throw std::runtime_error("integer conversion overflow");
                                s->stack.push_back(RuntimeValue::i64((uint64_t)t));
                            } else {
                                if (t < -9223372036854775808.0 || t >= 9223372036854775808.0)
                                    throw std::runtime_error("integer conversion overflow");
                                s->stack.push_back(RuntimeValue::i64((uint64_t)(int64_t)t));
                            }
                        } else {
                            if (uns) {
                                if (t < 0.0 || t >= 4294967296.0)
                                    throw std::runtime_error("integer conversion overflow");
                                s->stack.push_back(RuntimeValue::i32((uint32_t)t));
                            } else {
                                if (t < -2147483648.0 || t >= 2147483648.0)
                                    throw std::runtime_error("integer conversion overflow");
                                s->stack.push_back(RuntimeValue::i32((uint32_t)(int32_t)t));
                            }
                        }
                        break;
                    }
                    default:
                        throw std::runtime_error("unsupported numeric instruction");
                    }
                } else
                    s->stack.push_back(numeric(in.opcode, *s));
                break;
            }
            throw std::runtime_error("unsupported runtime instruction");
        }
        return s->pc;
    } catch (const std::exception &e) {
        frame->failed = true;
        try {
            frame->error = e.what();
        } catch (...) {
        }
        frame->finished = true;
        return invalid_pc;
    } catch (...) {
        frame->failed = true;
        try {
            frame->error = "unknown runtime failure";
        } catch (...) {
        }
        frame->finished = true;
        return invalid_pc;
    }
}

static RuntimeValue zero_value(ValueType t) {
    return RuntimeValue(t, 0);
}
static bool invoke_internal(RuntimeInstance::Impl *impl, uint32_t fi, const RuntimeValue *args,
                            std::size_t argument_count, RuntimeValue &result,
                            ExecutionContext &execution) {
    if (fi >= impl->mod.functions.size())
        throw std::runtime_error("invalid function index");
    const Function &fn = impl->mod.functions[fi];
    if (fn.type_index >= impl->mod.types.size())
        throw std::runtime_error("invalid function type");
    const FunctionType &ft = impl->mod.types[fn.type_index];
    if (ft.results.size() > 1)
        throw std::runtime_error("MVP functions may return at most one value");
    if (argument_count != ft.parameters.size())
        throw std::runtime_error("argument count mismatch");
    for (std::size_t i = 0; i < argument_count; ++i)
        expect_type(args[i], ft.parameters[i]);
    if (fn.imported) {
        std::map<RuntimeImportKey, RuntimeFunctionImport>::const_iterator it =
            impl->imports.functions.find(resource_key(fn.import_module, fn.import_name));
        if (it == impl->imports.functions.end() || !it->second.callback)
            throw std::runtime_error("unresolved function import '" + fn.import_module + "." +
                                     fn.import_name + "'");
        std::vector<RuntimeValue> arguments;
        if (argument_count)
            arguments.assign(args, args + argument_count);
        const std::vector<RuntimeValue> values = it->second.callback(it->second.context, arguments);
        if (values.size() != ft.results.size())
            throw std::runtime_error("host function result count mismatch");
        for (std::size_t i = 0; i < values.size(); ++i)
            expect_type(values[i], ft.results[i]);
        if (!values.empty())
            result = values[0];
        return !values.empty();
    }
    if (execution.depth >= impl->options.stack_limit)
        throw std::runtime_error("call stack exhausted");
    CallDepth depth(execution.depth);
    State state;
    state.stack.reserve(impl->stack_limits[fi]);
    state.execution = &execution;
    state.locals.reserve(argument_count + fn.locals.size());
    for (std::size_t i = 0; i < argument_count; ++i)
        state.locals.push_back(args[i]);
    for (std::size_t i = 0; i < fn.locals.size(); ++i)
        state.locals.push_back(zero_value(fn.locals[i].type));
    if (fn.code.empty()) {
        throw std::runtime_error("function has no code");
    }
    Control function;
    function.start = invalid_pc;
    function.end = (uint32_t)(fn.code.size() - 1);
    function.after = (uint32_t)fn.code.size();
    function.kind = 0xff;
    function.height = 0;
    function.result = ft.results.empty() ? TYPE_NONE : ft.results[0];
    state.controls.push_back(function);
    RuntimeFrame frame(impl->owner, fi, &state);
    if (impl->executor)
        impl->executor->execute(frame);
    else
        runtime_interpret(&frame);
    if (!frame.finished && !frame.failed)
        throw std::runtime_error("runtime executor returned before the function finished");
    if (frame.failed)
        throw std::runtime_error(frame.error.empty() ? "WebAssembly execution trapped"
                                                     : frame.error);
    if (ft.results.size() == 1) {
        if (state.stack.empty())
            throw std::runtime_error("function did not produce a result");
        RuntimeValue v = state.stack.back();
        expect_type(v, ft.results[0]);
        result = v;
    } else if (!state.stack.empty())
        throw std::runtime_error("unexpected function result");
    return !ft.results.empty();
}

RuntimeInstance::RuntimeInstance(const Module &m, const RuntimeOptions &o)
    : impl_(new Impl(m, o, 0)) {
    impl_->owner = this;
}
RuntimeInstance::RuntimeInstance(const Module &m, const RuntimeOptions &o, const RuntimeImports &im)
    : impl_(new Impl(m, o, &im)) {
    impl_->owner = this;
}
RuntimeInstance::~RuntimeInstance() {
    delete impl_;
}
void RuntimeInstance::set_executor(RuntimeExecutor *e) {
    impl_->executor = e;
}
void RuntimeInstance::set_import(const std::string &m, const std::string &n,
                                 const RuntimeFunctionImport &f) {
    impl_->imports.functions[resource_key(m, n)] = f;
}
RuntimeMemory *RuntimeInstance::memory(uint32_t i) {
    return i < impl_->memories.size() ? impl_->memories[i] : 0;
}
RuntimeTable *RuntimeInstance::table(uint32_t i) {
    return i < impl_->tables.size() ? impl_->tables[i] : 0;
}
RuntimeGlobal *RuntimeInstance::global(uint32_t i) {
    return i < impl_->globals.size() ? impl_->globals[i] : 0;
}
const Module &RuntimeInstance::module() const {
    return impl_->mod;
}

void RuntimeInstance::initialize() {
    if (impl_->initialized)
        return;
    if (impl_->initialization_started)
        throw std::runtime_error("previous runtime initialization failed");
    impl_->initialization_started = true;
    const Module &m = impl_->mod;
    for (std::size_t i = 0; i < m.functions.size(); ++i)
        if (m.functions[i].imported) {
            const Function &f = m.functions[i];
            std::map<RuntimeImportKey, RuntimeFunctionImport>::const_iterator it =
                impl_->imports.functions.find(resource_key(f.import_module, f.import_name));
            if (it == impl_->imports.functions.end() || !it->second.callback)
                throw std::runtime_error("unresolved function import '" + f.import_module + "." +
                                         f.import_name + "'");
        }
    for (std::size_t i = 0; i < m.memories.size(); ++i) {
        const Memory &d = m.memories[i];
        RuntimeMemory *p = 0;
        if (d.imported) {
            std::map<RuntimeImportKey, RuntimeMemory *>::const_iterator it =
                impl_->imports.memories.find(resource_key(d.import_module, d.import_name));
            if (it == impl_->imports.memories.end() || !it->second)
                throw std::runtime_error("unresolved memory import '" + d.import_module + "." +
                                         d.import_name + "'");
            p = it->second;
        } else {
            std::auto_ptr<RuntimeMemory> owned(new RuntimeMemory);
            impl_->owned_memories.push_back(owned.get());
            p = owned.release();
            p->has_maximum = d.limits.has_maximum;
            p->maximum_pages = d.limits.maximum;
            p->shared = d.shared;
            uint32_t cap = impl_->options.memory_limit_pages < 65536
                               ? impl_->options.memory_limit_pages
                               : 65536;
            if (d.limits.minimum > cap)
                throw std::runtime_error("memory minimum exceeds runtime limit");
            p->bytes.resize((std::size_t)d.limits.minimum * 65536);
        }
        RuntimeLock memory_lock(p->mutex);
        if (p->bytes.size() % 65536)
            throw std::runtime_error("imported memory size is not page aligned");
        uint32_t pages = (uint32_t)(p->bytes.size() / 65536);
        uint32_t cap =
            impl_->options.memory_limit_pages < 65536 ? impl_->options.memory_limit_pages : 65536;
        if (p->shared != d.shared || pages > cap || pages < d.limits.minimum ||
            (p->has_maximum && (p->maximum_pages < pages || p->maximum_pages > 65536)) ||
            (d.limits.has_maximum && pages > d.limits.maximum) ||
            (d.limits.has_maximum && (!p->has_maximum || p->maximum_pages > d.limits.maximum)))
            throw std::runtime_error("imported memory limits do not match");
        impl_->memories.push_back(p);
    }
    for (std::size_t i = 0; i < m.tables.size(); ++i) {
        const Table &d = m.tables[i];
        RuntimeTable *p = 0;
        if (d.imported) {
            std::map<RuntimeImportKey, RuntimeTable *>::const_iterator it =
                impl_->imports.tables.find(resource_key(d.import_module, d.import_name));
            if (it == impl_->imports.tables.end() || !it->second)
                throw std::runtime_error("unresolved table import '" + d.import_module + "." +
                                         d.import_name + "'");
            p = it->second;
        } else {
            std::auto_ptr<RuntimeTable> owned(new RuntimeTable);
            impl_->owned_tables.push_back(owned.get());
            p = owned.release();
            p->functions.resize(d.limits.minimum);
            p->has_maximum = d.limits.has_maximum;
            p->maximum_size = d.limits.maximum;
        }
        RuntimeLock table_lock(p->mutex);
        if (p->functions.size() < d.limits.minimum ||
            (p->has_maximum && p->maximum_size < p->functions.size()) ||
            (d.limits.has_maximum && p->functions.size() > d.limits.maximum) ||
            (d.limits.has_maximum && (!p->has_maximum || p->maximum_size > d.limits.maximum)))
            throw std::runtime_error("imported table limits do not match");
        impl_->tables.push_back(p);
    }
    for (std::size_t i = 0; i < m.globals.size(); ++i) {
        const Global &d = m.globals[i];
        RuntimeGlobal *p = 0;
        if (d.imported) {
            std::map<RuntimeImportKey, RuntimeGlobal *>::const_iterator it =
                impl_->imports.globals.find(resource_key(d.import_module, d.import_name));
            if (it == impl_->imports.globals.end() || !it->second)
                throw std::runtime_error("unresolved global import '" + d.import_module + "." +
                                         d.import_name + "'");
            p = it->second;
            if (p->value.type != d.type || p->mutable_ != d.mutable_)
                throw std::runtime_error("imported global type mismatch");
        } else {
            std::auto_ptr<RuntimeGlobal> owned(new RuntimeGlobal);
            impl_->owned_globals.push_back(owned.get());
            p = owned.release();
            p->mutable_ = d.mutable_;
            p->value = zero_value(d.type);
        }
        impl_->globals.push_back(p);
    }
    for (std::size_t i = 0; i < m.globals.size(); ++i)
        if (!m.globals[i].imported) {
            RuntimeValue value = eval_const(m.globals[i].init, impl_->globals);
            if (value.type != m.globals[i].type)
                throw std::runtime_error("global initializer type mismatch");
            RuntimeGlobal *global = impl_->globals[i];
            RuntimeLock lock(global->mutex);
            global->value = value;
        }
    std::vector<uint32_t> doff(m.data.size()), eoff(m.elements.size());
    for (std::size_t i = 0; i < m.data.size(); ++i) {
        const DataSegment &d = m.data[i];
        if (d.memory_index >= impl_->memories.size())
            throw std::runtime_error("data memory index out of bounds");
        RuntimeValue v = eval_const(d.offset, impl_->globals);
        if (v.type != TYPE_I32)
            throw std::runtime_error("data offset is not i32");
        doff[i] = v.as_u32();
        RuntimeMemory *memory = impl_->memories[d.memory_index];
        RuntimeLock lock(memory->mutex);
        if ((uint64_t)doff[i] + d.bytes.size() > memory->bytes.size())
            throw std::runtime_error("active data segment out of bounds");
    }
    for (std::size_t i = 0; i < m.elements.size(); ++i) {
        const ElementSegment &e = m.elements[i];
        if (e.table_index >= impl_->tables.size())
            throw std::runtime_error("element table index out of bounds");
        RuntimeValue v = eval_const(e.offset, impl_->globals);
        if (v.type != TYPE_I32)
            throw std::runtime_error("element offset is not i32");
        eoff[i] = v.as_u32();
        RuntimeTable *table = impl_->tables[e.table_index];
        RuntimeLock lock(table->mutex);
        if ((uint64_t)eoff[i] + e.functions.size() > table->functions.size())
            throw std::runtime_error("active element segment out of bounds");
        for (std::size_t j = 0; j < e.functions.size(); ++j)
            if (e.functions[j] >= m.functions.size())
                throw std::runtime_error("element function index out of bounds");
    }
    for (std::size_t i = 0; i < m.data.size(); ++i) {
        const DataSegment &d = m.data[i];
        RuntimeMemory *p = impl_->memories[d.memory_index];
        RuntimeLock lock(p->mutex);
        for (std::size_t j = 0; j < d.bytes.size(); ++j)
            p->bytes[doff[i] + j] = (unsigned char)d.bytes[j];
    }
    for (std::size_t i = 0; i < m.elements.size(); ++i) {
        const ElementSegment &e = m.elements[i];
        RuntimeTable *p = impl_->tables[e.table_index];
        RuntimeLock lock(p->mutex);
        for (std::size_t j = 0; j < e.functions.size(); ++j)
            p->functions[eoff[i] + j] = RuntimeFunctionRef(this, e.functions[j]);
    }
    if (impl_->options.run_start && m.has_start) {
        RuntimeValue r;
        ExecutionContext execution(impl_->options.fuel, runtime_current_thread_id(),
                                   runtime_current_cancel_flag(), runtime_current_fuel_budget());
        invoke_internal(impl_, m.start, 0, 0, r, execution);
    }
    impl_->initialized = true;
}

std::vector<RuntimeValue> RuntimeInstance::invoke_export(const std::string &name,
                                                         const std::vector<RuntimeValue> &args) {
    for (std::size_t i = 0; i < impl_->mod.exports.size(); ++i)
        if (impl_->mod.exports[i].name == name) {
            if (impl_->mod.exports[i].kind != 0)
                throw std::runtime_error("export is not a function");
            return invoke(impl_->mod.exports[i].index, args);
        }
    throw std::runtime_error("unknown export '" + name + "'");
}
std::vector<RuntimeValue> RuntimeInstance::invoke(uint32_t f,
                                                  const std::vector<RuntimeValue> &args) {
    if (!impl_->initialized)
        initialize();
    std::vector<RuntimeValue> r;
    ExecutionContext execution(impl_->options.fuel, runtime_current_thread_id(),
                               runtime_current_cancel_flag(), runtime_current_fuel_budget());
    RuntimeValue value;
    if (invoke_internal(impl_, f, args.empty() ? 0 : &args[0], args.size(), value, execution))
        r.push_back(value);
    return r;
}

std::string format_runtime_value(const RuntimeValue &v, bool raw) {
    std::ostringstream s;
    switch (v.type) {
    case TYPE_I32:
        if (!raw)
            s << "i32:";
        s << v.as_i32();
        break;
    case TYPE_I64:
        if (!raw)
            s << "i64:";
        s << v.as_i64();
        break;
    case TYPE_F32:
        if (!raw)
            s << "f32:";
        s << std::setprecision(9) << v.as_f32();
        break;
    case TYPE_F64:
        if (!raw)
            s << "f64:";
        s << std::setprecision(17) << v.as_f64();
        break;
    default:
        s << "?";
    }
    return s.str();
}
}
