#ifndef WATX_RUNTIME_VALUE_STACK_H
#define WATX_RUNTIME_VALUE_STACK_H
#include <algorithm>
#include <stdexcept>
#include <watx/runtime/native_frame.h>
#include <watx/runtime/runtime.h>
namespace watx {
class ValueStack {
    RuntimeNativeFrame &native_;
    RuntimeValue inline_[16];
    ValueStack(const ValueStack &);
    ValueStack &operator=(const ValueStack &);

  public:
    explicit ValueStack(RuntimeNativeFrame &native) : native_(native) {
        native_.stack = inline_;
        native_.size = 0;
        native_.capacity = 16;
        native_.locals = 0;
    }
    ~ValueStack() {
        if (native_.stack != inline_)
            delete[] native_.stack;
    }
    std::size_t size() const {
        return native_.size;
    }
    bool empty() const {
        return native_.size == 0;
    }
    RuntimeValue &back() {
        return native_.stack[native_.size - 1];
    }
    const RuntimeValue &back() const {
        return native_.stack[native_.size - 1];
    }
    void clear() {
        native_.size = 0;
    }
    void pop_back() {
        --native_.size;
    }
    void reserve(std::size_t capacity) {
        if (capacity <= native_.capacity)
            return;
        if (capacity > static_cast<std::size_t>(-1) / sizeof(RuntimeValue))
            throw std::length_error("operand stack capacity overflow");
        RuntimeValue *values = new RuntimeValue[capacity];
        std::copy(native_.stack, native_.stack + native_.size, values);
        if (native_.stack != inline_)
            delete[] native_.stack;
        native_.stack = values;
        native_.capacity = capacity;
    }
    void push_back(const RuntimeValue &value) {
        const RuntimeValue copy = value;
        if (native_.size == native_.capacity) {
            if (native_.capacity > static_cast<std::size_t>(-1) / 2)
                throw std::length_error("operand stack capacity overflow");
            reserve(native_.capacity * 2);
        }
        native_.stack[native_.size++] = copy;
    }
    void resize(std::size_t size) {
        if (size > native_.size) {
            reserve(size);
            std::fill(native_.stack + native_.size, native_.stack + size, RuntimeValue());
        }
        native_.size = size;
    }
};
}
#endif
