#include "VoiceLibraryManager.hpp"
#include <iostream>
#include <fstream>
#include <mutex>
#include <algorithm>

namespace ai_studio::ai {

VoiceLibraryManager::VoiceLibraryManager(std::filesystem::path storage_directory) noexcept
    : storage_directory_(std::move(storage_directory)) {
    std::cout << "[DEBUG][src/ai/VoiceLibraryManager.cpp::VoiceLibraryManager] Initialized library in: "
              << storage_directory_.string() << '\n';
}

bool VoiceLibraryManager::register_profile(const VoiceProfileMetadata& metadata) noexcept {
    if (metadata.profile_id.empty()) [[unlikely]] {
        return false;
    }

    std::unique_lock lock(library_mutex_);
    profiles_[metadata.profile_id] = metadata;
    if (active_profile_id_.empty()) {
        active_profile_id_ = metadata.profile_id;
    }
    std::cout << "[DEBUG][src/ai/VoiceLibraryManager.cpp::register_profile] Registered real voice profile: '"
              << metadata.display_name << "' (id=" << metadata.profile_id
              << ", source=" << metadata.source_file_path.string() << ")\n";
    return true;
}

bool VoiceLibraryManager::select_active_profile(std::string_view profile_id) noexcept {
    std::unique_lock lock(library_mutex_);
    auto it = profiles_.find(profile_id);
    if (it == profiles_.end()) [[unlikely]] {
        std::cerr << "[WARN][src/ai/VoiceLibraryManager.cpp::select_active_profile] Profile ID '"
                  << profile_id << "' not found in VoiceLibraryManager.\n";
        return false;
    }

    active_profile_id_ = it->first;
    std::cout << "[DEBUG][src/ai/VoiceLibraryManager.cpp::select_active_profile] Active cloned voice locked: '"
              << it->second.display_name << "' (id=" << active_profile_id_ << ")\n";
    return true;
}

bool VoiceLibraryManager::import_and_select_file(std::string_view file_path, VoiceUploadManager& upload_manager) noexcept {
    std::cout << "[DEBUG][src/ai/VoiceLibraryManager.cpp::import_and_select_file] Request to import/select: "
              << file_path << '\n';
    try {
        if (select_active_profile(file_path)) {
            return true;
        }

        std::filesystem::path path(file_path);
        if (!std::filesystem::exists(path)) {
            std::cerr << "[ERROR][src/ai/VoiceLibraryManager.cpp::import_and_select_file] File or profile not found: "
                      << file_path << "\n";
            return false;
        }

        std::string stem = path.stem().string();
        if (stem.empty()) stem = "cloned_voice";

        if (upload_manager.validate_source_media(file_path)) {
            if (upload_manager.preprocess_voice_source(stem, stem)) {
                if (const auto* meta = upload_manager.get_profile_metadata(stem)) {
                    (void)register_profile(*meta);
                    return select_active_profile(stem);
                }
            }
        }
        return false;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR][src/ai/VoiceLibraryManager.cpp::import_and_select_file] Exception: " << e.what() << "\n";
        return false;
    }
}

const VoiceProfileMetadata* VoiceLibraryManager::get_profile(std::string_view profile_id) const noexcept {
    std::shared_lock lock(library_mutex_);
    auto it = profiles_.find(profile_id);
    if (it != profiles_.end()) [[likely]] {
        return &it->second;
    }
    return nullptr;
}

