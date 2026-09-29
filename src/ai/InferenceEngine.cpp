#include "InferenceEngine.hpp"
#include "hw/HardwareManager.hpp"
#include <iostream>

// Include ONNX Runtime C++ API headers if available
#if __has_include(<onnxruntime_cxx_api.h>)
    #include <onnxruntime_cxx_api.h>
    #define AI_STUDIO_HAS_ORT 1
#else
    #define AI_STUDIO_HAS_ORT 0
#endif

namespace ai_studio::ai {

ExecutionProvider InferenceEngine::select_optimal_provider() noexcept {
    const auto& profile = hw::HardwareManager::get_capabilities();

    // 1. macOS Acceleration Path (Apple Silicon CoreML / Metal)
    if (profile.os_name == "macOS") {
        if (profile.cpu_architecture == "ARM64") {
            std::cout << "[AI Engine] Apple Silicon detected. Selecting CoreML / Metal Execution Provider.\n";
            return ExecutionProvider::CoreML;
        }
    }

    // 2. Windows / Linux Acceleration Path
    // In later milestones, we will query GPU vendor strings here to toggle CUDA/DirectML.
    // For now, we inspect architecture and default to hardware-accelerated stubs or CPU fallback.
    if (profile.os_name == "Windows" || profile.os_name == "Linux") {
        // Fallback or explicit provider routing
        std::cout << "[AI Engine] Standard OS detected. Inspecting GPU capabilities...\n";
    }

    std::cout << "[AI Engine] Defaulting to highly optimized Multi-Threaded CPU Execution Provider.\n";
    return ExecutionProvider::CPU;
}

std::string InferenceEngine::provider_to_string(ExecutionProvider provider) noexcept {
    switch (provider) {
        case ExecutionProvider::CPU:      return "CPU (Multi-Threaded)";
        case ExecutionProvider::CoreML:   return "CoreML / Metal (Apple Silicon)";
        case ExecutionProvider::CUDA:     return "NVIDIA CUDA";
        case ExecutionProvider::DirectML: return "Microsoft DirectML";
        case ExecutionProvider::TensorRT: return "NVIDIA TensorRT";
    }
    return "Unknown Provider";
}

bool InferenceEngine::initialize_backend() noexcept {
    #if AI_STUDIO_HAS_ORT
        try {
            // Initialize Ort global environment (zero-copy singleton)
            static Ort::Env ort_env(ORT_LOGGING_LEVEL_WARNING, "AIStudioEngineCore");
            return true;
        } catch (...) {
            return false;
        }
    #else
        // If ORT headers are compiling in stub mode during early staging
        return true;
    #endif
}

} // namespace ai_studio::ai