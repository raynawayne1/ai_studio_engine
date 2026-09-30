#pragma once

#include "../core/AudioFramePool.hpp"
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstring>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324)
#endif

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

    AudioCaptureEngine(const AudioCaptureEngine&) = delete;
    AudioCaptureEngine& operator=(const AudioCaptureEngine&) = delete;
    AudioCaptureEngine(AudioCaptureEngine&&) = delete;
    AudioCaptureEngine& operator=(AudioCaptureEngine&&) = delete;

    [[nodiscard]] bool start() noexcept;
    void stop() noexcept;

    [[nodiscard]] bool is_running() const noexcept { 
        return is_running_.load(std::memory_order_relaxed); 
    }
    
    [[nodiscard]] const AudioConfig& get_config() const noexcept { 
        return config_; 
    }

    void set_input_gain(float gain) noexcept {
        input_gain_.store(gain, std::memory_order_relaxed);
    }
    [[nodiscard]] float get_input_gain() const noexcept {
        return input_gain_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool push_captured_chunk(const float* src_samples, size_t num_samples, uint64_t timestamp_us) noexcept {
        if (!is_running_.load(std::memory_order_relaxed) || !src_samples) [[unlikely]] {
            return false;
        }

        core::AudioFrame* frame = frame_pool_.acquire_free_frame();
        if (!frame) [[unlikely]] {
            return false;
        }

        const size_t copy_count = (num_samples < frame->samples.size()) ? num_samples : frame->samples.size();
        const float gain = input_gain_.load(std::memory_order_relaxed);

        if (gain == 1.0f) {
            std::memcpy(frame->samples.data(), src_samples, copy_count * sizeof(float));
        } else {
            float* dst = frame->samples.data();
            for (size_t i = 0; i < copy_count; ++i) {
                dst[i] = src_samples[i] * gain;
            }
        }

        frame->timestamp_us = timestamp_us;
        frame->is_valid = true;

        if (frame_pool_.push_ready_frame(frame)) [[likely]] {
            return true;
        }
        
        // Prevent frame leak if ready queue is temporarily saturated
        frame_pool_.release_frame(frame);
        return false;
    }

private:
    core::AudioFramePool& frame_pool_;
    AudioConfig config_;
    alignas(64) std::atomic<float> input_gain_{1.0f};
    alignas(64) std::atomic<bool> is_running_{false};
};

} // namespace ai_studio::audio

#if defined(_MSC_VER)
#pragma warning(pop)
#endif