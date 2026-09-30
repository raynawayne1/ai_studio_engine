#pragma once

#include <string>
#include <vector>
#include <memory>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

namespace ai_studio::ai {

class BackgroundMattingEngine {
public:
    BackgroundMattingEngine() = delete;
    explicit BackgroundMattingEngine(const std::string& model_path);
    ~BackgroundMattingEngine() = default;

    // Generates real-time alpha matte, isolates subject, and blends with virtual background
    // EXTREME OPTIMIZATION: Zero-allocation SIMD pipeline
    void process_matting(cv::Mat& frame, const cv::Mat& custom_background = cv::Mat()) noexcept;

    bool is_ready() const noexcept { return m_session != nullptr; }

private:
    void pre_allocate_tensors() noexcept;
    
    // Analyzes the lighting on your real body and adjusts the fake background to match it perfectly
    void harmonize_lighting(const cv::Mat& subject, cv::Mat& background, const cv::Mat& alpha_mask) noexcept;

    std::string m_model_path;
    std::unique_ptr<Ort::Session> m_session;
    Ort::MemoryInfo m_memory_info{Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)};
    
    // ZERO-LAG PRE-ALLOCATED BUFFERS
    std::vector<float> m_input_tensor_values;
    std::vector<float> m_output_tensor_values;
    
    // Standard fast-matting input/output shapes (256x256 for extreme speed)
    std::vector<int64_t> m_input_shape{1, 3, 256, 256}; 
    std::vector<int64_t> m_output_shape{1, 1, 256, 256}; 

    // Pre-allocated OpenCV structural buffers to prevent ANY memory allocation in the live video loop
    cv::Mat m_resized_buffer;
    cv::Mat m_float_buffer;
    cv::Mat m_alpha_matte_256;
    cv::Mat m_alpha_matte_hd;
    cv::Mat m_frame_float;
    cv::Mat m_bg_float;
    cv::Mat m_alpha_3c;
    cv::Mat m_inverse_alpha_3c;
};

} // namespace ai_studio::ai