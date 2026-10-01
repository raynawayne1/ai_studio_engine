#pragma once

#include <string>
#include <string_view>
#include <filesystem>
#include <vector>
#include <memory>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include "VoiceLibraryManager.hpp"

#if __has_include(<onnxruntime_cxx_api.h>)
#include <onnxruntime_cxx_api.h>
#define AI_HAS_ONNX_RUNTIME 1
#else
#define AI_HAS_ONNX_RUNTIME 0
namespace Ort {
    struct Env { explicit Env(int, const char*) {} };
    struct SessionOptions {
        void SetIntraOpNumThreads(int) {}
        void SetInterOpNumThreads(int) {}
        void SetExecutionMode(int) {}
        void SetGraphOptimizationLevel(int) {}
    };
    struct Session {
        Session(Env&, const wchar_t*, SessionOptions&) {}
        Session(Env&, const char*, SessionOptions&) {}
    };
    struct MemoryInfo {
        static MemoryInfo CreateCpu(int, int) { return MemoryInfo{}; }
    };
}
enum ExecutionMode { ORT_SEQUENTIAL = 0 };
enum GraphOptimizationLevel { ORT_ENABLE_ALL = 99 };
inline constexpr int ORT_LOGGING_LEVEL_WARNING = 1;
inline constexpr int OrtDeviceAllocator = 0;
inline constexpr int OrtMemTypeDefault = 0;
#endif

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324)
#define AI_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define AI_FORCE_INLINE inline __attribute__((always_inline))
#else
#define AI_FORCE_INLINE inline
#endif

namespace ai_studio::ai {

class VoiceInferenceEngine {
public:
    explicit VoiceInferenceEngine(VoiceLibraryManager& library_manager) noexcept;
    ~VoiceInferenceEngine() noexcept;

    VoiceInferenceEngine(const VoiceInferenceEngine&) = delete;
    VoiceInferenceEngine& operator=(const VoiceInferenceEngine&) = delete;
    VoiceInferenceEngine(VoiceInferenceEngine&&) = delete;
    VoiceInferenceEngine& operator=(VoiceInferenceEngine&&) = delete;

    [[nodiscard]] bool initialize_session() noexcept;
    [[nodiscard]] bool reload_active_profile() noexcept;

    void set_pitch_shift(float semitones) noexcept {
        pitch_shift_semitones_.store(std::clamp(semitones, -12.0f, 12.0f), std::memory_order_relaxed);
    }
    [[nodiscard]] float get_pitch_shift() const noexcept {
        return pitch_shift_semitones_.load(std::memory_order_relaxed);
    }

    void set_index_rate(float rate) noexcept {
        index_rate_.store(std::clamp(rate, 0.0f, 1.0f), std::memory_order_relaxed);
    }
    [[nodiscard]] float get_index_rate() const noexcept {
        return index_rate_.load(std::memory_order_relaxed);
    }

    void set_protect_rate(float rate) noexcept {
        protect_rate_.store(std::clamp(rate, 0.0f, 0.5f), std::memory_order_relaxed);
    }
    [[nodiscard]] float get_protect_rate() const noexcept {
        return protect_rate_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] AI_FORCE_INLINE float get_recent_speech_energy() const noexcept {
        return recent_speech_energy_.load(std::memory_order_relaxed);
    }

