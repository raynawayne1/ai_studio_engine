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
        
        // Pre-allocate all physical memory up front during application startup.
        storage_.reserve(pool_size);
        for (size_t i = 0; i < pool_size; ++i) {
            storage_.push_back(std::make_unique<AudioFrame>(samples_per_frame));
            
            // Populate the free queue with lightweight raw pointers.
            free_queue_.push(storage_.back().get());
        }
    }

    // Prevent copying of the pool
    AudioFramePool(const AudioFramePool&) = delete;
    AudioFramePool& operator=(const AudioFramePool&) = delete;

    // 1. Microphone Thread asks for an empty frame to fill (noexcept for zero overhead)
    [[nodiscard]] AudioFrame* acquire_free_frame() noexcept {
        AudioFrame* frame = nullptr;
        if (free_queue_.pop(frame)) {
            return frame;
        }
        return nullptr; // Pool exhausted (Audio underrun guard)
    }

    // 2. Microphone Thread pushes the filled frame to the AI consumer
    bool push_ready_frame(AudioFrame* frame) noexcept {
        return ready_queue_.push(frame);
    }

    // 3. AI Thread grabs the ready frame to perform Voice Conversion / Inference
    [[nodiscard]] AudioFrame* acquire_ready_frame() noexcept {
        AudioFrame* frame = nullptr;
        if (ready_queue_.pop(frame)) {
            return frame;
        }
        return nullptr; // No frame waiting
    }

    // 4. AI Thread finishes processing and instantly returns the frame to the free pool
    void release_frame(AudioFrame* frame) noexcept {
        if (frame) {
            frame->reset();
            free_queue_.push(frame);
        }
    }

private:
    // Safe primary owner of the physical memory blocks.
    std::vector<std::unique_ptr<AudioFrame>> storage_;

    // Blazing-fast lock-free ring buffers passing only 8-byte raw pointers.
    LockFreeQueue<AudioFrame*> free_queue_;
    LockFreeQueue<AudioFrame*> ready_queue_;
};

} // namespace ai_studio::core