#pragma once

#include <string>
#include <string_view>
#include <filesystem>
#include <atomic>
#include <cstdint>

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

struct VoiceProfileMetadata {
    std::string profile_id;
    std::string display_name;
    std::filesystem::path source_file_path;
    std::filesystem::path processed_model_path;
    uint32_t sample_rate{48000};
    uint32_t channels{1};
    uint64_t duration_ms{0};
    // Cloned Voice Acoustic Fingerprint (extracted from user's pre-cloned voice file)
    float fundamental_bias_semitones{0.0f};
    float formant_f1_gain{1.0f};
    float formant_f2_gain{1.0f};
    float warmth_saturation{0.18f};
    bool is_validated{false};
};

class VoiceUploadManager {
public:
    explicit VoiceUploadManager(std::filesystem::path storage_directory) noexcept;
    ~VoiceUploadManager() noexcept = default;

    VoiceUploadManager(const VoiceUploadManager&) = delete;
    VoiceUploadManager& operator=(const VoiceUploadManager&) = delete;
    VoiceUploadManager(VoiceUploadManager&&) = delete;
    VoiceUploadManager& operator=(VoiceUploadManager&&) = delete;

    [[nodiscard]] bool validate_source_media(std::string_view file_path) noexcept;
    [[nodiscard]] bool preprocess_voice_source(std::string_view profile_id, std::string_view display_name) noexcept;

    [[nodiscard]] AI_FORCE_INLINE const VoiceProfileMetadata* get_profile_metadata(std::string_view profile_id) const noexcept {
        if (current_metadata_.profile_id == profile_id) [[likely]] {
            return &current_metadata_;
        }
        return nullptr;
    }

    [[nodiscard]] AI_FORCE_INLINE bool is_processing() const noexcept {
        return processing_active_.load(std::memory_order_relaxed);
    }

private:
    std::filesystem::path storage_directory_;
    VoiceProfileMetadata current_metadata_;
    alignas(64) std::atomic<bool> processing_active_{false};
};

} // namespace ai_studio::ai

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#undef AI_FORCE_INLINE