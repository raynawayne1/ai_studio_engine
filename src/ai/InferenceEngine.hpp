#pragma once

#include <string>
#include <string_view>
#include <memory>
#include <vector>
#include <unordered_map>
#include <onnxruntime_cxx_api.h>

namespace ai_studio::ai {

enum class ExecutionProvider {
    CPU,
    CoreML,
    Metal,
    CUDA,
    TensorRT,
    DirectML
};

class InferenceEngine {
public:
    InferenceEngine() = delete;
    ~InferenceEngine() = delete;

    [[nodiscard]] static ExecutionProvider select_optimal_provider() noexcept;
    [[nodiscard]] static std::string_view provider_to_string(ExecutionProvider provider) noexcept;
    
    // Initializes the global ONNX runtime environment (zero-lag, lock-free config)
    [[nodiscard]] static bool initialize_backend() noexcept;
    
    // Frees hardware resources cleanly
    static void shutdown() noexcept;

    // Creates an extremely optimized inference session for FaceSwap / RVC with safe fallbacks
    [[nodiscard]] static std::unique_ptr<Ort::Session> create_session(const std::string& model_path);

    // Access the global environment required by Ort::Session
    [[nodiscard]] static Ort::Env& get_env();

    [[nodiscard]] static bool is_initialized() noexcept;

private:
    static std::unique_ptr<Ort::Env> s_env;
    static ExecutionProvider s_active_provider;
};

} // namespace ai_studio::ai