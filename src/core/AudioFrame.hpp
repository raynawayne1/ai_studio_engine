#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324)
#endif

namespace ai_studio::core {

// alignas(32) ensures memory aligns perfectly for CPU AVX2/NEON SIMD instructions
struct alignas(32) AudioFrame {
    std::vector<float> samples;
    uint64_t timestamp_us{0};
    bool is_valid{false};

    explicit AudioFrame(size_t max_samples) {
        samples.assign(max_samples, 0.0f);
        timestamp_us = 0;
        is_valid = false;
    }

    AudioFrame(const AudioFrame&) = delete;
    AudioFrame& operator=(const AudioFrame&) = delete;
    AudioFrame(AudioFrame&&) = delete;
    AudioFrame& operator=(AudioFrame&&) = delete;

    ~AudioFrame() = default;

    void reset() noexcept {
        timestamp_us = 0;
        is_valid = false;
    }
};

} // namespace ai_studio::core

#ifdef _MSC_VER
#pragma warning(pop)
#endif