#include "HardwareManager.hpp"

// CRITICAL FOR WINDOWS MSVC: Platform socket headers MUST be included BEFORE <opencv2/opencv.hpp>
#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
#elif defined(__APPLE__)
    #include <sys/types.h>
    #include <sys/sysctl.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <poll.h>
    #include <unistd.h>
#elif defined(__linux__)
    #include <sys/sysinfo.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <poll.h>
    #include <unistd.h>
#endif

#include <iostream>
#include <thread>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <opencv2/opencv.hpp>
#include <opencv2/core/utils/logger.hpp>

namespace ai_studio::hw {

static SystemProfile query_os_for_capabilities() noexcept {
    SystemProfile profile;

    #if defined(_WIN32)
        profile.os_name = "Windows";
    #elif defined(__APPLE__)
        profile.os_name = "macOS";
    #elif defined(__linux__)
        profile.os_name = "Linux";
    #else
        profile.os_name = "Unknown";
    #endif

    #if defined(__x86_64__) || defined(_M_X64)
        profile.cpu_architecture = "x86_64";
    #elif defined(__aarch64__) || defined(_M_ARM64)
        profile.cpu_architecture = "ARM64";
    #elif defined(__arm__) || defined(_M_ARM)
        profile.cpu_architecture = "ARM";
    #elif defined(__i386__) || defined(_M_IX86)
        profile.cpu_architecture = "x86";
    #else
        profile.cpu_architecture = "Unknown Architecture";
    #endif

    profile.logical_cores = std::thread::hardware_concurrency();
    if (profile.logical_cores == 0) {
        profile.logical_cores = 1;
    }

    profile.total_ram_bytes = 0;

    #if defined(_WIN32)
        MEMORYSTATUSEX status;
        status.dwLength = sizeof(status);
        if (GlobalMemoryStatusEx(&status)) {
            profile.total_ram_bytes = status.ullTotalPhys;
        }
    #elif defined(__APPLE__)
        int mib[2] = { CTL_HW, HW_MEMSIZE };
        int64_t physical_memory = 0;
        size_t length = sizeof(physical_memory);
        if (sysctl(mib, 2, &physical_memory, &length, nullptr, 0) == 0) {
            profile.total_ram_bytes = static_cast<uint64_t>(physical_memory);
        }
    #elif defined(__linux__)
        struct sysinfo info;
        if (sysinfo(&info) == 0) {
            profile.total_ram_bytes = static_cast<uint64_t>(info.totalram) * info.mem_unit;
        }
    #endif

    profile.total_ram_gb = static_cast<double>(profile.total_ram_bytes) / (1024.0 * 1024.0 * 1024.0);

    std::cout << "[DEBUG][src/hw/HardwareManager.cpp::query_os_for_capabilities] OS=" << profile.os_name
              << " | Arch=" << profile.cpu_architecture
              << " | Cores=" << profile.logical_cores
              << " | RAM=" << profile.total_ram_gb << " GB\n";

    return profile;
}

const SystemProfile& HardwareManager::get_capabilities() noexcept {
    static const SystemProfile cached_profile = query_os_for_capabilities();
    return cached_profile;
}

bool HardwareManager::probe_local_stream_port(const std::string& host, int port, int timeout_ms) noexcept {
#if defined(_WIN32)
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) return false;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(sock, &write_fds);

    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = timeout_ms * 1000;

    int res = select(0, nullptr, &write_fds, nullptr, &tv);
    bool connected = false;
    if (res > 0 && FD_ISSET(sock, &write_fds)) {
        int so_error = 0;
        int len = sizeof(so_error);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &len);
        connected = (so_error == 0);
    }

    closesocket(sock);
    WSACleanup();
    return connected;
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    int res = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    bool connected = false;

    if (res == 0) {
        connected = true;
    } else {
        pollfd pfd{};
        pfd.fd = sock;
        pfd.events = POLLOUT;
        if (poll(&pfd, 1, timeout_ms) > 0) {
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len) == 0 && so_error == 0) {
                connected = true;
            }
        }
    }

    close(sock);
    return connected;
#endif
}

