#include <iostream>
#include <string_view>

consteval std::string_view getPlatform() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS (Apple Silicon)";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown OS";
#endif
}

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
    std::cout << " AI Studio Engine - Milestone 1\n";
    std::cout << "=======================================\n";
    std::cout << "Target OS    : " << getPlatform() << '\n';
    std::cout << "Compiler     : " << getCompiler() << '\n';
    std::cout << "C++ Standard : " << __cplusplus << '\n';
    std::cout << "=======================================\n";
    std::cout << "Engine initialized successfully.\n";
    return 0;
}
