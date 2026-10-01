#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>
#include <cstdint>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

namespace ai_studio::ai {

class FaceSwapEngine {
public:
    explicit FaceSwapEngine(const std::string& model_path);
    ~FaceSwapEngine() noexcept = default;

    // Load the target identity (the picture/avatar you want to look like)
    bool load_target_avatar(const std::string& image_path) noexcept;

    // Receives real-time head tilt, hand boxes, and blink state from BodyTrackerEngine
    void set_kinematics_context(
        float head_tilt_deg,
        const cv::Rect& left_hand,
        const cv::Rect& right_hand,
        bool eyes_blinking
    ) noexcept;

    // Extremely fast, zero-allocation live frame processing
    // Applies Face Swap, Lip Sync, Eye Blink, Hand Protection, and Skin Blending inline
    void process_frame(cv::Mat& live_frame, const std::vector<float>& audio_chunk = {}) noexcept;

    [[nodiscard]] bool is_ready() const noexcept {
        return m_avatar_loaded.load(std::memory_order_acquire);
    }

private:
    void pre_allocate_tensors() noexcept;
    cv::Rect detect_face_in_static_image(const cv::Mat& img) const noexcept;
    cv::Rect detect_and_track_face(const cv::Mat& frame) noexcept;
    void blend_skin_tones(const cv::Mat& source_face, const cv::Mat& target_face, cv::Mat& output_face) noexcept;
    void apply_realistic_lip_sync_and_blink(
        cv::Mat& harmonized_face_128,
        const cv::Mat& live_face_128,
        float audio_energy,
        bool blink_active
    ) noexcept;
    void composite_swapped_face(const cv::Mat& swapped_face, const cv::Rect& face_rect, cv::Mat& live_frame) noexcept;

    std::string m_model_path;
    std::unique_ptr<Ort::Session> m_session{nullptr};
    Ort::MemoryInfo m_memory_info{Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)};

    // High-performance pre-allocated buffers (ZERO LAG / ZERO RUNTIME ALLOCATION)
    std::vector<float> m_input_tensor_values;
    std::vector<float> m_output_tensor_values;
    std::vector<float> m_target_embedding_values;
    std::vector<int64_t> m_input_shape{1, 3, 128, 128};
    std::vector<int64_t> m_embedding_shape{1, 512};
    std::vector<int64_t> m_output_shape{1, 3, 128, 128};

    // Target Avatar Image Data
    cv::Mat m_target_avatar_rgb;
    cv::Mat m_target_avatar_face_128;
    mutable std::mutex m_avatar_mutex;
    std::atomic<bool> m_avatar_loaded{false};

    // Real-time kinematics context from BodyTrackerEngine
    std::atomic<float> m_head_tilt_deg{0.0f};
    std::atomic<bool> m_eyes_blinking{false};
    cv::Rect m_left_hand_box{0, 0, 0, 0};
    cv::Rect m_right_hand_box{0, 0, 0, 0};
    mutable std::mutex m_kin_mutex;

    // Pre-allocated feather mask & tracking state
    cv::Mat m_feather_mask_128;
    cv::Rect m_tracked_face_rect{0, 0, 0, 0};
    bool m_face_detected_previously{false};
    uint64_t m_frame_counter{0};
    float m_smoothed_mouth_open{0.0f};
};

} // namespace ai_studio::ai