std::vector<VideoDeviceDescriptor> HardwareManager::scan_video_devices() noexcept {
    // Silence noisy OpenCV AVFoundation/DSHOW warnings during hardware index probe
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_SILENT);

    std::cout << "[DEBUG][src/hw/HardwareManager.cpp::scan_video_devices] Probing Internal, External USB, Virtual & USB-C Phone sources...\n";
    std::vector<VideoDeviceDescriptor> devices;

#if defined(_WIN32)
    const int preferred_backend = cv::CAP_DSHOW;
#elif defined(__APPLE__)
    const int preferred_backend = cv::CAP_AVFOUNDATION;
#elif defined(__linux__)
    const int preferred_backend = cv::CAP_V4L2;
#else
    const int preferred_backend = cv::CAP_ANY;
#endif

    // 1. Scan hardware & virtual camera indices (stop immediately on first empty index on macOS/Windows)
    for (int idx = 0; idx < 4; ++idx) {
        std::string dev_name = "Camera #" + std::to_string(idx);
        bool is_virtual = false;

#if defined(__linux__)
        std::ifstream name_file("/sys/class/video4linux/video" + std::to_string(idx) + "/name");
        if (!name_file.is_open()) {
            continue;
        }
        std::getline(name_file, dev_name);
#endif

        cv::VideoCapture test_cap(idx, preferred_backend);
        if (test_cap.isOpened()) {
            std::string lower_name = dev_name;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (lower_name.find("virtual") != std::string::npos ||
                lower_name.find("obs") != std::string::npos ||
                lower_name.find("loopback") != std::string::npos) {
                is_virtual = true;
            }

            std::cout << "[DEBUG][src/hw/HardwareManager.cpp::scan_video_devices] Found OS Camera Index " << idx
                      << " (" << dev_name << ") | Virtual=" << (is_virtual ? "YES" : "NO") << '\n';

            devices.push_back(VideoDeviceDescriptor{
                idx,
                dev_name,
                std::to_string(idx),
                is_virtual,
                false
            });
            test_cap.release();
        } else {
            // If index 0 is not attached on macOS/Windows, stop probing higher indices immediately
            break;
        }
    }

    // 2. Probe Direct USB Type-C / Built-in Phone Bridge / ADB Tunnels (<12ms non-blocking check)
    struct StreamCandidate {
        const char* host;
        int port;
        const char* url;
        const char* label;
    };

    static const StreamCandidate usb_candidates[] = {
        {"127.0.0.1",      8766, "usb_phone_push",                   "Built-In USB-C Phone Camera Bridge (:8766)"},
        {"127.0.0.1",      8080, "http://127.0.0.1:8080/video",      "USB Type-C Phone Stream (:8080)"},
        {"127.0.0.1",      4747, "http://127.0.0.1:4747/video",      "USB Type-C Phone Stream (:4747)"},
        {"192.168.42.129", 8080, "http://192.168.42.129:8080/video", "Android USB-C Tethered Camera (192.168.42.129)"},
        {"127.0.0.1",      8554, "rtsp://127.0.0.1:8554/live",       "USB Type-C RTSP Stream (:8554)"}
    };

    for (const auto& candidate : usb_candidates) {
        if (probe_local_stream_port(candidate.host, candidate.port, 12)) {
            std::cout << "[DEBUG][src/hw/HardwareManager.cpp::scan_video_devices] Active USB-C / Stream Port Detected: "
                      << candidate.label << " -> " << candidate.url << '\n';
            devices.push_back(VideoDeviceDescriptor{
                -1,
                candidate.label,
                candidate.url,
                false,
                true
            });
        }
    }

    if (const char* env_url = std::getenv("AI_STUDIO_CAMERA_URL")) {
        std::string custom_url(env_url);
        if (!custom_url.empty()) {
            devices.push_back(VideoDeviceDescriptor{
                -1,
                "Custom USB/Network Stream",
                custom_url,
                false,
                true
            });
        }
    }

    std::cout << "[DEBUG][src/hw/HardwareManager.cpp::scan_video_devices] Scan complete. Total video sources found: "
              << devices.size() << '\n';
    return devices;
}

} // namespace ai_studio::hw