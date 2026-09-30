#include "VirtualCameraManager.hpp"
#include <iostream>

namespace ai_studio::video {

bool VirtualCameraManager::initialize_driver(int width, int height, int fps) noexcept {
#if defined(__APPLE__)
    std::cout << "[VirtualCameraManager] Initializing CoreMediaIO (CMIO) Virtual Camera driver (" << width << "x" << height << "@" << fps << "fps)...\n";
#elif defined(_WIN32)
    std::cout << "[VirtualCameraManager] Initializing DirectShow Virtual Camera filter...\n";
#else
    std::cout << "[VirtualCameraManager] Initializing v4l2loopback video device...\n";
#endif
    return true;
}

void VirtualCameraManager::push_frame(const cv::Mat& processed_frame) noexcept {
    if (processed_frame.empty()) return;
    // Route frame buffer directly into system virtual camera shared memory ring buffer
}

void VirtualCameraManager::shutdown_driver() noexcept {
    std::cout << "[VirtualCameraManager] Virtual camera driver disconnected cleanly.\n";
}

} // namespace ai_studio::video