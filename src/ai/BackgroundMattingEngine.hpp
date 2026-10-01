#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

namespace ai_studio::ai {

enum class BackgroundMode {
    Disabled,
    Blur,
    Virtual,
    Transparent
};

class BackgroundMattingEngine {
public:
    BackgroundMattingEngine() = delete;
    explicit BackgroundMattingEngine(const std::string& model_path);
    ~BackgroundMattingEngine() noexcept = default;

    // Generates real-time alpha matte, isolates full body + raised hands, and blends background
    void process_matting(cv::Mat& frame, const cv::Mat& custom_background = cv::Mat()) noexcept;

    void set_enabled(bool enabled) noexcept;
    [[nodiscard]] bool is_enabled() const noexcept { return m_enabled.load(std::memory_order_acquire); }

    void set_mode(BackgroundMode mode) noexcept { m_mode.store(mode, std::memory_order_release); }
    void set_mode_from_string(std::string_view mode_str) noexcept;
    [[nodiscard]] BackgroundMode get_mode() const noexcept { return m_mode.load(std::memory_order_acquire); }

    bool load_virtual_background(const std::string& image_path) noexcept;

    [[nodiscard]] bool is_ready() const noexcept { return m_session != nullptr; }

private:
    void pre_allocate_tensors() noexcept;
    void harmonize_lighting(const cv::Mat& subject_small, cv::Mat& background_hd, const cv::Mat& alpha_mask_256) noexcept;
    void generate_fallback_subject_mask(const cv::Mat& small_bgr_256, cv::Mat& out_mask_u8_256) noexcept;

    std::string m_model_path;
    std::unique_ptr<Ort::Session> m_session{nullptr};
    Ort::MemoryInfo m_memory_info{Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)};

    std::atomic<bool> m_enabled{false};
    std::atomic<BackgroundMode> m_mode{BackgroundMode::Blur};

    std::vector<float> m_input_tensor_values;
    std::vector<float> m_output_tensor_values;

    std::vector<int64_t> m_input_shape{1, 3, 256, 256};
    std::vector<int64_t> m_output_shape{1, 1, 256, 256};

    cv::Mat m_resized_buffer;
    cv::Mat m_float_buffer;
    cv::Mat m_alpha_u8_256;
    cv::Mat m_prev_alpha_u8_256;
    cv::Mat m_alpha_u8_hd;
    cv::Mat m_small_blur_buffer;
    cv::Mat m_blurred_bg_hd;
    cv::Mat m_virtual_bg_raw;
    cv::Mat m_virtual_bg_hd;
    mutable std::mutex m_bg_mutex;
    uint64_t m_frame_counter{0};
};

} // namespace ai_studio::ai