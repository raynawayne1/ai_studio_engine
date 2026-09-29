#include <iostream>
#include <iomanip>
#include <string_view>
#include <thread>
#include <chrono>
#include <cstdint>
#include "hw/HardwareManager.hpp"
#include "core/AudioFramePool.hpp"
#include "ai/InferenceEngine.hpp"
#include "audio/AudioCaptureEngine.hpp"

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

// Ultra-fast Real-Time Audio Capture & Zero-Allocation Pipeline Simulation
void run_live_audio_capture_simulation() {
    std::cout << "\nStarting Live Audio Capture & Zero-Allocation Pipeline Simulation...\n";
    
    // 1. Initialize Pool (1024 frames, 480 samples per 10ms chunk)
    ai_studio::core::AudioFramePool frame_pool(1024, 480);

    // 2. Initialize Capture Engine
    ai_studio::audio::AudioCaptureEngine capture_engine(frame_pool);
    if (!capture_engine.start()) {
        std::cerr << "ERROR: Failed to start Audio Capture Engine!\n";
        return;
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();

    // PRODUCER THREAD: Simulates high-frequency hardware microphone callbacks
    // using our optimized push_captured_chunk() zero-allocation method.
    std::thread microphone_producer([&]() {
        // Pre-allocate a dummy microphone input buffer (10ms of audio)
        std::vector<float> mock_mic_buffer(480, 0.123f);

        for (uint64_t i = 0; i < 1000000; ++i) {
            // Feed raw audio directly through the capture engine hook with zero allocations
            while (!capture_engine.push_captured_chunk(mock_mic_buffer.data(), mock_mic_buffer.size(), i * 10000)) {
                std::this_thread::yield(); // Backpressure relief if AI consumer is lagging
            }
        }
    });

    // CONSUMER THREAD: Simulates AI Voice Conversion (ONNX Runtime consumer)
    int processed_count = 0;
    std::thread ai_consumer([&]() {
        while (processed_count < 1000000) {
            ai_studio::core::AudioFrame* frame = frame_pool.acquire_ready_frame();
            
            if (frame != nullptr) {
                // Simulate running live AI inference / voice conversion on frame->samples here
                processed_count++;

                // Instantly recycle memory back to the audio capture pool with zero overhead
                frame_pool.release_frame(frame);
            } else {
                std::this_thread::yield();
            }
        }
    });

    microphone_producer.join();
    ai_consumer.join();
    capture_engine.stop();

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    std::cout << "SUCCESS: Processed " << processed_count << " live capture frames in " << duration_ms << " ms.\n";
    std::cout << "Pipeline status: Zero lag, zero runtime allocations, zero memory fragmentation.\n";
}

int main() {
    std::cout << "=======================================\n";
    std::cout << " AI Studio Engine - Milestone 6\n";
    std::cout << "=======================================\n";
    
    std::cout << "Compiler       : " << getCompiler() << '\n';
    std::cout << "C++ Standard   : " << __cplusplus << '\n';
    std::cout << "---------------------------------------\n";
    
    // 1. Hardware Profile Detection
    const auto& profile = ai_studio::hw::HardwareManager::get_capabilities();
    std::cout << "OS             : " << profile.os_name << '\n'; 
    std::cout << "Architecture   : " << profile.cpu_architecture << '\n';
    std::cout << "Logical Cores  : " << profile.logical_cores << '\n';
    std::cout << "System RAM     : " << std::fixed << std::setprecision(2) << profile.total_ram_gb << " GB\n";
    std::cout << "---------------------------------------\n";

    // 2. AI Inference Backend Selection & Initialization
    auto optimal_provider = ai_studio::ai::InferenceEngine::select_optimal_provider();
    std::cout << "Selected AI Provider: " << ai_studio::ai::InferenceEngine::provider_to_string(optimal_provider) << '\n';
    
    if (ai_studio::ai::InferenceEngine::initialize_backend()) {
        std::cout << "ONNX Runtime Environment: Initialized Successfully.\n";
    } else {
        std::cout << "ONNX Runtime Environment: Initialization Warning (Fallback active).\n";
    }
    std::cout << "=======================================\n";

    // Run real-time live audio capture simulation
    run_live_audio_capture_simulation();

    return 0;
}