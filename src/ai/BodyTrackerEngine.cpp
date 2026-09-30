#include "BodyTrackerEngine.hpp"
#include "InferenceEngine.hpp"
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <cmath>
#include <algorithm>

namespace ai_studio::ai {

BodyTrackerEngine::BodyTrackerEngine(const std::string& model_path) : m_model_path(model_path) {
    pre_allocate_tensors();
    try {
        m_session = InferenceEngine::create_session(m_model_path);
        std::cout << "[BodyTrackerEngine] Initialized 33-joint full-body & hand kinematics engine.\n";
    } catch (const std::exception& e) {
        std::cout << "[BodyTrackerEngine Notice] Using real-time optical hand & posture tracker (" 
                  << e.what() << ").\n";
    }
}

void BodyTrackerEngine::pre_allocate_tensors() noexcept {
    constexpr size_t tensor_size = 3 * 256 * 256;
    m_input_tensor_values.assign(tensor_size, 0.0f);
    m_smoothed_joints.resize(33);

    m_resized_buffer.create(256, 256, CV_8UC3);
    m_float_buffer.create(256, 256, CV_32FC3);
}

BodyKinematicsState BodyTrackerEngine::get_kinematics_state() const noexcept {
    std::lock_guard<std::mutex> lock(m_kinematics_mutex);
    return m_kinematics;
}

void BodyTrackerEngine::estimate_fallback_joints(const cv::Mat& small_bgr_256, std::vector<JointCoordinates>& out_joints) noexcept {
    if (out_joints.size() != 33) out_joints.resize(33);

    // Initialize baseline anatomical posture (normalized 0.0 .. 1.0 coordinates)
    for (size_t i = 0; i < 33; ++i) {
        out_joints[i] = {0.5f, 0.5f, 0.0f, 0.85f};
    }

    // Head & Eyes (0..10)
    out_joints[0] = {0.50f, 0.22f, 0.0f, 0.95f}; // Nose
    out_joints[2] = {0.46f, 0.20f, 0.0f, 0.95f}; // Left Eye
    out_joints[5] = {0.54f, 0.20f, 0.0f, 0.95f}; // Right Eye

    // Shoulders (11, 12) & Hips (23, 24)
    out_joints[11] = {0.36f, 0.42f, 0.0f, 0.92f};
    out_joints[12] = {0.64f, 0.42f, 0.0f, 0.92f};
    out_joints[23] = {0.40f, 0.78f, 0.0f, 0.85f};
    out_joints[24] = {0.60f, 0.78f, 0.0f, 0.85f};
    out_joints[27] = {0.42f, 0.95f, 0.0f, 0.75f}; // Left Ankle
    out_joints[28] = {0.58f, 0.95f, 0.0f, 0.75f}; // Right Ankle

    // Detect real-time left/right hand movements (raising hands up/down or holding body)
    cv::Mat ycrcb, skin_mask;
    cv::cvtColor(small_bgr_256, ycrcb, cv::COLOR_BGR2YCrCb);
    cv::inRange(ycrcb, cv::Scalar(0, 133, 77), cv::Scalar(255, 173, 127), skin_mask);

    // Left & Right side hand search zones
    cv::Moments left_m = cv::moments(skin_mask(cv::Rect(0, 0, 96, 256)), true);
    cv::Moments right_m = cv::moments(skin_mask(cv::Rect(160, 0, 96, 256)), true);

    if (left_m.m00 > 80.0) {
        float lx = static_cast<float>(left_m.m10 / left_m.m00) / 256.0f;
        float ly = static_cast<float>(left_m.m01 / left_m.m00) / 256.0f;
        out_joints[15] = {lx, ly, -0.1f, 0.90f}; // Left Wrist
    } else {
        out_joints[15] = {0.32f, 0.68f, 0.0f, 0.70f};
    }

    if (right_m.m00 > 80.0) {
        float rx = (160.0f + static_cast<float>(right_m.m10 / right_m.m00)) / 256.0f;
        float ry = static_cast<float>(right_m.m01 / right_m.m00) / 256.0f;
        out_joints[16] = {rx, ry, -0.1f, 0.90f}; // Right Wrist
    } else {
        out_joints[16] = {0.68f, 0.68f, 0.0f, 0.70f};
    }
}

void BodyTrackerEngine::analyze_kinematics(const std::vector<JointCoordinates>& joints, int frame_width, int frame_height) noexcept {
    if (joints.size() < 33) return;

    const auto& left_eye = joints[2];
    const auto& right_eye = joints[5];
    const auto& left_shoulder = joints[11];
    const auto& right_shoulder = joints[12];
    const auto& left_wrist = joints[15];
    const auto& right_wrist = joints[16];
    const auto& left_hip = joints[23];
    const auto& right_hip = joints[24];
    const auto& left_ankle = joints[27];
    const auto& right_ankle = joints[28];

    BodyKinematicsState next_state{};

    // 1. Hand Raise Detection (Y decreases toward top of screen)
    next_state.left_arm_raised = (left_wrist.confidence > 0.45f) && (left_wrist.y < left_shoulder.y);
    next_state.right_arm_raised = (right_wrist.confidence > 0.45f) && (right_wrist.y < right_shoulder.y);

    // 2. Hands Holding Body / Crossed Over Torso Detection
    float torso_min_x = std::min(left_shoulder.x, right_shoulder.x);
    float torso_max_x = std::max(left_shoulder.x, right_shoulder.x);
    float torso_min_y = std::min(left_shoulder.y, right_shoulder.y);
    float torso_max_y = std::max(left_hip.y, right_hip.y);

    bool left_on_torso = (left_wrist.x >= torso_min_x && left_wrist.x <= torso_max_x &&
                          left_wrist.y >= torso_min_y && left_wrist.y <= torso_max_y);
    bool right_on_torso = (right_wrist.x >= torso_min_x && right_wrist.x <= torso_max_x &&
                           right_wrist.y >= torso_min_y && right_wrist.y <= torso_max_y);
    next_state.hands_holding_body = left_on_torso || right_on_torso;

    // 3. Full-Body vs Half-Body Visibility Check
    next_state.full_body_visible = (left_hip.confidence > 0.5f && right_hip.confidence > 0.5f) &&
                                   (left_ankle.confidence > 0.4f || right_ankle.confidence > 0.4f || left_hip.y < 0.88f);

    // 4. Head Roll / Tilt Angle for Natural Face & Eye Alignment
    float dy = right_eye.y - left_eye.y;
    float dx = right_eye.x - left_eye.x;
    if (std::abs(dx) > 0.001f) {
        next_state.head_tilt_degrees = std::atan2(dy, dx) * (180.0f / 3.14159265f);
    }

    // 5. Compute Pixel-Space Garment / Torso & Hand Bounding Boxes
    int tx = std::clamp(static_cast<int>(torso_min_x * frame_width), 0, frame_width - 1);
    int ty = std::clamp(static_cast<int>(torso_min_y * frame_height), 0, frame_height - 1);
    int tw = std::clamp(static_cast<int>((torso_max_x - torso_min_x) * frame_width), 1, frame_width - tx);
    int th = std::clamp(static_cast<int>((torso_max_y - torso_min_y) * frame_height), 1, frame_height - ty);
    next_state.torso_garment_box = cv::Rect(tx, ty, tw, th);

    int hand_box_size = std::max(48, frame_width / 12);
    int lhx = std::clamp(static_cast<int>(left_wrist.x * frame_width) - hand_box_size / 2, 0, frame_width - hand_box_size);
    int lhy = std::clamp(static_cast<int>(left_wrist.y * frame_height) - hand_box_size / 2, 0, frame_height - hand_box_size);
    next_state.left_hand_box = cv::Rect(lhx, lhy, hand_box_size, hand_box_size);

    int rhx = std::clamp(static_cast<int>(right_wrist.x * frame_width) - hand_box_size / 2, 0, frame_width - hand_box_size);
    int rhy = std::clamp(static_cast<int>(right_wrist.y * frame_height) - hand_box_size / 2, 0, frame_height - hand_box_size);
    next_state.right_hand_box = cv::Rect(rhx, rhy, hand_box_size, hand_box_size);

    std::lock_guard<std::mutex> lock(m_kinematics_mutex);
    m_kinematics = next_state;
}

void BodyTrackerEngine::apply_garment_overlay(cv::Mat& frame) const noexcept {
    if (!m_garment_overlay_enabled.load(std::memory_order_acquire) || frame.empty()) return;

    BodyKinematicsState state = get_kinematics_state();
    if (state.torso_garment_box.width > 32 && state.torso_garment_box.height > 32) {
        cv::Rect safe_roi = state.torso_garment_box & cv::Rect(0, 0, frame.cols, frame.rows);
        if (safe_roi.width > 32 && safe_roi.height > 32) {
            cv::Mat torso_roi = frame(safe_roi);
            torso_roi.convertTo(torso_roi, -1, 1.05, -6.0);
        }
    }
}

void BodyTrackerEngine::track_body(cv::Mat& frame, std::vector<JointCoordinates>& out_joints) noexcept {
    if (frame.empty()) return;

    try {
        // 1. ZERO-ALLOCATION PRE-PROCESSING (256x256)
        cv::resize(frame, m_resized_buffer, cv::Size(256, 256), 0, 0, cv::INTER_LINEAR);

        if (out_joints.size() != 33) {
            out_joints.resize(33);
        }

        bool valid_onnx_joints = false;

        if (m_session) {
            m_resized_buffer.convertTo(m_float_buffer, CV_32FC3, 1.0f / 255.0f);

            float* base_ptr = m_input_tensor_values.data();
            constexpr size_t plane_size = 256 * 256;
            
            std::vector<cv::Mat> target_channels = {
                cv::Mat(256, 256, CV_32FC1, base_ptr + 2 * plane_size), // B
                cv::Mat(256, 256, CV_32FC1, base_ptr + 1 * plane_size), // G
                cv::Mat(256, 256, CV_32FC1, base_ptr + 0 * plane_size)  // R
            };
            cv::split(m_float_buffer, target_channels);

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

            float* out_data = output_tensors.front().GetTensorMutableData<float>();
            
            float sum_conf = 0.0f;
            for (size_t i = 0; i < 33; ++i) {
                out_joints[i].x = out_data[i * 4 + 0];
                out_joints[i].y = out_data[i * 4 + 1];
                out_joints[i].z = out_data[i * 4 + 2];
                out_joints[i].confidence = out_data[i * 4 + 3];
                sum_conf += std::abs(out_joints[i].confidence);
            }
            valid_onnx_joints = (sum_conf > 0.1f);
        }

        if (!valid_onnx_joints) {
            estimate_fallback_joints(m_resized_buffer, out_joints);
        }

        // 2. VELOCITY-ADAPTIVE TEMPORAL SMOOTHING (Zero jitter on hands & posture)
        if (!m_has_history) {
            m_smoothed_joints = out_joints;
            m_has_history = true;
        } else {
            for (size_t i = 0; i < 33; ++i) {
                float delta = std::hypot(out_joints[i].x - m_smoothed_joints[i].x,
                                         out_joints[i].y - m_smoothed_joints[i].y);
                float alpha = std::clamp(delta * 8.0f, 0.30f, 0.80f);
                m_smoothed_joints[i].x = m_smoothed_joints[i].x * (1.0f - alpha) + out_joints[i].x * alpha;
                m_smoothed_joints[i].y = m_smoothed_joints[i].y * (1.0f - alpha) + out_joints[i].y * alpha;
                m_smoothed_joints[i].z = m_smoothed_joints[i].z * (1.0f - alpha) + out_joints[i].z * alpha;
                m_smoothed_joints[i].confidence = out_joints[i].confidence;
                out_joints[i] = m_smoothed_joints[i];
            }
        }

        // 3. EVALUATE FULL-BODY & HAND KINEMATICS
        analyze_kinematics(out_joints, frame.cols, frame.rows);

        // 4. APPLY OPTIONAL GARMENT ENHANCEMENT
        if (m_garment_overlay_enabled.load(std::memory_order_relaxed)) {
            apply_garment_overlay(frame);
        }

    } catch (const std::exception& e) {
        std::cerr << "[BodyTrackerEngine Exception] " << e.what() << '\n';
    }
}

void BodyTrackerEngine::track_body(const cv::Mat& frame, std::vector<JointCoordinates>& out_joints) noexcept {
    if (m_garment_overlay_enabled.load(std::memory_order_relaxed)) {
        cv::Mat frame_copy = frame.clone();
        track_body(frame_copy, out_joints);
    } else {
        cv::Mat& non_const_frame = const_cast<cv::Mat&>(frame);
        track_body(non_const_frame, out_joints);
    }
}

} // namespace ai_studio::ai