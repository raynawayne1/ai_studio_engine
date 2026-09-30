#pragma once

#include <vector>
#include <string>
#include <memory>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

namespace ai_studio::ai {

struct JointCoordinates {
    float x;
    float y;
    float z;
    float confidence;
};

class BodyTrackerEngine {
public:
    BodyTrackerEngine() = delete;
    explicit BodyTrackerEngine(const std::string& model_path);
    ~BodyTrackerEngine() = default;

    // Process full-body frame, extracting skeletal joints (hands, arms, posture)
    // EXTREME OPTIMIZATION: Zero-allocation hot path
    void track_body(const cv::Mat& frame, std::vector<JointCoordinates>& out_joints) noexcept;

    bool is_ready() const noexcept { return m_session != nullptr; }

private:
    void pre_allocate_tensors() noexcept;
    
    // Internal kinematic checks for arms raising/lowering and posture validation
    void analyze_kinematics(const std::vector<JointCoordinates>& joints) noexcept;

    std::string m_model_path;
    std::unique_ptr<Ort::Session> m_session;
    Ort::MemoryInfo m_memory_info{Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)};
    
    // ZERO-LAG PRE-ALLOCATED BUFFERS
    std::vector<float> m_input_tensor_values;
    std::vector<int64_t> m_input_shape{1, 3, 256, 256}; // Standard BlazePose/MediaPipe Input
    
    // Output: 33 joints * 4 values (x, y, z, confidence)
    std::vector<int64_t> m_output_shape{1, 132}; 

    // Pre-allocated OpenCV matrices to prevent malloc() stalls during video loop
    cv::Mat m_resized_buffer;
    cv::Mat m_float_buffer;
};

} // namespace ai_studio::ai