#pragma once

#include <vector>
#include <cstdint>

namespace ai_studio::core {

// alignas(32) ensures the memory aligns perfectly for CPU AVX/SIMD instructions.
// This is critical for maximizing ONNX Runtime inference speed.
struct alignas(32) AudioFrame {
    std::vector<float> samples;
    uint64_t timestamp_us;
    bool is_valid;

    // We allocate the vector memory EXACTLY ONCE when the app starts.
    explicit AudioFrame(size_t max_samples) {
        samples.reserve(max_samples);
        samples.resize(max_samples, 0.0f);
        timestamp_us = 0;
        is_valid = false;
    }

    // Reset for reuse without deleting or reallocating memory
    void reset() noexcept {
        timestamp_us = 0;
        is_valid = false;
        // Notice we do NOT clear() or shrink_to_fit() the vector. 
        // We keep the memory reserved to guarantee zero lag.
    }
};

} // namespace ai_studio::core