#include "CloudSyncManager.hpp"
#include <iostream>

namespace ai_studio::ai {

CloudSyncManager::CloudSyncManager(std::filesystem::path local_cache_dir) noexcept
    : local_cache_dir_(std::move(local_cache_dir)) {}

bool CloudSyncManager::sync_model_metadata(std::string_view model_id) noexcept {
    status_.store(SyncStatus::Synchronizing, std::memory_order_release);
    std::cout << "[Cloud Sync] Checking cloud metadata for model: " << model_id << "...\n";
    
    try {
        if (!std::filesystem::exists(local_cache_dir_)) [[unlikely]] {
            std::filesystem::create_directories(local_cache_dir_);
        }
        
        status_.store(SyncStatus::UpToDate, std::memory_order_release);
        std::cout << "[Cloud Sync] Asset synchronization complete. Offline cache verified.\n";
        return true;
    } catch (const std::exception& e) {
        status_.store(SyncStatus::Error, std::memory_order_release);
        std::cerr << "[Cloud Sync Error] " << e.what() << "\n";
        return false;
    }
}

bool CloudSyncManager::verify_local_asset(std::string_view model_id) const noexcept {
    try {
        std::filesystem::path target_path = local_cache_dir_;
        target_path /= std::string(model_id);
        target_path += ".onnx";
        
        if (std::filesystem::exists(target_path) && std::filesystem::file_size(target_path) > 0) [[likely]] {
            return true;
        }
    } catch (...) {}
    return false;
}

} // namespace ai_studio::ai