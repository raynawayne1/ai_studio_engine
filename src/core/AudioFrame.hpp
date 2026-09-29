#pragma once

#include <vector>
#include <cstdint>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
#endif

namespace ai_studio::core {

// alignas(32) ensures memory aligns perfectly for CPU AVX/SIMD instructions.
// This maximizes ONNX Runtime inference speed for real-time AI models.
struct alignas(32) AudioFrame {
    std::vector<float> samples;
    uint64_t timestamp_us;
    bool is_valid;

    // Pre-allocate vector memory EXACTLY ONCE at startup. Zero runtime allocations.
    explicit AudioFrame(size_t max_samples) {
        samples.reserve(max_samples);
        samples.resize(max_samples, 0.0f);
        timestamp_us = 0;
        is_valid = false;
    }

    // Delete copy/move operations to prevent accidental expensive copies during real-time streaming
    AudioFrame(const AudioFrame&) = delete;
    AudioFrame& operator=(const AudioFrame&) = delete;
    AudioFrame(AudioFrame&&) = delete;
    AudioFrame& operator=(AudioFrame&&) = delete;

    ~AudioFrame() = default;

    // Reset for immediate reuse without releasing or reallocating memory
    void reset() noexcept {
        timestamp_us = 0;
        is_valid = false;
        // Note: We do NOT clear() or shrink_to_fit(). Memory stays reserved for zero lag.
    }
};

} // namespace ai_studio::core

#ifdef _MSC_VER
#pragma warning(pop)
#endif