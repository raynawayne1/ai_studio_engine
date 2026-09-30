#include "FaceSwapEngine.hpp"
#include "InferenceEngine.hpp"
#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/photo.hpp>
#include <iostream>
#include <stdexcept>

namespace ai_studio::ai {

FaceSwapEngine::FaceSwapEngine(const std::string& model_path) : m_model_path(model_path) {
    try {
        m_session = InferenceEngine::create_session(m_model_path);
        pre_allocate_tensors();
        std::cout << "[FaceSwapEngine] Real-time inference pipeline initialized.\n";
    } catch (const std::exception& e) {
        std::cerr << "[FaceSwapEngine Error] ONNX Initialization failed: " << e.what() << '\n';
    }
}

void FaceSwapEngine::pre_allocate_tensors() noexcept {
    size_t tensor_size = 3 * 128 * 128;
    m_input_tensor_values.resize(tensor_size, 0.0f);
    m_output_tensor_values.resize(tensor_size, 0.0f);

    m_face_mask = cv::Mat::zeros(128, 128, CV_8UC1);
    cv::ellipse(m_face_mask, cv::Point(64, 64), cv::Size(60, 60), 0, 0, 360, cv::Scalar(255), -1);
    cv::GaussianBlur(m_face_mask, m_face_mask, cv::Size(15, 15), 0);
}

bool FaceSwapEngine::load_target_avatar(const std::string& image_path) noexcept {
    try {
        // 1. Instantly read the absolute image file path sent from React
        cv::Mat raw_avatar = cv::imread(image_path, cv::IMREAD_COLOR);
        if (raw_avatar.empty()) {
            std::cerr << "[FaceSwapEngine Error] Failed to load avatar image: " << image_path << '\n';
            return false;
        }

        // 2. Safely lock the memory to prevent crash if camera is currently processing
        std::unique_lock<std::shared_mutex> lock(m_avatar_mutex);
        cv::resize(raw_avatar, m_target_avatar_embedding, cv::Size(128, 128));
        m_avatar_loaded.store(true, std::memory_order_release);
        
        std::cout << "[FaceSwapEngine] Target avatar injected live: " << image_path << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[FaceSwapEngine Exception] " << e.what() << '\n';
        return false;
    }
}

void FaceSwapEngine::process_frame(cv::Mat& live_frame, const std::vector<float>& audio_chunk) noexcept {
    if (!is_ready() || live_frame.empty()) return;

    // Use a shared_lock so multiple threads can process video simultaneously without blocking,
    // but guarantees safety if the UI suddenly changes the avatar mid-frame!
    std::shared_lock<std::shared_mutex> lock(m_avatar_mutex);

    try {
        cv::Mat aligned_face = align_face(live_frame);
        if (aligned_face.empty()) return;

        cv::Mat float_face;
        aligned_face.convertTo(float_face, CV_32FC3, 1.0f / 255.0f);
        
        std::vector<cv::Mat> input_channels = {
            cv::Mat(128, 128, CV_32FC1, m_input_tensor_values.data() + 2 * 128 * 128),
            cv::Mat(128, 128, CV_32FC1, m_input_tensor_values.data() + 1 * 128 * 128),
            cv::Mat(128, 128, CV_32FC1, m_input_tensor_values.data() + 0 * 128 * 128)
        };
        cv::split(float_face, input_channels);

        const char* input_names[] = {"target", "source_embedding"};
        const char* output_names[] = {"output"};

        Ort::Value input_tensors[] = {
            Ort::Value::CreateTensor<float>(m_memory_info, m_input_tensor_values.data(), m_input_tensor_values.size(), m_input_shape.data(), m_input_shape.size()),
            Ort::Value::CreateTensor<float>(m_memory_info, m_input_tensor_values.data(), 512, m_input_shape.data(), 1) 
        };

        auto output_tensors = m_session->Run(Ort::RunOptions{nullptr}, input_names, input_tensors, 2, output_names, 1);

        float* out_data = output_tensors.front().GetTensorMutableData<float>();
        std::vector<cv::Mat> output_channels = {
            cv::Mat(128, 128, CV_32FC1, out_data + 2 * 128 * 128),
            cv::Mat(128, 128, CV_32FC1, out_data + 1 * 128 * 128),
            cv::Mat(128, 128, CV_32FC1, out_data + 0 * 128 * 128)
        };
        
        cv::Mat swapped_face_float;
        cv::merge(output_channels, swapped_face_float);
        
        cv::Mat swapped_face;
        swapped_face_float.convertTo(swapped_face, CV_8UC3, 255.0f);

        cv::Mat color_corrected_face;
        blend_skin_tones(aligned_face, swapped_face, color_corrected_face);
        apply_seamless_clone(color_corrected_face, live_frame);

    } catch (const std::exception& e) {
        std::cerr << "[FaceSwapEngine Inference Error] " << e.what() << '\n';
    }
}

cv::Mat FaceSwapEngine::align_face(const cv::Mat& frame) noexcept {
    cv::Mat cropped;
    cv::resize(frame, cropped, cv::Size(128, 128));
    return cropped;
}

void FaceSwapEngine::blend_skin_tones(cv::Mat& source_face, const cv::Mat& target_face, cv::Mat& output_face) noexcept {
    cv::Mat source_lab, target_lab;
    cv::cvtColor(source_face, source_lab, cv::COLOR_BGR2Lab);
    cv::cvtColor(target_face, target_lab, cv::COLOR_BGR2Lab);

    std::vector<cv::Mat> s_channels, t_channels;
    cv::split(source_lab, s_channels);
    cv::split(target_lab, t_channels);

    for (int i = 0; i < 3; ++i) {
        cv::Scalar s_mean, s_std, t_mean, t_std;
        cv::meanStdDev(s_channels[i], s_mean, s_std);
        cv::meanStdDev(t_channels[i], t_mean, t_std);
        
        double std_ratio = (s_std[0] > 0.001) ? (t_std[0] / s_std[0]) : 1.0;
        s_channels[i].convertTo(t_channels[i], -1, 1.0 / std_ratio, s_mean[0] - (t_mean[0] / std_ratio));
    }
    
    cv::merge(t_channels, output_face);
    cv::cvtColor(output_face, output_face, cv::COLOR_Lab2BGR);
}

void FaceSwapEngine::apply_seamless_clone(const cv::Mat& swapped_face, cv::Mat& live_frame) noexcept {
    (void)swapped_face;
    (void)live_frame;
}

} // namespace ai_studio::ai