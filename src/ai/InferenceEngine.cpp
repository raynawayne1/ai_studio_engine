#include "InferenceEngine.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cstring>

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

namespace ai_studio::ai {

std::unique_ptr<Ort::Env> InferenceEngine::s_env = nullptr;
ExecutionProvider InferenceEngine::s_active_provider = ExecutionProvider::CPU;

ExecutionProvider InferenceEngine::select_optimal_provider() noexcept {
#if defined(__APPLE__)
    return ExecutionProvider::CoreML; // Apple CoreML (Neural Engine / Metal GPU)
#elif defined(_WIN32)
    return ExecutionProvider::DirectML; // Windows DirectX 12 hardware acceleration
#else
    return ExecutionProvider::CUDA; // Linux NVIDIA hardware acceleration
#endif
}

std::string_view InferenceEngine::provider_to_string(ExecutionProvider provider) noexcept {
    switch (provider) {
        case ExecutionProvider::CPU: return "CPU (Vectorized Multi-Threaded)";
        case ExecutionProvider::CoreML: return "Apple CoreML (Neural Engine / GPU)";
        case ExecutionProvider::Metal: return "Apple Metal GPU";
        case ExecutionProvider::CUDA: return "NVIDIA CUDA";
        case ExecutionProvider::TensorRT: return "NVIDIA TensorRT";
        case ExecutionProvider::DirectML: return "Microsoft DirectML";
        default: return "Unknown";
    }
}

bool InferenceEngine::initialize_backend() noexcept {
    try {
        if (s_env) return true;

        s_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_FATAL, "AIStudioEngine_ZeroLag");
        s_env->DisableTelemetryEvents();

        s_active_provider = select_optimal_provider();
        std::cout << "[DEBUG][src/ai/InferenceEngine.cpp::initialize_backend] Native Backend initialized ("
                  << provider_to_string(s_active_provider) << ") with zero-lag parameters.\n";
        return true;
    } catch (const Ort::Exception& e) {
        std::cerr << "[ERROR][src/ai/InferenceEngine.cpp::initialize_backend] ONNX Fatal Error: " << e.what() << '\n';
        return false;
    }
}

void InferenceEngine::shutdown() noexcept {
    s_env.reset();
    std::cout << "[DEBUG][src/ai/InferenceEngine.cpp::shutdown] ONNX Runtime hardware resources released cleanly.\n";
}

bool InferenceEngine::is_initialized() noexcept {
    return s_env != nullptr;
}

Ort::Env& InferenceEngine::get_env() {
    if (!s_env) {
        if (!initialize_backend()) {
            throw std::runtime_error("ONNX Environment initialization failed.");
        }
    }
    return *s_env;
}

std::unique_ptr<Ort::Session> InferenceEngine::create_session(const std::string& model_path) {
    std::cout << "[DEBUG][src/ai/InferenceEngine.cpp::create_session] Verifying model file: " << model_path << '\n';
    if (!std::filesystem::exists(model_path)) {
        throw std::runtime_error("Model file not found on disk: " + model_path);
    }

    const auto file_bytes = std::filesystem::file_size(model_path);
    if (file_bytes < 128) {
        throw std::runtime_error("Model file is a lightweight header stub (" + std::to_string(file_bytes) + " bytes): " + model_path);
    }

    auto& env = get_env();
    Ort::SessionOptions session_options;

    session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    session_options.EnableMemPattern();
    session_options.EnableCpuMemArena();
    session_options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

    unsigned int hw_concurrency = std::thread::hardware_concurrency();
    unsigned int optimal_threads = (hw_concurrency > 2) ? (hw_concurrency - 1) : 1;
    session_options.SetIntraOpNumThreads(static_cast<int>(optimal_threads));
    session_options.SetInterOpNumThreads(1);

    try {
        const auto available = Ort::GetAvailableProviders();
        const auto has_provider = [&](std::string_view name) noexcept {
            return std::find(available.begin(), available.end(), name) != available.end();
        };

#if defined(__APPLE__)
        if (has_provider("CoreMLExecutionProvider")) {
            using AppendCoreMLFn = OrtStatus* (*)(OrtSessionOptions*, uint32_t);
            void* sym = dlsym(RTLD_DEFAULT, "OrtSessionOptionsAppendExecutionProvider_CoreML");
            if (sym != nullptr) {
                AppendCoreMLFn append_coreml = nullptr;
                std::memcpy(&append_coreml, &sym, sizeof(sym));
                OrtStatus* status = append_coreml(session_options, 0);
                if (status != nullptr) {
                    Ort::GetApi().ReleaseStatus(status);
                } else {
                    std::cout << "[DEBUG][src/ai/InferenceEngine.cpp::create_session] Attached CoreMLExecutionProvider for: "
                              << model_path << '\n';
                }
            }
        }
#endif

        if (has_provider("CUDAExecutionProvider")) {
            OrtCUDAProviderOptions cuda_opts{};
            cuda_opts.device_id = 0;
            session_options.AppendExecutionProvider_CUDA(cuda_opts);
            std::cout << "[DEBUG][src/ai/InferenceEngine.cpp::create_session] Attached CUDAExecutionProvider for: "
                      << model_path << '\n';
        } else if (has_provider("XnnpackExecutionProvider")) {
            std::unordered_map<std::string, std::string> xnn_options;
            xnn_options["intra_op_num_threads"] = std::to_string(optimal_threads);
            session_options.AppendExecutionProvider("XNNPACK", xnn_options);
        }
    } catch (const Ort::Exception& e) {
        std::cerr << "[WARN][src/ai/InferenceEngine.cpp::create_session] Hardware provider fallback to SIMD CPU: "
                  << e.what() << '\n';
    }

#if defined(_WIN32)
    std::wstring wide_path(model_path.begin(), model_path.end());
    auto sess = std::make_unique<Ort::Session>(env, wide_path.c_str(), session_options);
#else
    auto sess = std::make_unique<Ort::Session>(env, model_path.c_str(), session_options);
#endif
    std::cout << "[DEBUG][src/ai/InferenceEngine.cpp::create_session] Successfully created Ort::Session for: "
              << model_path << " (" << (file_bytes / 1024) << " KB)\n";
    return sess;
}

} // namespace ai_studio::ai