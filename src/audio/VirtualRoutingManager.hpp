#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <iostream>
#include "../core/AudioFramePool.hpp"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324)
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

    VirtualRoutingManager(const VirtualRoutingManager&) = delete;
    VirtualRoutingManager& operator=(const VirtualRoutingManager&) = delete;
    VirtualRoutingManager(VirtualRoutingManager&&) = delete;
    VirtualRoutingManager& operator=(VirtualRoutingManager&&) = delete;

    [[nodiscard]] bool start_virtual_routing() noexcept;
    void stop_virtual_routing() noexcept;

    [[nodiscard]] AI_FORCE_INLINE bool route_audio_frame(const float* AI_RESTRICT samples, size_t count) noexcept {
        if (!active_.load(std::memory_order_relaxed) || samples == nullptr || count == 0) [[unlikely]] {
            return false;
        }

        float peak = 0.0f;
        for (size_t i = 0; i < count; ++i) {
            const float abs_v = (samples[i] >= 0.0f) ? samples[i] : -samples[i];
            if (abs_v > peak) peak = abs_v;
        }
        last_peak_level_.store(peak, std::memory_order_relaxed);
        const uint64_t total = routed_frames_.fetch_add(1, std::memory_order_relaxed) + 1;

        if (total == 1 || total % 300 == 0) {
            std::cout << "[DEBUG][src/audio/VirtualRoutingManager.hpp::route_audio_frame] Routed 48kHz Virtual Mic Frame #"
                      << total << " (" << count << " samples | Peak=" << peak << ")\n";
        }
        return true;
    }

    [[nodiscard]] bool is_routing_active() const noexcept {
        return active_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] float get_peak_level() const noexcept {
        return last_peak_level_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t get_routed_frames() const noexcept {
        return routed_frames_.load(std::memory_order_relaxed);
    }

private:
    alignas(64) std::atomic<bool> active_{false};
    alignas(64) std::atomic<float> last_peak_level_{0.0f};
    alignas(64) std::atomic<uint64_t> routed_frames_{0};
};

} // namespace ai_studio::audio

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#undef AI_FORCE_INLINE
#undef AI_RESTRICT