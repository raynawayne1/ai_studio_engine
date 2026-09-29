#pragma once

#include <string_view>

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
    // Selects the optimal execution provider based on hardware capabilities (noexcept, zero-copy)
    [[nodiscard]] static ExecutionProvider select_optimal_provider() noexcept;
    
    // Returns a string_view representation with ZERO heap allocations
    [[nodiscard]] static constexpr std::string_view provider_to_string(ExecutionProvider provider) noexcept;

    // Verifies if the ONNX Runtime environment is fully initialized
    [[nodiscard]] static bool initialize_backend() noexcept;
};

} // namespace ai_studio::ai