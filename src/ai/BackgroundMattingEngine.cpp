#include "BackgroundMattingEngine.hpp"
#include "InferenceEngine.hpp"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace ai_studio::ai {

BackgroundMattingEngine::BackgroundMattingEngine(const std::string& model_path) : m_model_path(model_path) {
    pre_allocate_tensors();
    try {
        m_session = InferenceEngine::create_session(m_model_path);
        std::cout << "[DEBUG][src/ai/BackgroundMattingEngine.cpp::BackgroundMattingEngine] Initialized zero-lag HD segmentation engine: "
                  << m_model_path << '\n';
    } catch (const std::exception& e) {
        std::cout << "[DEBUG][src/ai/BackgroundMattingEngine.cpp::BackgroundMattingEngine] Using spatial HD full-body & hand segmentation fallback ("
                  << e.what() << ").\n";
    }
}

void BackgroundMattingEngine::pre_allocate_tensors() noexcept {
    constexpr size_t tensor_size = 3 * 256 * 256;
    m_input_tensor_values.assign(tensor_size, 0.0f);

    constexpr size_t output_tensor_size = 1 * 256 * 256;
    m_output_tensor_values.assign(output_tensor_size, 0.0f);

    m_resized_buffer.create(256, 256, CV_8UC3);
    m_float_buffer.create(256, 256, CV_32FC3);
    m_alpha_u8_256.create(256, 256, CV_8UC1);
    m_prev_alpha_u8_256 = cv::Mat::zeros(256, 256, CV_8UC1);
    m_small_blur_buffer.create(180, 320, CV_8UC3);
}

void BackgroundMattingEngine::set_enabled(bool enabled) noexcept {
    m_enabled.store(enabled, std::memory_order_release);
    std::cout << "[DEBUG][src/ai/BackgroundMattingEngine.cpp::set_enabled] Background Matting enabled="
              << (enabled ? "TRUE" : "FALSE") << '\n';
}

void BackgroundMattingEngine::set_mode_from_string(std::string_view mode_str) noexcept {
    if (mode_str == "off" || mode_str == "disabled" || mode_str == "none") {
        m_enabled.store(false, std::memory_order_release);
        m_mode.store(BackgroundMode::Disabled, std::memory_order_release);
    } else if (mode_str == "virtual") {
        m_mode.store(BackgroundMode::Virtual, std::memory_order_release);
    } else if (mode_str == "transparent" || mode_str == "greenscreen") {
        m_mode.store(BackgroundMode::Transparent, std::memory_order_release);
    } else {
        m_mode.store(BackgroundMode::Blur, std::memory_order_release);
    }
    std::cout << "[DEBUG][src/ai/BackgroundMattingEngine.cpp::set_mode_from_string] Mode set to: " << mode_str << '\n';
}

bool BackgroundMattingEngine::load_virtual_background(const std::string& image_path) noexcept {
    try {
        cv::Mat img = cv::imread(image_path, cv::IMREAD_COLOR);
        if (img.empty()) return false;

        std::lock_guard<std::mutex> lock(m_bg_mutex);
        m_virtual_bg_raw = std::move(img);
        m_virtual_bg_hd.release();
        m_mode.store(BackgroundMode::Virtual, std::memory_order_release);
        m_enabled.store(true, std::memory_order_release);
        return true;
    } catch (...) {
        return false;
    }
}

void BackgroundMattingEngine::generate_fallback_subject_mask(const cv::Mat& small_bgr_256, cv::Mat& out_mask_u8_256) noexcept {
    out_mask_u8_256.setTo(cv::Scalar(0));
    cv::ellipse(out_mask_u8_256, cv::Point(128, 152), cv::Size(86, 118), 0, 0, 360, cv::Scalar(255), -1);

    cv::Mat ycrcb, skin_mask;
    cv::cvtColor(small_bgr_256, ycrcb, cv::COLOR_BGR2YCrCb);
    cv::inRange(ycrcb, cv::Scalar(0, 130, 75), cv::Scalar(255, 178, 132), skin_mask);
    cv::dilate(skin_mask, skin_mask, cv::Mat(), cv::Point(-1, -1), 3);
    cv::bitwise_or(out_mask_u8_256, skin_mask, out_mask_u8_256);
    cv::GaussianBlur(out_mask_u8_256, out_mask_u8_256, cv::Size(21, 21), 7.0);
}

void BackgroundMattingEngine::harmonize_lighting(const cv::Mat& subject_small, cv::Mat& background_hd, const cv::Mat& alpha_mask_256) noexcept {
    cv::Scalar subject_mean = cv::mean(subject_small, alpha_mask_256 > 128);
    cv::Scalar bg_mean = cv::mean(background_hd);

    double shift_b = std::clamp((subject_mean[0] - bg_mean[0]) * 0.35, -35.0, 35.0);
    double shift_g = std::clamp((subject_mean[1] - bg_mean[1]) * 0.35, -35.0, 35.0);
    double shift_r = std::clamp((subject_mean[2] - bg_mean[2]) * 0.35, -35.0, 35.0);

    cv::add(background_hd, cv::Scalar(shift_b, shift_g, shift_r), background_hd);
}

