#pragma once

#include "LockFreeQueue.hpp"
#include "AudioFrame.hpp"
#include <memory>
#include <vector>
#include <iostream>

namespace ai_studio::core {

class AudioFramePool {
public:
    AudioFramePool(size_t pool_size, size_t samples_per_frame)
        : free_queue_(pool_size), ready_queue_(pool_size) {
        
        storage_.reserve(pool_size);
        for (size_t i = 0; i < pool_size; ++i) {
            storage_.push_back(std::make_unique<AudioFrame>(samples_per_frame));
            (void)free_queue_.push(storage_.back().get());
        }
        std::cout << "[DEBUG][src/core/AudioFramePool.hpp::AudioFramePool] Pre-allocated "
                  << pool_size << " lock-free audio frames (" << samples_per_frame
                  << " samples/frame @ 48kHz, aligned to 32 bytes).\n";
    }

    AudioFramePool(const AudioFramePool&) = delete;
    AudioFramePool& operator=(const AudioFramePool&) = delete;

    [[nodiscard]] AudioFrame* acquire_free_frame() noexcept {
        AudioFrame* frame = nullptr;
        if (free_queue_.pop(frame)) {
            return frame;
        }
        return nullptr;
    }

    bool push_ready_frame(AudioFrame* frame) noexcept {
        return ready_queue_.push(frame);
    }

    [[nodiscard]] AudioFrame* acquire_ready_frame() noexcept {
        AudioFrame* frame = nullptr;
        if (ready_queue_.pop(frame)) {
            return frame;
        }
        return nullptr;
    }

    void release_frame(AudioFrame* frame) noexcept {
        if (frame) {
            frame->reset();
            (void)free_queue_.push(frame);
        }
    }

private:
    std::vector<std::unique_ptr<AudioFrame>> storage_;
    LockFreeQueue<AudioFrame*> free_queue_;
    LockFreeQueue<AudioFrame*> ready_queue_;
};

} // namespace ai_studio::core