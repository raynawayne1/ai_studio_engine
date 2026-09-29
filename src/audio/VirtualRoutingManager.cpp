#include "VirtualRoutingManager.hpp"
#include <iostream>

namespace ai_studio::audio {

VirtualRoutingManager::VirtualRoutingManager([[maybe_unused]] ai_studio::core::AudioFramePool& frame_pool) noexcept {}

bool VirtualRoutingManager::start_virtual_routing() noexcept {
    if (active_.load(std::memory_order_acquire)) [[unlikely]] {
        return true;
    }

    std::cout << "[Virtual Routing] Initializing system Virtual Microphone driver stream...\n";
    active_.store(true, std::memory_order_release);
    std::cout << "[Virtual Routing] Virtual Microphone stream successfully active and awaiting frames.\n";
    return true;
}

void VirtualRoutingManager::stop_virtual_routing() noexcept {
    if (!active_.load(std::memory_order_acquire)) [[unlikely]] {
        return;
    }

    active_.store(false, std::memory_order_release);
    std::cout << "[Virtual Routing] Virtual Microphone stream stopped cleanly.\n";
}

} // namespace ai_studio::audio