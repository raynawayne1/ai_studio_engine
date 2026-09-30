#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <shared_mutex>
#include <filesystem>
#include <unordered_map>

namespace ai_studio::ai {

struct ModelMetadata {
    std::string model_id;
    std::filesystem::path file_path;
    size_t file_size_bytes = 0;
    bool is_cached = false;
};

struct TransparentStringHash {
    using is_transparent = void;
    [[nodiscard]] size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
    [[nodiscard]] size_t operator()(const std::string& s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
};

class ModelManager {
public:
    explicit ModelManager(std::filesystem::path models_directory) noexcept;
    ~ModelManager() noexcept = default;

    ModelManager(const ModelManager&) = delete;
    ModelManager& operator=(const ModelManager&) = delete;
    ModelManager(ModelManager&&) = delete;
    ModelManager& operator=(ModelManager&&) = delete;

    [[nodiscard]] bool scan_local_models() noexcept;
    [[nodiscard]] bool register_model(std::string_view model_id, const std::filesystem::path& path) noexcept;
    [[nodiscard]] const ModelMetadata* get_model_metadata(std::string_view model_id) const noexcept;
    [[nodiscard]] std::vector<ModelMetadata> get_all_models() const noexcept;

private:
    std::filesystem::path models_dir_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, ModelMetadata, TransparentStringHash, std::equal_to<>> cached_models_;
};

} // namespace ai_studio::ai