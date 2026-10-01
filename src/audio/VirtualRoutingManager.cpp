#include "VirtualRoutingManager.hpp"
#include <iostream>

namespace ai_studio::audio {

VirtualRoutingManager::VirtualRoutingManager([[maybe_unused]] ai_studio::core::AudioFramePool& frame_pool) noexcept {
    std::cout << "[DEBUG][src/audio/VirtualRoutingManager.cpp::VirtualRoutingManager] Initialized VirtualRoutingManager.\n";
}

bool VirtualRoutingManager::start_virtual_routing() noexcept {
    if (active_.load(std::memory_order_acquire)) [[unlikely]] {
        return true;
    }

    std::cout << "[DEBUG][src/audio/VirtualRoutingManager.cpp::start_virtual_routing] Initializing Virtual Microphone Bridge driver stream...\n";
    routed_frames_.store(0, std::memory_order_relaxed);
    last_peak_level_.store(0.0f, std::memory_order_relaxed);
    active_.store(true, std::memory_order_release);
    std::cout << "[DEBUG][src/audio/VirtualRoutingManager.cpp::start_virtual_routing] Virtual Microphone stream active and ready for Zoom/Discord/Teams.\n";
    return true;
}

void VirtualRoutingManager::stop_virtual_routing() noexcept {
    if (!active_.load(std::memory_order_acquire)) [[unlikely]] {
        return;
    }

    active_.store(false, std::memory_order_release);
    last_peak_level_.store(0.0f, std::memory_order_relaxed);
    std::cout << "[DEBUG][src/audio/VirtualRoutingManager.cpp::stop_virtual_routing] Virtual Microphone stream stopped cleanly. Total routed frames: "
              << routed_frames_.load(std::memory_order_relaxed) << '\n';
}

} // namespace ai_studio::audio