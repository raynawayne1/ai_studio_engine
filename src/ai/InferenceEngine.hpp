#pragma once

#include <string>
#include <vector>

namespace ai_studio::ai {

enum class ExecutionProvider {
    CPU,
    CoreML,
    CUDA,
    DirectML,
    TensorRT
};

class InferenceEngine {
public:
    // Selects the optimal execution provider based on hardware capabilities and OS
    static ExecutionProvider select_optimal_provider() noexcept;
    
    // Returns a human-readable string representation of the active AI backend
    static std::string provider_to_string(ExecutionProvider provider) noexcept;

    // Verifies if the ONNX Runtime environment is fully initialized
    [[nodiscard]] static bool initialize_backend() noexcept;
};

} // namespace ai_studio::ai