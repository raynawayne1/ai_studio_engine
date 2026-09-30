#include "VirtualCameraManager.hpp"
#include <iostream>

namespace ai_studio::video {

VirtualCameraManager::VirtualCameraManager() noexcept = default;

VirtualCameraManager::~VirtualCameraManager() noexcept {
    stop_capture();
}

bool VirtualCameraManager::start_capture(int camera_index) noexcept {
    if (m_running.load(std::memory_order_acquire)) return true;

    // 1. Open the user's real physical webcam
    m_cap.open(camera_index, cv::CAP_ANY);
    if (!m_cap.isOpened()) {
        std::cerr << "[Video Engine] Error: Failed to open physical camera index " << camera_index << '\n';
        return false;
    }

    // 2. Force High-Definition & Low-Latency settings
    m_cap.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
    m_cap.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
    m_cap.set(cv::CAP_PROP_FPS, 30);
    m_cap.set(cv::CAP_PROP_BUFFERSIZE, 1); // Minimize latency delay

    m_running.store(true, std::memory_order_release);
    m_capture_thread = std::thread(&VirtualCameraManager::capture_loop, this);
    
    std::cout << "[Video Engine] Live camera feed initialized at 720p 30FPS.\n";
    return true;
}

void VirtualCameraManager::stop_capture() noexcept {
    if (m_running.exchange(false, std::memory_order_acq_rel)) {
        if (m_capture_thread.joinable()) {
            m_capture_thread.join();
        }
        if (m_cap.isOpened()) {
            m_cap.release();
        }
        std::cout << "[Video Engine] Camera capture stopped cleanly.\n";
    }
}

void VirtualCameraManager::set_ai_processing_callback(std::function<void(cv::Mat&)> callback) noexcept {
    m_ai_callback = std::move(callback);
}

bool VirtualCameraManager::is_running() const noexcept {
    return m_running.load(std::memory_order_acquire);
}

void VirtualCameraManager::capture_loop() noexcept {
    cv::Mat frame;
    while (m_running.load(std::memory_order_relaxed)) {
        // 1. Grab frame from real webcam
        if (!m_cap.read(frame) || frame.empty()) {
            std::this_thread::yield();
            continue;
        }

        // 2. Execute Zero-Lag AI Pipeline (Face Swap, Lip Sync, Body Tracking, Matting)
        if (m_ai_callback) {
            m_ai_callback(frame);
        }

        // 3. Push the modified, deepfaked frame to the OS Virtual Camera (Zoom/Discord)
        route_to_virtual_camera(frame);
    }
}

void VirtualCameraManager::route_to_virtual_camera(const cv::Mat& frame) noexcept {
    (void)frame; // Suppress unused warning until OS-specific APIs are linked
    
    /* 
     * CROSS-PLATFORM VIRTUAL CAMERA ROUTING TARGETS:
     * - Windows: Push to DirectShow / OBS Virtual Cam memory map
     * - macOS: Push to CoreMediaIO / Syphon server 
     * - Linux: Push to /dev/videoX via v4l2loopback
     */
}

} // namespace ai_studio::video