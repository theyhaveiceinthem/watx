#ifndef WATX_RUNTIME_INLINE_VECTOR_H
#define WATX_RUNTIME_INLINE_VECTOR_H
#include <algorithm>
#include <cstddef>
#include <stdexcept>
namespace watx {
template <typename T, std::size_t Inline> class InlineVector {
    T inline_[Inline];
    T *data_;
    std::size_t size_, capacity_;
    InlineVector(const InlineVector &);
    InlineVector &operator=(const InlineVector &);

  public:
    InlineVector() : data_(inline_), size_(0), capacity_(Inline) {}
    ~InlineVector() {
        if (data_ != inline_)
            delete[] data_;
    }
    std::size_t size() const {
        return size_;
    }
    bool empty() const {
        return size_ == 0;
    }
    T &operator[](std::size_t index) {
        return data_[index];
    }
    T &back() {
        return data_[size_ - 1];
    }
    void pop_back() {
        --size_;
    }
    void clear() {
        size_ = 0;
    }
    void reserve(std::size_t count) {
        if (count <= capacity_)
            return;
        if (count > static_cast<std::size_t>(-1) / sizeof(T))
            throw std::length_error("runtime vector capacity overflow");
        T *values = new T[count];
        try {
            std::copy(data_, data_ + size_, values);
        } catch (...) {
            delete[] values;
            throw;
        }
        if (data_ != inline_)
            delete[] data_;
        data_ = values;
        capacity_ = count;
    }
    void push_back(const T &value) {
        T copy = value;
        if (size_ == capacity_) {
            if (capacity_ > static_cast<std::size_t>(-1) / 2)
                throw std::length_error("runtime vector capacity overflow");
            reserve(capacity_ * 2);
        }
        data_[size_++] = copy;
    }
    void resize(std::size_t count) {
        if (count > size_) {
            reserve(count);
            std::fill(data_ + size_, data_ + count, T());
        }
        size_ = count;
    }
};
}
#endif
