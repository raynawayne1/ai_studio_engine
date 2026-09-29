#pragma once

#include <atomic>
#include <vector>
#include <cstddef>

namespace ai_studio::core {

/**
 * Ultra-fast, Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer.
 * Crucial for real-time audio/video pipelines. Guarantees zero locks, 
 * zero mutexes, and zero dynamic allocations during real-time processing.
 */
template<typename T>
class LockFreeQueue {
public:
    // Pre-allocates the buffer memory during setup so the real-time thread never allocates
    explicit LockFreeQueue(size_t capacity) 
        : capacity_(capacity), buffer_(capacity) {}

    // Called ONLY by the Producer thread (e.g., Microphone capture)
    bool push(const T& item) noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = increment(current_tail);
        
        if (next_tail != head_.load(std::memory_order_acquire)) {
            buffer_[current_tail] = item;
            tail_.store(next_tail, std::memory_order_release);
            return true;
        }
        return false; // Queue is completely full
    }

    // Called ONLY by the Consumer thread (e.g., AI Inference or Virtual Driver)
    bool pop(T& out_item) noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false; // Queue is completely empty
        }
        
        out_item = buffer_[current_head];
        head_.store(increment(current_head), std::memory_order_release);
        return true;
    }

private:
    size_t increment(size_t index) const noexcept {
        return (index + 1) % capacity_;
    }

    size_t capacity_;
    std::vector<T> buffer_;
    
    // alignas(64) prevents CPU Cache False Sharing by ensuring 
    // head_ and tail_ live on completely different CPU cache lines.
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

} // namespace ai_studio::core