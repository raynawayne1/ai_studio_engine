#pragma once

#include <atomic>
#include <thread>
#include <functional>
#include <string>
#include <opencv2/opencv.hpp>

namespace ai_studio::video {

class VirtualCameraManager {
public:
    VirtualCameraManager() noexcept;
    ~VirtualCameraManager() noexcept;

    // Starts capturing from physical webcam and routing to OS virtual camera
    bool start_capture(int camera_index = 0) noexcept;
    
    // Cleanly shuts down hardware and unmaps shared memory
    void stop_capture() noexcept;

    // Attaches the Zero-Lag AI Pipeline (FaceSwap, Body Tracking, Matting)
    void set_ai_processing_callback(std::function<void(cv::Mat&)> callback) noexcept;

    bool is_running() const noexcept { return m_running.load(std::memory_order_acquire); }

private:
    void capture_loop() noexcept;
    
    // EXTREME OPTIMIZATION: Hardware-accelerated memory-mapped OS routing
    bool initialize_virtual_device(int width, int height, int fps) noexcept;
    void route_to_virtual_camera(const cv::Mat& frame) noexcept;
    void shutdown_virtual_device() noexcept;

    cv::VideoCapture m_cap;
    std::atomic<bool> m_running{false};
    std::thread m_capture_thread;
    std::function<void(cv::Mat&)> m_ai_callback;

    // Fixed High-Definition, Low-Latency configuration
    const int m_width{1280};
    const int m_height{720};
    const int m_fps{30};
    
    // OS-Specific Shared Memory Handles (For Zoom, Discord, OBS Virtual Cam)
    void* m_shared_memory_handle{nullptr};
    uint8_t* m_mapped_buffer{nullptr};
    size_t m_frame_size_bytes{0};
};

} // namespace ai_studio::video