#pragma once

#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <string>
#include <vector>
#include <opencv2/opencv.hpp>
#include "../hw/HardwareManager.hpp"

namespace ai_studio::video {

class VirtualCameraManager {
public:
    VirtualCameraManager() noexcept;
    ~VirtualCameraManager() noexcept;

    // Starts capturing from physical/virtual webcam index (with automatic fallback if index fails)
    bool start_capture(int camera_index = 0) noexcept;

    // Starts capturing directly from a USB Type-C stream URL, device path, or local video file
    bool start_capture(const std::string& stream_or_file_path) noexcept;

    // Dynamically auto-detects the fastest working camera or USB Type-C stream across OS
    bool start_auto_capture() noexcept;
    
    // Cleanly shuts down hardware and unmaps shared memory
    void stop_capture() noexcept;

    // Attaches the Zero-Lag AI Pipeline (FaceSwap, Body Tracking, Matting)
    void set_ai_processing_callback(std::function<void(cv::Mat&)> callback) noexcept;

    // Sets a custom fallback stream URL (e.g., USB Type-C tunnel or RTSP/HTTP endpoint)
    void set_fallback_stream_url(const std::string& url) noexcept;

    bool is_running() const noexcept { return m_running.load(std::memory_order_acquire); }
    double get_current_fps() const noexcept { return m_current_fps.load(std::memory_order_relaxed); }
    std::string get_active_source_name() const noexcept;

private:
    // Decoupled Dual-Thread Architecture for 0ms Driver Queue Lag
    void capture_loop() noexcept;
    void process_loop() noexcept;

    bool open_hardware_camera(int index) noexcept;
    bool open_stream_or_file(const std::string& endpoint) noexcept;
    void configure_zero_lag_capture(bool is_stream) noexcept;
    void start_worker_threads() noexcept;
    
    // EXTREME OPTIMIZATION: Hardware-accelerated memory-mapped OS routing
    bool initialize_virtual_device(int width, int height, int fps) noexcept;
    void route_to_virtual_camera(const cv::Mat& frame) noexcept;
    void shutdown_virtual_device() noexcept;

    cv::VideoCapture m_cap;
    std::atomic<bool> m_running{false};
    bool m_is_file_or_stream{false};
    std::string m_active_endpoint;
    std::string m_active_source_name{"None"};
    std::string m_custom_fallback_url;
    mutable std::mutex m_state_mutex;

    std::thread m_capture_thread;
    std::thread m_process_thread;
    std::function<void(cv::Mat&)> m_ai_callback;

    // Lock-Free / Zero-Copy Triple Buffering State
    cv::Mat m_triple_buffers[3];
    int m_write_idx{0};
    int m_shared_idx{1};
    int m_read_idx{2};
    std::atomic<bool> m_has_new_frame{false};
    std::mutex m_swap_mutex;
    std::condition_variable m_frame_cv;

    // Real-time FPS Telemetry
    std::atomic<double> m_current_fps{0.0};

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