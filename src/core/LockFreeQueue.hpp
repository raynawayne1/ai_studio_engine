#pragma once

#include <atomic>
#include <vector>
#include <cstddef>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324)
#endif

namespace ai_studio::core {

/**
 * Ultra-fast, Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer.
 * Uses power-of-two bitwise masking (& mask_) instead of integer division (%)
 * for single-cycle throughput and zero slot loss.
 */
template<typename T>
class LockFreeQueue {
public:
    explicit LockFreeQueue(size_t min_capacity)
        : capacity_(next_power_of_two(min_capacity + 1)),
          mask_(capacity_ - 1),
          buffer_(capacity_) {}

    // Called ONLY by the Producer thread
    bool push(const T& item) noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) & mask_;
        
        if (next_tail != head_.load(std::memory_order_acquire)) {
            buffer_[current_tail] = item;
            tail_.store(next_tail, std::memory_order_release);
            return true;
        }
        return false; // Queue is completely full
    }

    // Called ONLY by the Consumer thread
    bool pop(T& out_item) noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false; // Queue is completely empty
        }
        
        out_item = buffer_[current_head];
        head_.store((current_head + 1) & mask_, std::memory_order_release);
        return true;
    }

private:
    static constexpr size_t next_power_of_two(size_t n) noexcept {
        size_t p = 2;
        while (p < n) {
            p <<= 1;
        }
        return p;
    }

    size_t capacity_;
    size_t mask_;
    std::vector<T> buffer_;
    
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

} // namespace ai_studio::core

#ifdef _MSC_VER
#pragma warning(pop)
#endif