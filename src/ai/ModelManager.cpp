#include "ModelManager.hpp"
#include <iostream>
#include <mutex>

namespace ai_studio::ai {

ModelManager::ModelManager(std::filesystem::path models_directory) noexcept
    : models_dir_(std::move(models_directory)) {}

bool ModelManager::scan_local_models() noexcept {
    std::unique_lock lock(mutex_);
    std::cout << "[Model Manager] Scanning local model cache directory: " << models_dir_.string() << "\n";
    
    try {
        if (!std::filesystem::exists(models_dir_)) [[unlikely]] {
            std::filesystem::create_directories(models_dir_);
            std::cout << "[Model Manager] Created local model cache directory successfully.\n";
            return true;
        }

        for (const auto& entry : std::filesystem::directory_iterator(models_dir_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".onnx") [[likely]] {
                std::string model_id = entry.path().stem().string();
                size_t file_size = std::filesystem::file_size(entry.path());
                
                cached_models_.insert_or_assign(model_id, ModelMetadata{
                    model_id,
                    entry.path(),
                    file_size,
                    true
                });

                std::cout << "[Model Manager] Discovered cached model: " << model_id 
                          << " (" << (file_size / 1024) << " KB)\n";
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Model Manager Error] Exception during directory scan: " << e.what() << "\n";
        return false;
    }
}

bool ModelManager::register_model(std::string_view model_id, const std::filesystem::path& path) noexcept {
    std::unique_lock lock(mutex_);
    try {
        if (std::filesystem::exists(path)) [[likely]] {
            std::string id_str(model_id);
            size_t file_size = std::filesystem::file_size(path);
            
            cached_models_.insert_or_assign(id_str, ModelMetadata{
                id_str,
                path,
                file_size,
                true
            });
            return true;
        }
    } catch (...) {}
    return false;
}

const ModelMetadata* ModelManager::get_model_metadata(std::string_view model_id) const noexcept {
    std::shared_lock lock(mutex_);
    auto it = cached_models_.find(model_id);
    if (it != cached_models_.end()) [[likely]] {
        return &it->second;
    }
    return nullptr;
}

std::vector<ModelMetadata> ModelManager::get_all_models() const noexcept {
    std::shared_lock lock(mutex_);
    std::vector<ModelMetadata> list;
    list.reserve(cached_models_.size());
    for (const auto& [_, meta] : cached_models_) {
        list.push_back(meta);
    }
    return list;
}

} // namespace ai_studio::ai