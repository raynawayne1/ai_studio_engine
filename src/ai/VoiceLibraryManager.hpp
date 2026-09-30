#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <filesystem>
#include <shared_mutex>
#include <vector>
#include <cstdint>
#include "VoiceUploadManager.hpp"

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

// Unique transparent hash for zero-allocation string_view lookups in unordered_map
struct LibraryTransparentHash {
    using is_transparent = void;
    [[nodiscard]] size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
    [[nodiscard]] size_t operator()(const std::string& s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
};

class VoiceLibraryManager {
public:
    explicit VoiceLibraryManager(std::filesystem::path storage_directory) noexcept;
    ~VoiceLibraryManager() noexcept = default;

    // Prevent copying and moving
    VoiceLibraryManager(const VoiceLibraryManager&) = delete;
    VoiceLibraryManager& operator=(const VoiceLibraryManager&) = delete;
    VoiceLibraryManager(VoiceLibraryManager&&) = delete;
    VoiceLibraryManager& operator=(VoiceLibraryManager&&) = delete;

    // Register or update a voice profile in the library
    [[nodiscard]] bool register_profile(const VoiceProfileMetadata& metadata) noexcept;

    // Select the active voice profile with zero string allocation lookup
    [[nodiscard]] bool select_active_profile(std::string_view profile_id) noexcept;

    // Directly import or activate a voice file (.onnx, .wav, .mp3) selected from the UI
    [[nodiscard]] bool import_and_select_file(std::string_view file_path, VoiceUploadManager& upload_manager) noexcept;

    // Retrieve active profile metadata with force-inlined zero overhead
    [[nodiscard]] AI_FORCE_INLINE const VoiceProfileMetadata* get_active_profile() const noexcept {
        std::shared_lock lock(library_mutex_);
        if (active_profile_id_.empty()) [[unlikely]] {
            return nullptr;
        }
        auto it = profiles_.find(active_profile_id_);
        if (it != profiles_.end()) [[likely]] {
            return &it->second;
        }
        return nullptr;
    }

    // Retrieve specific profile metadata by ID with zero heap allocations
    [[nodiscard]] const VoiceProfileMetadata* get_profile(std::string_view profile_id) const noexcept;

    // Scan and load all local voice profiles from storage directory
    [[nodiscard]] bool scan_library() noexcept;

    // Delete a voice profile from library and disk
    [[nodiscard]] bool delete_profile(std::string_view profile_id) noexcept;

    // Return a list of all available profile IDs
    [[nodiscard]] std::vector<std::string> get_profile_ids() const noexcept;

    // Return all registered profiles for UI listing
    [[nodiscard]] std::vector<VoiceProfileMetadata> get_all_profiles() const noexcept;

private:
    std::filesystem::path storage_directory_;
    std::unordered_map<std::string, VoiceProfileMetadata, LibraryTransparentHash, std::equal_to<>> profiles_;
    std::string active_profile_id_;
    mutable std::shared_mutex library_mutex_;
};

} // namespace ai_studio::ai

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#undef AI_FORCE_INLINE