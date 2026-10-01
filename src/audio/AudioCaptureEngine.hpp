#pragma once

#include "../core/AudioFramePool.hpp"
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <iostream>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324)
#endif

namespace ai_studio::audio {

struct AudioConfig {
    uint32_t sample_rate = 48000; // Professional studio-grade 48kHz
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

    void set_input_gain(float gain) noexcept;
    [[nodiscard]] float get_input_gain() const noexcept { 
        return input_gain_.load(std::memory_order_relaxed); 
    }

    [[nodiscard]] bool push_captured_chunk(const float* src_samples, size_t num_samples, uint64_t timestamp_us) noexcept {
        if (!is_running_.load(std::memory_order_relaxed) || !src_samples) [[unlikely]] {
            return false;
        }

        core::AudioFrame* frame = frame_pool_.acquire_free_frame();
        if (!frame) [[unlikely]] {
            ++dropped_frames_count_;
            if (dropped_frames_count_.load(std::memory_order_relaxed) % 100 == 1) {
                std::cerr << "[WARN][src/audio/AudioCaptureEngine.hpp::push_captured_chunk] Frame pool exhausted! Dropped "
                          << dropped_frames_count_.load(std::memory_order_relaxed) << " audio frames.\n";
            }
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
            const uint64_t total = ++captured_chunks_count_;
            if (total == 1 || total % 300 == 0) {
                std::cout << "[DEBUG][src/audio/AudioCaptureEngine.hpp::push_captured_chunk] Captured 48kHz Audio Chunk #"
                          << total << " (" << copy_count << " samples | ts=" << timestamp_us << " us)\n";
            }
            return true;
        }
        
        frame_pool_.release_frame(frame);
        return false;
    }

private:
    core::AudioFramePool& frame_pool_;
    AudioConfig config_;
    alignas(64) std::atomic<float> input_gain_{1.0f};
    alignas(64) std::atomic<bool> is_running_{false};
    alignas(64) std::atomic<uint64_t> captured_chunks_count_{0};
    alignas(64) std::atomic<uint64_t> dropped_frames_count_{0};
};

} // namespace ai_studio::audio

#if defined(_MSC_VER)
#pragma warning(pop)
#endif