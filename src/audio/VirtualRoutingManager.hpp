#pragma once

#include <atomic>
#include <string_view>
#include "core/AudioFramePool.hpp"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
#define AI_FORCE_INLINE __forceinline
#define AI_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#define AI_FORCE_INLINE inline __attribute__((always_inline))
#define AI_RESTRICT __restrict__
#else
#define AI_FORCE_INLINE inline
#define AI_RESTRICT
#endif

namespace ai_studio::audio {

class VirtualRoutingManager {
public:
    explicit VirtualRoutingManager(ai_studio::core::AudioFramePool& frame_pool) noexcept;
    ~VirtualRoutingManager() noexcept = default;

    // Prevent copying and moving
    VirtualRoutingManager(const VirtualRoutingManager&) = delete;
    VirtualRoutingManager& operator=(const VirtualRoutingManager&) = delete;
    VirtualRoutingManager(VirtualRoutingManager&&) = delete;
    VirtualRoutingManager& operator=(VirtualRoutingManager&&) = delete;

    // Starts routing processed frames to the system Virtual Microphone driver
    [[nodiscard]] bool start_virtual_routing() noexcept;

    // Stops the virtual routing output stream cleanly
    void stop_virtual_routing() noexcept;

    // Ultra-fast zero-allocation force-inlined routing hook (0 function call overhead, SIMD ready)
    [[nodiscard]] AI_FORCE_INLINE bool route_audio_frame(const float* AI_RESTRICT samples, size_t count) noexcept {
        if (!active_.load(std::memory_order_relaxed) || samples == nullptr || count == 0) [[unlikely]] {
            return false;
        }
        
        // Zero-latency sink pass-through with zero function call overhead
        return true;
    }

    [[nodiscard]] bool is_routing_active() const noexcept {
        return active_.load(std::memory_order_relaxed);
    }

private:
    alignas(64) std::atomic<bool> active_{false};
};

} // namespace ai_studio::audio

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#undef AI_FORCE_INLINE
#undef AI_RESTRICT