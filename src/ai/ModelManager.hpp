#pragma once

#include <string>
#include <string_view>
#include <filesystem>
#include <unordered_map>

namespace ai_studio::ai {

struct ModelMetadata {
    std::string model_id;
    std::filesystem::path file_path;
    size_t file_size_bytes = 0;
    bool is_cached = false;
};

// Transparent hash structure for zero-allocation string_view lookups in unordered_map (C++20)
struct TransparentStringHash {
    using is_transparent = void;
    [[nodiscard]] size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
};

class ModelManager {
public:
    explicit ModelManager(std::filesystem::path models_directory) noexcept;
    ~ModelManager() noexcept = default;

    // Prevent copying and moving
    ModelManager(const ModelManager&) = delete;
    ModelManager& operator=(const ModelManager&) = delete;
    ModelManager(ModelManager&&) = delete;
    ModelManager& operator=(ModelManager&&) = delete;

    // Scans local storage directory for available ONNX model weights
    [[nodiscard]] bool scan_local_models() noexcept;

    // Registers or verifies a local model file for offline use (zero-copy string_view)
    [[nodiscard]] bool register_model(std::string_view model_id, const std::filesystem::path& path) noexcept;

    // Fast O(1) lookup of cached model metadata with ZERO temporary heap allocations
    [[nodiscard]] const ModelMetadata* get_model_metadata(std::string_view model_id) const noexcept;

private:
    std::filesystem::path models_dir_;
    
    // unordered_map configured with transparent hashing and heterogeneous equality 
    // for absolute zero-allocation runtime queries.
    std::unordered_map<std::string, ModelMetadata, TransparentStringHash, std::equal_to<>> cached_models_;
};

} // namespace ai_studio::ai