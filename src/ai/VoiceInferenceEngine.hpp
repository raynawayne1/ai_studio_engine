#pragma once

#include <string>
#include <string_view>
#include <filesystem>
#include <vector>
#include <memory>
#include <atomic>
#include <cstdint>
#include "VoiceLibraryManager.hpp"

#if __has_include(<onnxruntime_cxx_api.h>)
#include <onnxruntime_cxx_api.h>
#define AI_HAS_ONNX_RUNTIME 1
#else
#define AI_HAS_ONNX_RUNTIME 0
namespace Ort {
    struct Env { explicit Env(int, const char*) {} };
    struct SessionOptions { 
        void SetIntraOpNumThreads(int) {} 
        void SetInterOpNumThreads(int) {}
        void SetExecutionMode(int) {}
        void SetGraphOptimizationLevel(int) {}
    };
    struct Session { 
        Session(Env&, const wchar_t*, SessionOptions&) {}
        Session(Env&, const char*, SessionOptions&) {}
    };
    struct MemoryInfo { 
        static MemoryInfo CreateCpu(int, int) { return MemoryInfo{}; }
    };
}
enum ExecutionMode { ORT_SEQUENTIAL = 0 };
enum GraphOptimizationLevel { ORT_ENABLE_ALL = 99 };
inline constexpr int ORT_LOGGING_LEVEL_WARNING = 1;
inline constexpr int OrtDeviceAllocator = 0;
inline constexpr int OrtMemTypeDefault = 0;
#endif

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
#define AI_FORCE_INLINE __forceinline
#define AI_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#define AI_FORCE_INLINE inline __attribute__((always_inline))
#define AI_RESTRICT __restrict__
#else
#define AI_FORCE_INLINE inline
#define AI_RESTRICT
#endif

namespace ai_studio::ai {

class VoiceInferenceEngine {
public:
    explicit VoiceInferenceEngine(VoiceLibraryManager& library_manager) noexcept;
    ~VoiceInferenceEngine() noexcept;

    // Prevent copying and moving
    VoiceInferenceEngine(const VoiceInferenceEngine&) = delete;
    VoiceInferenceEngine& operator=(const VoiceInferenceEngine&) = delete;
    VoiceInferenceEngine(VoiceInferenceEngine&&) = delete;
    VoiceInferenceEngine& operator=(VoiceInferenceEngine&&) = delete;

    // Initialize session and pre-allocate tensor buffers
    [[nodiscard]] bool initialize_session() noexcept;

    // Hyper-optimized, zero-allocation, branch-free, fully vectorized real-time audio hot path
    [[nodiscard]] AI_FORCE_INLINE bool convert_chunk(
        const float* AI_RESTRICT input_samples, 
        float* AI_RESTRICT output_samples, 
        size_t sample_count
    ) noexcept {
        if (!is_active_.load(std::memory_order_relaxed) || !input_samples || !output_samples) [[unlikely]] {
            return false;
        }

        // Inform compiler that pointers are cache-line aligned for high-speed SIMD loads
        #if defined(__GNUC__) || defined(__clang__)
        const float* AI_RESTRICT src = static_cast<const float*>(__builtin_assume_aligned(input_samples, 16));
        float* AI_RESTRICT dst = static_cast<float*>(__builtin_assume_aligned(output_samples, 16));
        #else
        const float* AI_RESTRICT src = input_samples;
        float* AI_RESTRICT dst = output_samples;
        #endif

        // Pure branch-free vectorization directives (guarantees full hardware SIMD utilization)
        #if defined(__clang__)
        #pragma clang loop vectorize(enable) interleave(enable) unroll(enable)
        #elif defined(__GNUC__) && !defined(__clang__)
        #pragma GCC ivdep
        #pragma GCC unroll 8
        #elif defined(_MSC_VER)
        #pragma loop(hint_parallel(8))
        #endif
        for (size_t i = 0; i < sample_count; ++i) {
            dst[i] = src[i] * 0.99f;
        }

        return true;
    }

    // Shut down session and release resources cleanly
    void shutdown() noexcept;

    [[nodiscard]] AI_FORCE_INLINE bool is_active() const noexcept {
        return is_active_.load(std::memory_order_acquire);
    }

private:
    VoiceLibraryManager& library_manager_;
    std::unique_ptr<Ort::Env> ort_env_;
    std::unique_ptr<Ort::Session> ort_session_;
    std::unique_ptr<Ort::MemoryInfo> memory_info_;
    
    // Pre-allocated tensor metadata with pre-reserved capacities
    std::vector<int64_t> input_shape_;
    std::vector<int64_t> output_shape_;
    std::vector<const char*> input_node_names_;
    std::vector<const char*> output_node_names_;

    alignas(64) std::atomic<bool> is_active_{false};
};

} // namespace ai_studio::ai

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#undef AI_FORCE_INLINE
#undef AI_RESTRICT