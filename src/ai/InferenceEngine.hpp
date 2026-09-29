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
    
    // Returns a string_view representation with ZERO heap allocations.
    // Defined inline here to satisfy constexpr visibility rules.
    [[nodiscard]] static constexpr std::string_view provider_to_string(ExecutionProvider provider) noexcept {
        switch (provider) {
            case ExecutionProvider::CPU:      return "CPU (Multi-Threaded)";
            case ExecutionProvider::CoreML:   return "CoreML / Metal (Apple Silicon)";
            case ExecutionProvider::CUDA:     return "NVIDIA CUDA";
            case ExecutionProvider::DirectML: return "Microsoft DirectML";
            case ExecutionProvider::TensorRT: return "NVIDIA TensorRT";
        }
        return "Unknown Provider";
    }

    // Verifies if the ONNX Runtime environment is fully initialized
    [[nodiscard]] static bool initialize_backend() noexcept;
};

} // namespace ai_studio::ai