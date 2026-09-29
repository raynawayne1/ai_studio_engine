#pragma once

#include "LockFreeQueue.hpp"
#include "AudioFrame.hpp"
#include <memory>
#include <vector>

namespace ai_studio::core {

class AudioFramePool {
public:
    AudioFramePool(size_t pool_size, size_t samples_per_frame)
        : free_queue_(pool_size), ready_queue_(pool_size) {
        
        // Pre-allocate the physical memory exactly once at startup.
        storage_.reserve(pool_size);
        for (size_t i = 0; i < pool_size; ++i) {
            storage_.push_back(std::make_unique<AudioFrame>(samples_per_frame));
            
            // Push the raw, lightweight pointer into the free queue.
            free_queue_.push(storage_.back().get());
        }
    }

    // 1. Microphone Thread asks for an empty frame to fill
    [[nodiscard]] AudioFrame* acquire_free_frame() noexcept {
        AudioFrame* frame = nullptr;
        if (free_queue_.pop(frame)) {
            return frame;
        }
        return nullptr; // Pool is exhausted (Audio underrun)
    }

    // 2. Microphone Thread sends the filled frame to the AI
    bool push_ready_frame(AudioFrame* frame) noexcept {
        return ready_queue_.push(frame);
    }

    // 3. AI Thread grabs the frame to perform Voice Conversion
    [[nodiscard]] AudioFrame* acquire_ready_frame() noexcept {
        AudioFrame* frame = nullptr;
        if (ready_queue_.pop(frame)) {
            return frame;
        }
        return nullptr; // No audio waiting to be processed
    }

    // 4. AI Thread finishes, and returns the frame to the free pool for reuse
    void release_frame(AudioFrame* frame) noexcept {
        if (frame) {
            frame->reset();
            free_queue_.push(frame);
        }
    }

private:
    // The actual memory lives safely here.
    std::vector<std::unique_ptr<AudioFrame>> storage_;

    // We pass lightweight, 8-byte raw pointers between threads for absolute maximum speed.
    LockFreeQueue<AudioFrame*> free_queue_;
    LockFreeQueue<AudioFrame*> ready_queue_;
};

} // namespace ai_studio::core