bool VoiceLibraryManager::scan_library() noexcept {
    std::unique_lock lock(library_mutex_);
    try {
        if (!std::filesystem::exists(storage_directory_)) {
            std::filesystem::create_directories(storage_directory_);
            return true;
        }

        // Remove legacy dummy placeholder files if they were created by old builds
        const std::filesystem::path legacy_dummy_wav = storage_directory_ / "sample_source.wav";
        const std::filesystem::path legacy_dummy_onnx = storage_directory_ / "user_custom_profile.onnx";
        const std::filesystem::path legacy_dummy_meta = storage_directory_ / "user_custom_profile.voice.meta";
        if (std::filesystem::exists(legacy_dummy_wav) && std::filesystem::file_size(legacy_dummy_wav) < 128) {
            std::filesystem::remove(legacy_dummy_wav);
            if (!std::filesystem::exists(legacy_dummy_meta) &&
                std::filesystem::exists(legacy_dummy_onnx) &&
                std::filesystem::file_size(legacy_dummy_onnx) < 128) {
                std::filesystem::remove(legacy_dummy_onnx);
            }
        }

        // 1. Load all saved .voice.meta sidecars (created when Admin clones a video/audio file)
        for (const auto& entry : std::filesystem::directory_iterator(storage_directory_)) {
            if (entry.is_regular_file() && entry.path().filename().string().ends_with(".voice.meta")) {
                std::ifstream in(entry.path());
                if (in.is_open()) {
                    VoiceProfileMetadata meta;
                    std::string src_path_str;
                    std::getline(in, meta.profile_id);
                    std::getline(in, meta.display_name);
                    std::getline(in, src_path_str);
                    in >> meta.fundamental_bias_semitones
                       >> meta.formant_f1_gain
                       >> meta.formant_f2_gain
                       >> meta.warmth_saturation;

                    if (!meta.profile_id.empty()) {
                        meta.source_file_path = src_path_str;
                        meta.processed_model_path = storage_directory_ / (meta.profile_id + ".onnx");
                        meta.sample_rate = 48000;
                        meta.channels = 1;
                        meta.is_validated = true;
                        profiles_[meta.profile_id] = meta;
                        if (active_profile_id_.empty()) {
                            active_profile_id_ = meta.profile_id;
                        }
                        std::cout << "[DEBUG][src/ai/VoiceLibraryManager.cpp::scan_library] Loaded cloned voice profile: '"
                                  << meta.display_name << "' (id=" << meta.profile_id << ")\n";
                    }
                }
            }
        }

        // 2. Also discover any standalone .onnx voice models placed in /models
        for (const auto& entry : std::filesystem::directory_iterator(storage_directory_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".onnx") {
                std::string filename = entry.path().stem().string();
                if (filename == "faceswap" || filename == "body_tracker" ||
                    filename == "selfie_segmentation" || filename == "sample_voice_model") {
                    continue;
                }
                if (profiles_.find(filename) == profiles_.end()) {
                    VoiceProfileMetadata meta;
                    meta.profile_id = filename;
                    meta.display_name = filename;
                    meta.source_file_path = entry.path();
                    meta.processed_model_path = entry.path();
                    meta.sample_rate = 48000;
                    meta.channels = 1;
                    meta.is_validated = true;
                    profiles_[filename] = meta;
                    if (active_profile_id_.empty()) {
                        active_profile_id_ = filename;
                    }
                    std::cout << "[DEBUG][src/ai/VoiceLibraryManager.cpp::scan_library] Discovered ONNX voice model on disk: "
                              << filename << "\n";
                }
            }
        }

        std::cout << "[DEBUG][src/ai/VoiceLibraryManager.cpp::scan_library] Total real voice profiles in library: "
                  << profiles_.size() << '\n';
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR][src/ai/VoiceLibraryManager.cpp::scan_library] Exception: " << e.what() << "\n";
        return false;
    }
}

bool VoiceLibraryManager::delete_profile(std::string_view profile_id) noexcept {
    std::unique_lock lock(library_mutex_);
    auto it = profiles_.find(profile_id);
    if (it == profiles_.end()) [[unlikely]] {
        return false;
    }

    try {
        std::string id_str = it->first;
        if (std::filesystem::exists(it->second.processed_model_path)) {
            std::filesystem::remove(it->second.processed_model_path);
        }
        std::filesystem::path meta_path = storage_directory_ / (id_str + ".voice.meta");
        if (std::filesystem::exists(meta_path)) {
            std::filesystem::remove(meta_path);
        }
        profiles_.erase(it);
        if (active_profile_id_ == id_str) {
            active_profile_id_ = profiles_.empty() ? "" : profiles_.begin()->first;
        }
        std::cout << "[DEBUG][src/ai/VoiceLibraryManager.cpp::delete_profile] Deleted voice profile: " << id_str << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR][src/ai/VoiceLibraryManager.cpp::delete_profile] Exception: " << e.what() << "\n";
        return false;
    }
}

std::vector<std::string> VoiceLibraryManager::get_profile_ids() const noexcept {
    std::shared_lock lock(library_mutex_);
    std::vector<std::string> ids;
    ids.reserve(profiles_.size());
    for (const auto& [id, _] : profiles_) {
        ids.push_back(id);
    }
    return ids;
}

std::vector<VoiceProfileMetadata> VoiceLibraryManager::get_all_profiles() const noexcept {
    std::shared_lock lock(library_mutex_);
    std::vector<VoiceProfileMetadata> list;
    list.reserve(profiles_.size());
    for (const auto& [_, meta] : profiles_) {
        list.push_back(meta);
    }
    return list;
}

} // namespace ai_studio::ai