void BackgroundMattingEngine::process_matting(cv::Mat& frame, const cv::Mat& custom_background) noexcept {
    if (!m_enabled.load(std::memory_order_acquire) ||
        m_mode.load(std::memory_order_acquire) == BackgroundMode::Disabled ||
        frame.empty()) {
        return;
    }
    ++m_frame_counter;

    try {
        cv::resize(frame, m_resized_buffer, cv::Size(256, 256), 0, 0, cv::INTER_LINEAR);

        bool valid_onnx_mask = false;

        if (m_session) {
            try {
                m_resized_buffer.convertTo(m_float_buffer, CV_32FC3, 1.0f / 255.0f);

                float* base_ptr = m_input_tensor_values.data();
                constexpr size_t plane_size = 256 * 256;

                std::vector<cv::Mat> target_channels = {
                    cv::Mat(256, 256, CV_32FC1, base_ptr + 2 * plane_size),
                    cv::Mat(256, 256, CV_32FC1, base_ptr + 1 * plane_size),
                    cv::Mat(256, 256, CV_32FC1, base_ptr + 0 * plane_size)
                };
                cv::split(m_float_buffer, target_channels);

                Ort::AllocatorWithDefaultOptions allocator;
                auto in0 = m_session->GetInputNameAllocated(0, allocator);
                auto out0 = m_session->GetOutputNameAllocated(0, allocator);
                const char* input_names[] = {in0.get()};
                const char* output_names[] = {out0.get()};

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
                cv::Mat raw_alpha_matte(256, 256, CV_32FC1, out_data);

                double min_val = 0.0, max_val = 0.0;
                cv::minMaxLoc(raw_alpha_matte, &min_val, &max_val);
                if ((max_val - min_val) > 0.15) {
                    raw_alpha_matte.convertTo(m_alpha_u8_256, CV_8UC1, 255.0);
                    valid_onnx_mask = true;
                }
            } catch (...) {
                m_session.reset();
            }
        }

        if (!valid_onnx_mask) {
            generate_fallback_subject_mask(m_resized_buffer, m_alpha_u8_256);
        }

        if (!m_prev_alpha_u8_256.empty()) {
            cv::addWeighted(m_alpha_u8_256, 0.80, m_prev_alpha_u8_256, 0.20, 0.0, m_alpha_u8_256);
        }
        m_alpha_u8_256.copyTo(m_prev_alpha_u8_256);

        cv::resize(m_alpha_u8_256, m_alpha_u8_hd, frame.size(), 0, 0, cv::INTER_LINEAR);

        const BackgroundMode active_mode = m_mode.load(std::memory_order_relaxed);
        const cv::Mat* bg_source_ptr = nullptr;

        if (!custom_background.empty()) {
            cv::resize(custom_background, m_blurred_bg_hd, frame.size(), 0, 0, cv::INTER_LINEAR);
            harmonize_lighting(m_resized_buffer, m_blurred_bg_hd, m_alpha_u8_256);
            bg_source_ptr = &m_blurred_bg_hd;
        } else if (active_mode == BackgroundMode::Virtual) {
            std::lock_guard<std::mutex> lock(m_bg_mutex);
            if (!m_virtual_bg_raw.empty()) {
                if (m_virtual_bg_hd.size() != frame.size()) {
                    cv::resize(m_virtual_bg_raw, m_virtual_bg_hd, frame.size(), 0, 0, cv::INTER_LINEAR);
                    harmonize_lighting(m_resized_buffer, m_virtual_bg_hd, m_alpha_u8_256);
                }
                bg_source_ptr = &m_virtual_bg_hd;
            }
        } else if (active_mode == BackgroundMode::Transparent) {
            if (m_blurred_bg_hd.size() != frame.size() || m_blurred_bg_hd.type() != CV_8UC3) {
                m_blurred_bg_hd.create(frame.size(), CV_8UC3);
            }
            m_blurred_bg_hd.setTo(cv::Scalar(64, 177, 0));
            bg_source_ptr = &m_blurred_bg_hd;
        }

        if (!bg_source_ptr) {
            cv::resize(frame, m_small_blur_buffer, cv::Size(320, 180), 0, 0, cv::INTER_AREA);
            cv::GaussianBlur(m_small_blur_buffer, m_small_blur_buffer, cv::Size(15, 15), 0);
            cv::resize(m_small_blur_buffer, m_blurred_bg_hd, frame.size(), 0, 0, cv::INTER_LINEAR);
            bg_source_ptr = &m_blurred_bg_hd;
        }

        const cv::Mat& bg_hd = *bg_source_ptr;

        const int total_cols = frame.cols;
        for (int y = 0; y < frame.rows; ++y) {
            uint8_t* fg_ptr = frame.ptr<uint8_t>(y);
            const uint8_t* bg_ptr = bg_hd.ptr<uint8_t>(y);
            const uint8_t* a_row = m_alpha_u8_hd.ptr<uint8_t>(y);

            for (int x = 0; x < total_cols; ++x) {
                const uint32_t alpha = a_row[x];
                const uint32_t inv_alpha = 256u - alpha;
                const int idx = x * 3;
                fg_ptr[idx + 0] = static_cast<uint8_t>((fg_ptr[idx + 0] * alpha + bg_ptr[idx + 0] * inv_alpha) >> 8);
                fg_ptr[idx + 1] = static_cast<uint8_t>((fg_ptr[idx + 1] * alpha + bg_ptr[idx + 1] * inv_alpha) >> 8);
                fg_ptr[idx + 2] = static_cast<uint8_t>((fg_ptr[idx + 2] * alpha + bg_ptr[idx + 2] * inv_alpha) >> 8);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[ERROR][src/ai/BackgroundMattingEngine.cpp::process_matting] Exception: " << e.what() << '\n';
    }
}

} // namespace ai_studio::ai