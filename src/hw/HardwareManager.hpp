#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace ai_studio::hw {

struct SystemProfile {
    std::string os_name;
    std::string cpu_architecture;
    unsigned int logical_cores;
    uint64_t total_ram_bytes;
    double total_ram_gb;
};

struct VideoDeviceDescriptor {
    int index{-1};                  // OS camera index (-1 if direct USB/network stream)
    std::string name;               // Human-readable camera or stream name
    std::string endpoint;           // Device path or USB Type-C / stream URL
    bool is_virtual{false};         // True for OBS, Camo, v4l2loopback, etc.
    bool is_stream{false};          // True for Direct USB Type-C / HTTP / RTSP streams
};

class HardwareManager {
public:
    // [[nodiscard]] forces a compiler warning if the result is ignored.
    // noexcept guarantees zero exception-handling overhead.
    // Returning a const reference (&) guarantees zero-copy memory access.
    [[nodiscard]] static const SystemProfile& get_capabilities() noexcept;

    // Dynamically scans all active Internal, External, Virtual, and USB Type-C stream sources
    [[nodiscard]] static std::vector<VideoDeviceDescriptor> scan_video_devices() noexcept;

    // Ultra-fast (<15ms) non-blocking TCP probe so USB Type-C stream discovery never hangs
    [[nodiscard]] static bool probe_local_stream_port(const std::string& host, int port, int timeout_ms = 15) noexcept;
};

} // namespace ai_studio::hw