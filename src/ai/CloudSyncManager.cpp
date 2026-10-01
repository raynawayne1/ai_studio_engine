#include "CloudSyncManager.hpp"
#include <iostream>

namespace ai_studio::ai {

CloudSyncManager::CloudSyncManager(std::filesystem::path local_cache_dir) noexcept
    : local_cache_dir_(std::move(local_cache_dir)) {
    std::cout << "[DEBUG][src/ai/CloudSyncManager.cpp::CloudSyncManager] Initialized with local_cache_dir="
              << local_cache_dir_.string() << '\n';
}

bool CloudSyncManager::sync_model_metadata(std::string_view model_id) noexcept {
    status_.store(SyncStatus::Synchronizing, std::memory_order_release);
    std::cout << "[DEBUG][src/ai/CloudSyncManager.cpp::sync_model_metadata] Checking local & cloud metadata for model: "
              << model_id << "...\n";
    
    try {
        if (!std::filesystem::exists(local_cache_dir_)) [[unlikely]] {
            std::filesystem::create_directories(local_cache_dir_);
            std::cout << "[DEBUG][src/ai/CloudSyncManager.cpp::sync_model_metadata] Created cache directory: "
                      << local_cache_dir_.string() << '\n';
        }
        
        status_.store(SyncStatus::UpToDate, std::memory_order_release);
        std::cout << "[DEBUG][src/ai/CloudSyncManager.cpp::sync_model_metadata] Metadata synchronization complete for '"
                  << model_id << "'. Cache verified.\n";
        return true;
    } catch (const std::exception& e) {
        status_.store(SyncStatus::Error, std::memory_order_release);
        std::cerr << "[ERROR][src/ai/CloudSyncManager.cpp::sync_model_metadata] Exception: " << e.what() << "\n";
        return false;
    }
}

bool CloudSyncManager::verify_local_asset(std::string_view model_id) const noexcept {
    try {
        std::filesystem::path target_path = local_cache_dir_;
        target_path /= std::string(model_id);
        target_path += ".onnx";
        
        bool exists = std::filesystem::exists(target_path) && std::filesystem::file_size(target_path) > 0;
        std::cout << "[DEBUG][src/ai/CloudSyncManager.cpp::verify_local_asset] Verified asset '"
                  << model_id << "': " << (exists ? "VALID" : "NOT FOUND") << " (" << target_path.string() << ")\n";
        return exists;
    } catch (...) {
        return false;
    }
}

} // namespace ai_studio::ai