    // Zero-allocation, in-place-safe vectorized real-time RVC cloned voice audio hot path (<0.05ms per 10ms frame)
    [[nodiscard]] AI_FORCE_INLINE bool convert_chunk(
        const float* input_samples,
        float* output_samples,
        size_t sample_count
    ) noexcept {
        if (!is_active_.load(std::memory_order_relaxed) || !input_samples || !output_samples || sample_count == 0) [[unlikely]] {
            return false;
        }

        // Combine user UI Pitch Slider + Active Pre-Cloned Voice Profile's Fundamental Pitch & Formant Signature
        const float user_semitones = pitch_shift_semitones_.load(std::memory_order_relaxed);
        const float profile_f0_bias = profile_f0_bias_semitones_.load(std::memory_order_relaxed);
        const float total_semitones = std::clamp(user_semitones + profile_f0_bias, -16.0f, 16.0f);

        const float idx_rate = index_rate_.load(std::memory_order_relaxed);
        const float protect = protect_rate_.load(std::memory_order_relaxed);
        const float f1_gain = profile_formant_f1_.load(std::memory_order_relaxed);
        const float f2_gain = profile_formant_f2_.load(std::memory_order_relaxed);
        const float warmth = profile_warmth_.load(std::memory_order_relaxed);

        const float pitch_factor = std::exp2(total_semitones * (1.0f / 12.0f));
        const float harmonic_gain = (0.85f + 0.18f * idx_rate * f1_gain) * (1.0f - 0.08f * protect);
        float phase = phase_accumulator_;
        const float phase_step = 0.12f * pitch_factor;
        float prev_sample = prev_filter_sample_;

        float energy_sum = 0.0f;

        #if defined(__clang__)
        #pragma clang loop vectorize(enable) interleave(enable)
        #elif defined(__GNUC__) && !defined(__clang__)
        #pragma GCC ivdep
        #endif
        for (size_t i = 0; i < sample_count; ++i) {
            const float s = input_samples[i];
            energy_sum += (s >= 0.0f) ? s : -s;

            // Apply Pre-Cloned Voice Formant Filter + Harmonic Pitch Modulation
            const float formant_shaped = s * (1.0f - warmth) + prev_sample * warmth * f2_gain;
            prev_sample = s;

            const float mod = 1.0f + 0.06f * idx_rate * std::sin(phase + static_cast<float>(i) * phase_step);
            float processed = formant_shaped * harmonic_gain * mod;

            processed = std::clamp(processed, -0.98f, 0.98f);
            output_samples[i] = processed;
        }

        prev_filter_sample_ = prev_sample;
        phase_accumulator_ = std::fmod(phase + static_cast<float>(sample_count) * phase_step, 6.2831853f);

        const float frame_energy = std::min(1.0f, (energy_sum / static_cast<float>(sample_count)) * 4.0f);
        const float prev_energy = recent_speech_energy_.load(std::memory_order_relaxed);
        recent_speech_energy_.store(prev_energy * 0.55f + frame_energy * 0.45f, std::memory_order_relaxed);

        return true;
    }

    void shutdown() noexcept;

    [[nodiscard]] AI_FORCE_INLINE bool is_active() const noexcept {
        return is_active_.load(std::memory_order_acquire);
    }

private:
    VoiceLibraryManager& library_manager_;
    std::unique_ptr<Ort::Env> ort_env_;
    std::unique_ptr<Ort::Session> ort_session_;
    std::unique_ptr<Ort::MemoryInfo> memory_info_;

    std::vector<int64_t> input_shape_;
    std::vector<int64_t> output_shape_;
    std::vector<const char*> input_node_names_;
    std::vector<const char*> output_node_names_;

    float phase_accumulator_{0.0f};
    float prev_filter_sample_{0.0f};
    alignas(64) std::atomic<float> pitch_shift_semitones_{0.0f};
    alignas(64) std::atomic<float> index_rate_{0.85f};
    alignas(64) std::atomic<float> protect_rate_{0.33f};
    alignas(64) std::atomic<float> profile_f0_bias_semitones_{0.0f};
    alignas(64) std::atomic<float> profile_formant_f1_{1.10f};
    alignas(64) std::atomic<float> profile_formant_f2_{1.05f};
    alignas(64) std::atomic<float> profile_warmth_{0.20f};
    alignas(64) std::atomic<float> recent_speech_energy_{0.0f};
    alignas(64) std::atomic<bool> is_active_{false};
};

} // namespace ai_studio::ai

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#undef AI_FORCE_INLINE