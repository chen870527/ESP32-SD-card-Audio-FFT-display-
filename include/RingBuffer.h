#pragma once

#include <atomic>
#include <cstdint>

template <typename T, size_t Capacity>
class RingBuffer {
public:
    RingBuffer() : head_(0), tail_(0) {}

    bool push(const T& item) {
        size_t next_head = (head_.load(std::memory_order_relaxed) + 1) % Capacity;
        if (next_head == tail_.load(std::memory_order_acquire)) {
            return false; // Full
        }
        buffer_[head_.load(std::memory_order_relaxed)] = item;
        head_.store(next_head, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        size_t current_tail = tail_.load(std::memory_order_relaxed);
        if (current_tail == head_.load(std::memory_order_acquire)) {
            return false; // Empty
        }
        item = buffer_[current_tail];
        tail_.store((current_tail + 1) % Capacity, std::memory_order_release);
        return true;
    }

    size_t pushArray(const T* src, size_t count) {
        size_t written = 0;
        for (size_t i = 0; i < count; ++i) {
            if (!push(src[i])) break;
            written++;
        }
        return written;
    }

    size_t popArray(T* dst, size_t count) {
        size_t readCount = 0;
        for (size_t i = 0; i < count; ++i) {
            if (!pop(dst[i])) break;
            readCount++;
        }
        return readCount;
    }

    size_t available() const {
        size_t h = head_.load(std::memory_order_acquire);
        size_t t = tail_.load(std::memory_order_acquire);
        if (h >= t) {
            return h - t;
        } else {
            return Capacity - t + h;
        }
    }

    size_t availableForWrite() const {
        return (Capacity - 1) - available();
    }

private:
    T buffer_[Capacity];
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};
