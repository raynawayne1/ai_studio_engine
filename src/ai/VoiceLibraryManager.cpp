#include "VoiceLibraryManager.hpp"
#include <iostream>
#include <fstream>
#include <mutex>
#include <algorithm>

namespace ai_studio::ai {

VoiceLibraryManager::VoiceLibraryManager(std::filesystem::path storage_directory) noexcept
    : storage_directory_(std::move(storage_directory)) {}

bool VoiceLibraryManager::register_profile(const VoiceProfileMetadata& metadata) noexcept {
    if (metadata.profile_id.empty()) [[unlikely]] {
        return false;
    }

    std::unique_lock lock(library_mutex_);
    profiles_[metadata.profile_id] = metadata;
    std::cout << "[Voice Library] Registered voice profile: " << metadata.display_name 
              << " (" << metadata.profile_id << ")\n";
    return true;
}

bool VoiceLibraryManager::select_active_profile(std::string_view profile_id) noexcept {
    std::unique_lock lock(library_mutex_);
    auto it = profiles_.find(profile_id);
    if (it == profiles_.end()) [[unlikely]] {
        std::cerr << "[Voice Library Error] Profile ID not found in library: " << profile_id << "\n";
        return false;
    }

    active_profile_id_ = it->first;
    std::cout << "[Voice Library] Active voice profile selected: " << it->second.display_name << "\n";
    return true;
}

bool VoiceLibraryManager::import_and_select_file(std::string_view file_path, VoiceUploadManager& upload_manager) noexcept {
    try {
        std::filesystem::path path(file_path);
        if (!std::filesystem::exists(path)) {
            // Check if the caller passed a profile_id instead of a file path
            if (select_active_profile(file_path)) {
                return true;
            }
            std::cerr << "[Voice Library Error] Voice file or profile not found: " << file_path << "\n";
            return false;
        }

        std::string stem = path.stem().string();
        if (stem.empty()) stem = "custom_voice";

        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        if (ext == ".onnx") {
            VoiceProfileMetadata meta;
            meta.profile_id = stem;
            meta.display_name = stem;
            meta.source_file_path = path;
            meta.processed_model_path = path;
            meta.sample_rate = 48000;
            meta.channels = 1;
            meta.is_validated = true;
            (void)register_profile(meta);
            return select_active_profile(stem);
        }

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
        std::cerr << "[Voice Library Exception] Failed importing voice file: " << e.what() << "\n";
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

        for (const auto& entry : std::filesystem::directory_iterator(storage_directory_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".onnx") {
                std::string filename = entry.path().stem().string();
                // Skip vision models in the shared /models directory
                if (filename == "faceswap" || filename == "body_tracker" || filename == "selfie_segmentation") {
                    continue;
                }
                if (profiles_.find(filename) == profiles_.end()) {
                    VoiceProfileMetadata meta;
                    meta.profile_id = filename;
                    meta.display_name = filename;
                    meta.processed_model_path = entry.path();
                    meta.is_validated = true;
                    profiles_[filename] = meta;
                    std::cout << "[Voice Library] Discovered cached profile on disk: " << filename << "\n";
                }
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Voice Library Exception] Scan failed: " << e.what() << "\n";
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
        if (std::filesystem::exists(it->second.processed_model_path)) {
            std::filesystem::remove(it->second.processed_model_path);
        }
        std::string id_str = it->first;
        profiles_.erase(it);
        if (active_profile_id_ == id_str) {
            active_profile_id_.clear();
        }
        std::cout << "[Voice Library] Deleted profile: " << id_str << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Voice Library Exception] Deletion failed: " << e.what() << "\n";
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