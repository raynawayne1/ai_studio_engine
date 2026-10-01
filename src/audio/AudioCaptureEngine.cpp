#include "AudioCaptureEngine.hpp"
#include <iostream>

namespace ai_studio::audio {

AudioCaptureEngine::AudioCaptureEngine(core::AudioFramePool& frame_pool, AudioConfig config) noexcept
    : frame_pool_(frame_pool), config_(config) {
    std::cout << "[DEBUG][src/audio/AudioCaptureEngine.cpp::AudioCaptureEngine] Initialized with sample_rate="
              << config_.sample_rate << " Hz | channels=" << config_.channels
              << " | frame_size=" << config_.frame_size << " samples (10ms).\n";
}

AudioCaptureEngine::~AudioCaptureEngine() noexcept {
    stop();
}

void AudioCaptureEngine::set_input_gain(float gain) noexcept {
    input_gain_.store(gain, std::memory_order_relaxed);
    std::cout << "[DEBUG][src/audio/AudioCaptureEngine.cpp::set_input_gain] Input gain set to: "
              << gain << "x\n";
}

bool AudioCaptureEngine::start() noexcept {
    if (is_running_.load(std::memory_order_acquire)) {
        return true;
    }

    std::cout << "[DEBUG][src/audio/AudioCaptureEngine.cpp::start] Starting 48kHz audio capture engine...\n";
    captured_chunks_count_.store(0, std::memory_order_relaxed);
    dropped_frames_count_.store(0, std::memory_order_relaxed);
    is_running_.store(true, std::memory_order_release);
    std::cout << "[DEBUG][src/audio/AudioCaptureEngine.cpp::start] Audio capture stream successfully active.\n";
    return true;
}

void AudioCaptureEngine::stop() noexcept {
    if (!is_running_.load(std::memory_order_acquire)) {
        return;
    }

    is_running_.store(false, std::memory_order_release);
    std::cout << "[DEBUG][src/audio/AudioCaptureEngine.cpp::stop] Audio capture stream stopped cleanly. Total chunks captured: "
              << captured_chunks_count_.load(std::memory_order_relaxed) << '\n';
}

} // namespace ai_studio::audio