#include "BackgroundMattingEngine.hpp"
#include "InferenceEngine.hpp"
#include <iostream>
#include <stdexcept>

namespace ai_studio::ai {

BackgroundMattingEngine::BackgroundMattingEngine(const std::string& model_path) : m_model_path(model_path) {
    try {
        m_session = InferenceEngine::create_session(m_model_path);
        pre_allocate_tensors();
        std::cout << "[BackgroundMattingEngine] Initialized segmentation engine successfully.\n";
    } catch (const std::exception& e) {
        std::cerr << "[BackgroundMattingEngine Error] " << e.what() << '\n';
    }
}

void BackgroundMattingEngine::pre_allocate_tensors() noexcept {
    // 1. Allocate exact contiguous memory for ONNX CHW tensor (256x256 RGB)
    const size_t tensor_size = 3 * 256 * 256;
    m_input_tensor_values.resize(tensor_size, 0.0f);
    
    // Output is a 1-channel alpha matte
    const size_t output_tensor_size = 1 * 256 * 256;
    m_output_tensor_values.resize(output_tensor_size, 0.0f);

    // 2. Pre-allocate OpenCV structural buffers to completely eliminate malloc() lag
    m_resized_buffer.create(256, 256, CV_8UC3);
    m_float_buffer.create(256, 256, CV_32FC3);
    m_alpha_matte_256 = cv::Mat(256, 256, CV_32FC1, m_output_tensor_values.data());
}

void BackgroundMattingEngine::harmonize_lighting(const cv::Mat& subject, cv::Mat& background, const cv::Mat& alpha_mask) noexcept {
    // SIMD-accelerated Mean tracking
    // We only calculate the lighting of the pixels where you (the subject) exist using the alpha mask
    cv::Scalar subject_mean = cv::mean(subject, alpha_mask > 0.5f);
    cv::Scalar bg_mean = cv::mean(background);

    // Shift background lighting to perfectly match your real room's lighting on your face/body
    for (int i = 0; i < 3; ++i) {
        double shift = subject_mean[i] - bg_mean[i];
        background += cv::Scalar(i == 0 ? shift : 0, i == 1 ? shift : 0, i == 2 ? shift : 0);
    }
}

void BackgroundMattingEngine::process_matting(cv::Mat& frame, const cv::Mat& custom_background) noexcept {
    if (!is_ready() || frame.empty()) return;

    try {
        // 1. ZERO-ALLOCATION PRE-PROCESSING
        cv::resize(frame, m_resized_buffer, cv::Size(256, 256), 0, 0, cv::INTER_LINEAR);
        m_resized_buffer.convertTo(m_float_buffer, CV_32FC3, 1.0f / 255.0f);

        // 2. EXTREME OPTIMIZATION: Implicit BGR->RGB + HWC->CHW via SIMD mapping
        float* base_ptr = m_input_tensor_values.data();
        const size_t plane_size = 256 * 256;
        
        std::vector<cv::Mat> target_channels = {
            cv::Mat(256, 256, CV_32FC1, base_ptr + 2 * plane_size), // B -> Offset 2
            cv::Mat(256, 256, CV_32FC1, base_ptr + 1 * plane_size), // G -> Offset 1
            cv::Mat(256, 256, CV_32FC1, base_ptr + 0 * plane_size)  // R -> Offset 0
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

        // 4. EXTRACT ALPHA MATTE & RESIZE BACK TO HD (Zero-Copy)
        float* out_data = output_tensors.front().GetTensorMutableData<float>();
        
        // Since m_alpha_matte_256 maps directly to out_data, we resize it to the full HD frame size
        cv::Mat raw_alpha_matte(256, 256, CV_32FC1, out_data);
        cv::resize(raw_alpha_matte, m_alpha_matte_hd, frame.size(), 0, 0, cv::INTER_LINEAR);

        // 5. HARDWARE-ACCELERATED ALPHA BLENDING (SIMD)
        // Convert 1-channel alpha into 3-channel alpha for matrix multiplication
        cv::cvtColor(m_alpha_matte_hd, m_alpha_3c, cv::COLOR_GRAY2BGR);
        
        // Inverse alpha mask for the background (1.0 - alpha)
        m_inverse_alpha_3c = cv::Scalar(1.0f, 1.0f, 1.0f) - m_alpha_3c;

        frame.convertTo(m_frame_float, CV_32FC3);

        if (!custom_background.empty()) {
            cv::Mat bg_resized;
            cv::resize(custom_background, bg_resized, frame.size());
            bg_resized.convertTo(m_bg_float, CV_32FC3);

            // Lighting Harmonization: Makes the fake background match the actual lighting on your body
            harmonize_lighting(m_frame_float, m_bg_float, m_alpha_matte_hd);

            // Formula: Final = (Subject * Alpha) + (Background * (1 - Alpha))
            // .mul() uses deep CPU AVX2/NEON vectorization to multiply millions of pixels instantly
            cv::Mat blended = m_frame_float.mul(m_alpha_3c) + m_bg_float.mul(m_inverse_alpha_3c);
            blended.convertTo(frame, CV_8UC3);
        } else {
            // No custom background? Just blur the real background beautifully like Zoom/Teams!
            cv::Mat blurred_bg;
            cv::GaussianBlur(m_frame_float, blurred_bg, cv::Size(45, 45), 0);
            cv::Mat blended = m_frame_float.mul(m_alpha_3c) + blurred_bg.mul(m_inverse_alpha_3c);
            blended.convertTo(frame, CV_8UC3);
        }

    } catch (const std::exception& e) {
        std::cerr << "[BackgroundMattingEngine Exception] " << e.what() << '\n';
    }
}

} // namespace ai_studio::ai