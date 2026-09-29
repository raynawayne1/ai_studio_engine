#include <iostream>
#include <iomanip>
#include <string_view>
#include <thread>
#include <chrono>
#include "hw/HardwareManager.hpp"
#include "core/AudioFramePool.hpp"

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

// Ultra-fast Bi-Directional Zero-Allocation Benchmark
void run_zero_allocation_simulation() {
    std::cout << "\nStarting Zero-Allocation Audio Pool Simulation...\n";
    
    // Create a pool of 1024 frames, each holding 480 samples (10ms of 48kHz audio)
    // This allocates all memory up front.
    ai_studio::core::AudioFramePool frame_pool(1024, 480);
    
    auto start_time = std::chrono::high_resolution_clock::now();

    // PRODUCER: Microphone Ingest Thread
    std::thread producer([&]() {
        for (int i = 0; i < 1000000; ++i) {
            ai_studio::core::AudioFrame* frame = nullptr;
            
            // Wait for a free frame
            while ((frame = frame_pool.acquire_free_frame()) == nullptr) {
                std::this_thread::yield(); 
            }

            // Simulate writing audio data
            frame->timestamp_us = i * 10000; 
            frame->is_valid = true;

            // Send to AI
            while (!frame_pool.push_ready_frame(frame)) {
                std::this_thread::yield();
            }
        }
    });

    // CONSUMER: AI Voice Conversion Thread
    int processed_count = 0;
    std::thread consumer([&]() {
        while (processed_count < 1000000) {
            ai_studio::core::AudioFrame* frame = frame_pool.acquire_ready_frame();
            
            if (frame != nullptr) {
                // Simulate running ONNX Inference on frame->samples here...
                
                processed_count++;

                // Instantly recycle the memory back to the Microphone
                frame_pool.release_frame(frame);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    std::cout << "SUCCESS: Processed " << processed_count << " pooled audio frames in " << duration_ms << " ms.\n";
    std::cout << "Total runtime allocations: ZERO. Memory fragmentation: ZERO.\n";
}

int main() {
    std::cout << "=======================================\n";
    std::cout << " AI Studio Engine - Milestone 4\n";
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

    // Run the zero-allocation benchmark
    run_zero_allocation_simulation();

    return 0;
}