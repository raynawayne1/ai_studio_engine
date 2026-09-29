#pragma once

#include <string>
#include <string_view>
#include <filesystem>
#include <atomic>
#include <cstdint>
#include "VoiceLibraryManager.hpp"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
#define AI_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define AI_FORCE_INLINE inline __attribute__((always_inline))
#else
#define AI_FORCE_INLINE inline
#endif

namespace ai_studio::ai {

struct VoicePreviewConfig {
    int pitch_shift{0};         // Semitone adjustment (-12 to +12)
    float index_rate{0.75f};    // Feature retrieval weight (0.0 to 1.0)
    float protect_rate{0.33f};  // Protection for voiceless consonants and breath sounds
    bool enabled{true};
};

class VoicePreviewManager {
public:
    explicit VoicePreviewManager(VoiceLibraryManager& library_manager) noexcept;
    ~VoicePreviewManager() noexcept = default;

    // Prevent copying and moving
    VoicePreviewManager(const VoicePreviewManager&) = delete;
    VoicePreviewManager& operator=(const VoicePreviewManager&) = delete;
    VoicePreviewManager(VoicePreviewManager&&) = delete;
    VoicePreviewManager& operator=(VoicePreviewManager&&) = delete;

    // Step 1: Load and cryptographically/structurally verify the active local voice model before call
    [[nodiscard]] bool load_and_verify_active_model() noexcept;

    // Step 2: Configure advanced pre-call voice parameters
    void configure_settings(const VoicePreviewConfig& config) noexcept;

    // Step 3: Start real-time local microphone preview/test session
    [[nodiscard]] bool start_preview() noexcept;

    // Step 4: Force-inlined ultra-fast vectorised live audio chunk processing (Zero-allocation hot path)
    [[nodiscard]] AI_FORCE_INLINE bool process_preview_chunk(const float* input_samples, float* output_samples, size_t sample_count) noexcept {
        if (!preview_active_.load(std::memory_order_relaxed) || !input_samples || !output_samples) [[unlikely]] {
            return false;
        }

        const float pitch_gain = 1.0f + (static_cast<float>(current_config_.pitch_shift) * 0.05f);
        const float index_weight = current_config_.index_rate;
        const float combined_scale = pitch_gain * index_weight;
        const float residual_scale = 1.0f - index_weight;

        // Strict compiler vectorization directives isolated per compiler family
        #if defined(__clang__)
        #pragma clang loop vectorize(enable) interleave(enable)
        #elif defined(__GNUC__) && !defined(__clang__)
        #pragma GCC ivdep
        #endif
        for (size_t i = 0; i < sample_count; ++i) {
            output_samples[i] = (input_samples[i] * combined_scale) + (input_samples[i] * residual_scale);
        }

        return true;
    }

    // Step 5: Stop preview session
    void stop_preview() noexcept;

    // Step 6: Confirm final voice readiness for call launch
    [[nodiscard]] AI_FORCE_INLINE bool is_ready_for_call() const noexcept {
        return model_verified_.load(std::memory_order_acquire) && !preview_active_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] AI_FORCE_INLINE bool is_preview_active() const noexcept {
        return preview_active_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] const VoicePreviewConfig& get_config() const noexcept {
        return current_config_;
    }

private:
    VoiceLibraryManager& library_manager_;
    VoicePreviewConfig current_config_;
    alignas(64) std::atomic<bool> model_verified_{false};
    alignas(64) std::atomic<bool> preview_active_{false};
};

} // namespace ai_studio::ai

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#undef AI_FORCE_INLINE