#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <memory>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

namespace ai_studio::ai {

class FaceSwapEngine {
public:
    explicit FaceSwapEngine(const std::string& model_path);
    ~FaceSwapEngine() noexcept = default;

    // Load the target identity (the picture/avatar you want to look like)
    bool load_target_avatar(const std::string& image_path) noexcept;

    // Extremely fast, zero-allocation live frame processing
    // Applies Face Swap, Lip Sync, and Skin Blending inline directly to the frame
    void process_frame(cv::Mat& live_frame, const std::vector<float>& audio_chunk = {}) noexcept;

    bool is_ready() const noexcept { return m_avatar_loaded.load(std::memory_order_acquire) && m_session != nullptr; }

private:
    void pre_allocate_tensors() noexcept;
    cv::Mat align_face(const cv::Mat& frame) noexcept;
    void blend_skin_tones(cv::Mat& source_face, const cv::Mat& target_face, cv::Mat& output_face) noexcept;
    void apply_seamless_clone(const cv::Mat& swapped_face, cv::Mat& live_frame) noexcept;

    std::string m_model_path;
    std::unique_ptr<Ort::Session> m_session;
    Ort::MemoryInfo m_memory_info{Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)};
    
    // High-performance pre-allocated buffers (ZERO LAG / ZERO ALLOCATION)
    std::vector<float> m_input_tensor_values;
    std::vector<float> m_output_tensor_values;
    std::vector<int64_t> m_input_shape{1, 3, 128, 128}; // Standard inswapper input
    std::vector<int64_t> m_output_shape{1, 3, 128, 128};
    
    cv::Mat m_target_avatar_embedding;
    cv::Mat m_face_mask; // Pre-allocated blending mask
    std::atomic<bool> m_avatar_loaded{false};
};

} // namespace ai_studio::ai