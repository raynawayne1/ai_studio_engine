#pragma once

#include <string>
#include <cstdint>

namespace ai_studio::hw {

struct SystemProfile {
    std::string os_name;
    std::string cpu_architecture;
    unsigned int logical_cores;
    uint64_t total_ram_bytes;
    double total_ram_gb;
};

class HardwareManager {
public:
    // [[nodiscard]] forces a compiler warning if the result is ignored.
    // noexcept guarantees zero exception-handling overhead.
    // Returning a const reference (&) guarantees zero-copy memory access.
    [[nodiscard]] static const SystemProfile& get_capabilities() noexcept;
};

} // namespace ai_studio::hw