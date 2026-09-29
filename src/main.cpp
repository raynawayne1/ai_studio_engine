#include <iostream>
#include <iomanip>
#include <string_view>
#include "hw/HardwareManager.hpp"

// We keep the compiler check here in main.cpp for diagnostics
consteval std::string_view getCompiler() {
#if defined(_MSC_VER)
    return "MSVC";
#elif defined(__clang__)
    return "Clang";
#elif defined(__GNUC__)
    return "GCC";
#else
    return "Unknown Compiler";
#endif
}

int main() {
    std::cout << "=======================================\n";
    std::cout << " AI Studio Engine - Milestone 2\n";
    std::cout << "=======================================\n";
    
    // Output from Milestone 1
    std::cout << "Compiler       : " << getCompiler() << '\n';
    std::cout << "C++ Standard   : " << __cplusplus << '\n';
    std::cout << "---------------------------------------\n";
    
    std::cout << "Initializing hardware detection...\n";

    // Request the capability profile from our new Hardware module
    // This profile.os_name replaces the old getPlatform() function!
    const auto& profile = ai_studio::hw::HardwareManager::get_capabilities();

    std::cout << "---------------------------------------\n";
    std::cout << " Hardware Capability Profile\n";
    std::cout << "---------------------------------------\n";
    std::cout << "OS             : " << profile.os_name << '\n'; 
    std::cout << "Architecture   : " << profile.cpu_architecture << '\n';
    std::cout << "Logical Cores  : " << profile.logical_cores << '\n';
    std::cout << "System RAM     : " << std::fixed << std::setprecision(2) << profile.total_ram_gb << " GB\n";
    std::cout << "=======================================\n";
    std::cout << "Engine initialized successfully.\n";

    return 0;
}