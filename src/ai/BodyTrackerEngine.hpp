#pragma once

#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

namespace ai_studio::ai {

struct JointCoordinates {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float confidence{0.0f};
};

struct BodyKinematicsState {
    bool left_arm_raised{false};
    bool right_arm_raised{false};
    bool hands_holding_body{false};
    bool full_body_visible{false};
    bool eyes_blinking{false};
    float head_tilt_degrees{0.0f};
    cv::Rect torso_garment_box{0, 0, 0, 0};
    cv::Rect left_hand_box{0, 0, 0, 0};
    cv::Rect right_hand_box{0, 0, 0, 0};
};

class BodyTrackerEngine {
public:
    BodyTrackerEngine() = delete;
    explicit BodyTrackerEngine(const std::string& model_path);
    ~BodyTrackerEngine() noexcept = default;

    // Process full-body frame, extracting 33 skeletal joints (face, hands, arms, torso, full body)
    void track_body(cv::Mat& frame, std::vector<JointCoordinates>& out_joints) noexcept;
    void track_body(const cv::Mat& frame, std::vector<JointCoordinates>& out_joints) noexcept;

    // Skin-safe garment / cloth color transformation on the torso
    void apply_garment_overlay(cv::Mat& frame) const noexcept;

    void set_garment_overlay_enabled(bool enabled) noexcept;
    [[nodiscard]] bool is_garment_overlay_enabled() const noexcept {
        return m_garment_overlay_enabled.load(std::memory_order_acquire);
    }

    [[nodiscard]] BodyKinematicsState get_kinematics_state() const noexcept;
    [[nodiscard]] bool is_ready() const noexcept { return true; }

private:
    void pre_allocate_tensors() noexcept;
    void analyze_kinematics(const std::vector<JointCoordinates>& joints, int frame_width, int frame_height) noexcept;
    void estimate_fallback_joints(const cv::Mat& small_bgr_256, std::vector<JointCoordinates>& out_joints) noexcept;

    std::string m_model_path;
    std::unique_ptr<Ort::Session> m_session{nullptr};
    Ort::MemoryInfo m_memory_info{Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)};

    // ZERO-LAG PRE-ALLOCATED BUFFERS
    std::vector<float> m_input_tensor_values;
    std::vector<int64_t> m_input_shape{1, 3, 256, 256};
    std::vector<int64_t> m_output_shape{1, 132};

    std::vector<JointCoordinates> m_smoothed_joints;
    bool m_has_history{false};

    std::atomic<bool> m_garment_overlay_enabled{false};
    BodyKinematicsState m_kinematics;
    mutable std::mutex m_kinematics_mutex;

    cv::Mat m_resized_buffer;
    cv::Mat m_float_buffer;
    uint64_t m_frame_counter{0};
};

} // namespace ai_studio::ai