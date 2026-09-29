#pragma once

#include <string>
#include <string_view>
#include <filesystem>
#include <atomic>
#include <cstdint>

namespace ai_studio::ai {

struct VoiceProfileMetadata {
    std::string profile_id;
    std::string display_name;
    std::filesystem::path source_file_path;
    std::filesystem::path processed_model_path;
    uint32_t sample_rate{48000};
    uint32_t channels{1};
    uint64_t duration_ms{0};
    bool is_validated{false};
};

class VoiceUploadManager {
public:
    explicit VoiceUploadManager(std::filesystem::path storage_directory) noexcept;
    ~VoiceUploadManager() noexcept = default;

    // Prevent copying and moving
    VoiceUploadManager(const VoiceUploadManager&) = delete;
    VoiceUploadManager& operator=(const VoiceUploadManager&) = delete;
    VoiceUploadManager(VoiceUploadManager&&) = delete;
    VoiceUploadManager& operator=(VoiceUploadManager&&) = delete;

    // High-speed zero-allocation source media validation for studio-quality RVC inputs
    [[nodiscard]] bool validate_source_media(std::string_view file_path) noexcept;

    // High-performance preprocessing & feature extraction pipeline (48kHz Mono PCM canonical target)
    [[nodiscard]] bool preprocess_voice_source(std::string_view profile_id, std::string_view display_name) noexcept;

    // Retrieve active profile metadata with zero lookup latency
    [[nodiscard]] const VoiceProfileMetadata* get_profile_metadata(std::string_view profile_id) const noexcept;

    [[nodiscard]] bool is_processing() const noexcept {
        return processing_active_.load(std::memory_order_relaxed);
    }

private:
    std::filesystem::path storage_directory_;
    VoiceProfileMetadata current_metadata_;
    alignas(64) std::atomic<bool> processing_active_{false};
};

} // namespace ai_studio::ai