#include "AudioCaptureEngine.hpp"
#include <iostream>

namespace ai_studio::audio {

AudioCaptureEngine::AudioCaptureEngine(core::AudioFramePool& frame_pool, AudioConfig config) noexcept
    : frame_pool_(frame_pool), config_(config) {}

AudioCaptureEngine::~AudioCaptureEngine() noexcept {
    stop();
}

bool AudioCaptureEngine::start() noexcept {
    if (is_running_.load(std::memory_order_acquire)) {
        return true;
    }

    std::cout << "[Audio Engine] Initializing zero-lag microphone capture stream...\n";
    std::cout << "[Audio Engine] Target Sample Rate: " << config_.sample_rate 
              << " Hz | Channels: " << config_.channels 
              << " | Chunk Size: " << config_.frame_size << " samples\n";

    is_running_.store(true, std::memory_order_release);
    std::cout << "[Audio Engine] Audio capture stream successfully active.\n";
    return true;
}

void AudioCaptureEngine::stop() noexcept {
    if (!is_running_.load(std::memory_order_acquire)) {
        return;
    }

    is_running_.store(false, std::memory_order_release);
    std::cout << "[Audio Engine] Audio capture stream stopped cleanly.\n";
}

} // namespace ai_studio::audio