#pragma once

#include <string>
#include <string_view>
#include <memory>
#include <unordered_map>
#include <onnxruntime_cxx_api.h> // REAL ONNX RUNTIME C++ API

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

    static ExecutionProvider select_optimal_provider() noexcept;
    static std::string_view provider_to_string(ExecutionProvider provider) noexcept;
    
    // Initializes the global ONNX runtime environment (zero-lag telemetry config)
    static bool initialize_backend() noexcept;
    
    // Frees hardware resources cleanly
    static void shutdown() noexcept;

    // Creates an extremely optimized inference session for FaceSwap / RVC
    static std::unique_ptr<Ort::Session> create_session(const std::string& model_path);

    // Access the global environment required by Ort::Session
    static Ort::Env& get_env();

private:
    static std::unique_ptr<Ort::Env> s_env;
    static ExecutionProvider s_active_provider;
};

} // namespace ai_studio::ai