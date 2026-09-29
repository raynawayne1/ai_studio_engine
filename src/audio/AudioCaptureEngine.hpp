#pragma once

#include "core/AudioFramePool.hpp"
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace ai_studio::audio {

struct AudioConfig {
    uint32_t sample_rate = 48000; // Professional studio grade 48kHz
    uint32_t channels = 1;        // Mono capture for AI voice conversion
    uint32_t frame_size = 480;    // 10ms chunks at 48kHz for ultra-low latency
};

class AudioCaptureEngine {
public:
    explicit AudioCaptureEngine(core::AudioFramePool& frame_pool, AudioConfig config = {}) noexcept;
    ~AudioCaptureEngine() noexcept;

    // Prevent copying and moving
    AudioCaptureEngine(const AudioCaptureEngine&) = delete;
    AudioCaptureEngine& operator=(const AudioCaptureEngine&) = delete;
    AudioCaptureEngine(AudioCaptureEngine&&) = delete;
    AudioCaptureEngine& operator=(AudioCaptureEngine&&) = delete;

    // Starts capturing from the default system microphone (or fallback simulation)
    [[nodiscard]] bool start() noexcept;

    // Stops capture cleanly
    void stop() noexcept;

    [[nodiscard]] bool is_running() const noexcept { 
        return is_running_.load(std::memory_order_relaxed); 
    }
    
    [[nodiscard]] const AudioConfig& get_config() const noexcept { 
        return config_; 
    }

    /**
     * ULTRA-FAST REAL-TIME AUDIO INGESTION HOOK
     * Called directly inside native OS audio callbacks (CoreAudio/WASAPI/ALSA).
     * Copies raw audio data into our pre-allocated memory pool with ZERO heap allocations,
     * using compiler-optimized memory operations.
     */
    [[nodiscard]] bool push_captured_chunk(const float* src_samples, size_t num_samples, uint64_t timestamp_us) noexcept {
        if (!is_running_.load(std::memory_order_relaxed) || !src_samples) {
            return false;
        }

        // 1. Acquire an empty frame from the pool with zero allocations
        core::AudioFrame* frame = frame_pool_.acquire_free_frame();
        if (!frame) {
            return false; // Audio underrun guard (pool exhausted)
        }

        // 2. Ultra-fast bulk memory copy directly into pre-aligned vector buffer
        const size_t copy_count = (num_samples < frame->samples.size()) ? num_samples : frame->samples.size();
        std::memcpy(frame->samples.data(), src_samples, copy_count * sizeof(float));

        frame->timestamp_us = timestamp_us;
        frame->is_valid = true;

        // 3. Push immediately to the ready queue for AI processing
        return frame_pool_.push_ready_frame(frame);
    }

private:
    core::AudioFramePool& frame_pool_;
    AudioConfig config_;
    alignas(64) std::atomic<bool> is_running_{false};
    void* native_device_handle_ = nullptr;
};

} // namespace ai_studio::audio