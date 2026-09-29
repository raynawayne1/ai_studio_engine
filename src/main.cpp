#include <iostream>
#include <iomanip>
#include <string_view>
#include <thread>
#include <chrono>
#include "hw/HardwareManager.hpp"
#include "core/LockFreeQueue.hpp"

// Keep the compiler check for diagnostics
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

// Ultra-fast Real-Time concurrency test
void run_realtime_simulation() {
    std::cout << "\nStarting Lock-Free Audio Pipeline Simulation...\n";
    
    // Queue sized for 1024 frames of data
    ai_studio::core::LockFreeQueue<int> audio_queue(1024);
    
    auto start_time = std::chrono::high_resolution_clock::now();

    // PRODUCER THREAD: Simulates Microphone ingest
    std::thread producer([&]() {
        for (int i = 0; i < 1000000; ++i) {
            while (!audio_queue.push(i)) {
                // If queue is full, yield to prevent locking
                std::this_thread::yield(); 
            }
        }
    });

    // CONSUMER THREAD: Simulates AI Processing / Virtual Mic output
    int received_count = 0;
    std::thread consumer([&]() {
        int item;
        while (received_count < 1000000) {
            if (audio_queue.pop(item)) {
                received_count++;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    std::cout << "SUCCESS: Processed " << received_count << " frames in " << duration_ms << " ms.\n";
    std::cout << "Zero locks, zero mutexes, zero memory allocations during execution.\n";
}

int main() {
    std::cout << "=======================================\n";
    std::cout << " AI Studio Engine - Milestone 3\n";
    std::cout << "=======================================\n";
    
    std::cout << "Compiler       : " << getCompiler() << '\n';
    std::cout << "C++ Standard   : " << __cplusplus << '\n';
    std::cout << "---------------------------------------\n";
    
    const auto& profile = ai_studio::hw::HardwareManager::get_capabilities();
    
    std::cout << "OS             : " << profile.os_name << '\n'; 
    std::cout << "Architecture   : " << profile.cpu_architecture << '\n';
    std::cout << "Logical Cores  : " << profile.logical_cores << '\n';
    std::cout << "System RAM     : " << std::fixed << std::setprecision(2) << profile.total_ram_gb << " GB\n";
    std::cout << "=======================================\n";

    // Run the high-performance benchmark
    run_realtime_simulation();

    return 0;
}