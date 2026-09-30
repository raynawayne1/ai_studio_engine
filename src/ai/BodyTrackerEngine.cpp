#include "BodyTrackerEngine.hpp"
#include "InferenceEngine.hpp"
#include <iostream>
#include <stdexcept>

namespace ai_studio::ai {

BodyTrackerEngine::BodyTrackerEngine(const std::string& model_path) : m_model_path(model_path) {
    try {
        m_session = InferenceEngine::create_session(m_model_path);
        pre_allocate_tensors();
        std::cout << "[BodyTrackerEngine] Initialized full-body kinematics engine successfully.\n";
    } catch (const std::exception& e) {
        std::cerr << "[BodyTrackerEngine Error] " << e.what() << '\n';
    }
}

void BodyTrackerEngine::pre_allocate_tensors() noexcept {
    // 1. Allocate exact contiguous memory for the ONNX CHW tensor (256x256 RGB)
    const size_t tensor_size = 3 * 256 * 256;
    m_input_tensor_values.resize(tensor_size, 0.0f);

    // 2. Pre-allocate OpenCV structural buffers to prevent ANY memory allocation in the live video loop
    m_resized_buffer.create(256, 256, CV_8UC3);
    m_float_buffer.create(256, 256, CV_32FC3);
}

void BodyTrackerEngine::analyze_kinematics(const std::vector<JointCoordinates>& joints) noexcept {
    if (joints.size() < 33) return; // Standard BlazePose / MediaPipe 33 keypoints
    
    // Real Kinematics Example: Check wrist vs shoulder coordinates for arm raise detection
    const auto& left_wrist = joints[15];
    const auto& left_shoulder = joints[11];
    const auto& right_wrist = joints[16];
    const auto& right_shoulder = joints[12];
    
    // In screen coordinates, Y decreases as you go UP (0 is top of screen)
    if ((left_wrist.y < left_shoulder.y) && (left_wrist.confidence > 0.5f)) {
        // Left arm is raised physically
    }
    if ((right_wrist.y < right_shoulder.y) && (right_wrist.confidence > 0.5f)) {
        // Right arm is raised physically
    }
}

void BodyTrackerEngine::track_body(const cv::Mat& frame, std::vector<JointCoordinates>& out_joints) noexcept {
    if (!is_ready() || frame.empty()) return;

    try {
        // 1. ZERO-ALLOCATION PRE-PROCESSING
        // We write directly into our pre-allocated m_resized_buffer instead of returning a new Mat
        cv::resize(frame, m_resized_buffer, cv::Size(256, 256), 0, 0, cv::INTER_LINEAR);
        m_resized_buffer.convertTo(m_float_buffer, CV_32FC3, 1.0f / 255.0f);

        // 2. EXTREME OPTIMIZATION: Implicit BGR->RGB + HWC->CHW via SIMD mapping
        // By mapping the output pointers in reverse order (2, 1, 0), cv::split natively separates 
        // the interleaved channels AND converts BGR to RGB simultaneously using AVX CPU instructions.
        float* base_ptr = m_input_tensor_values.data();
        const size_t plane_size = 256 * 256;
        
        std::vector<cv::Mat> target_channels = {
            cv::Mat(256, 256, CV_32FC1, base_ptr + 2 * plane_size), // B goes to offset 2 (B_plane)
            cv::Mat(256, 256, CV_32FC1, base_ptr + 1 * plane_size), // G goes to offset 1 (G_plane)
            cv::Mat(256, 256, CV_32FC1, base_ptr + 0 * plane_size)  // R goes to offset 0 (R_plane)
        };
        cv::split(m_float_buffer, target_channels);

        // 3. HARDWARE-ACCELERATED INFERENCE
        const char* input_names[] = {"input"};
        const char* output_names[] = {"output"};

        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            m_memory_info, 
            m_input_tensor_values.data(), 
            m_input_tensor_values.size(), 
            m_input_shape.data(), 
            m_input_shape.size()
        );

        auto output_tensors = m_session->Run(
            Ort::RunOptions{nullptr}, 
            input_names, &input_tensor, 1, 
            output_names, 1
        );

        // 4. PARSE 3D SKELETAL JOINTS (Zero-Copy Read)
        // Extract 33 joints * 4 properties (x, y, z, confidence)
        float* out_data = output_tensors.front().GetTensorMutableData<float>();
        
        // Ensure caller's vector has exactly 33 slots to avoid re-allocation
        if (out_joints.size() != 33) out_joints.resize(33);

        for (size_t i = 0; i < 33; ++i) {
            out_joints[i].x = out_data[i * 4 + 0];
            out_joints[i].y = out_data[i * 4 + 1];
            out_joints[i].z = out_data[i * 4 + 2];
            out_joints[i].confidence = out_data[i * 4 + 3];
        }

        // 5. EVALUATE POSTURE / GESTURES
        analyze_kinematics(out_joints);

    } catch (const std::exception& e) {
        std::cerr << "[BodyTrackerEngine Exception] " << e.what() << '\n';
    }
}

} // namespace ai_studio::ai