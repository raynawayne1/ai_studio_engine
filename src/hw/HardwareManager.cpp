#include "HardwareManager.hpp"
#include <thread>

// Platform-specific headers for RAM detection
#if defined(_WIN32)
    #include <windows.h>
#elif defined(__APPLE__)
    #include <sys/types.h>
    #include <sys/sysctl.h>
#elif defined(__linux__)
    #include <sys/sysinfo.h>
#endif

namespace ai_studio::hw {

// Internal helper to perform the actual OS queries only once
static SystemProfile query_os_for_capabilities() noexcept {
    SystemProfile profile;

    // 1. Detect Operating System
    #if defined(_WIN32)
        profile.os_name = "Windows";
    #elif defined(__APPLE__)
        profile.os_name = "macOS";
    #elif defined(__linux__)
        profile.os_name = "Linux";
    #else
        profile.os_name = "Unknown";
    #endif

    // 2. Detect CPU Architecture
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

    // 3. Detect Logical CPU Cores
    profile.logical_cores = std::thread::hardware_concurrency();
    if (profile.logical_cores == 0) {
        profile.logical_cores = 1; // Safe fallback
    }

    // 4. Detect System RAM (Platform Specific)
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

    // Calculate human-readable GB
    profile.total_ram_gb = static_cast<double>(profile.total_ram_bytes) / (1024.0 * 1024.0 * 1024.0);

    return profile;
}

const SystemProfile& HardwareManager::get_capabilities() noexcept {
    // Thread-Safe C++ Magic Static: 
    // Executes the heavy OS query exactly ONCE on the very first call.
    // All subsequent calls return the cached memory instantly.
    static const SystemProfile cached_profile = query_os_for_capabilities();
    return cached_profile;
}

} // namespace ai_studio::hw