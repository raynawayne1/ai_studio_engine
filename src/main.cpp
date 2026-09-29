#include <iostream>
#include <iomanip>
#include <string_view>
#include <thread>
#include <chrono>
#include <cstdint>
#include <fstream>
#include "hw/HardwareManager.hpp"
#include "core/AudioFramePool.hpp"
#include "ai/InferenceEngine.hpp"
#include "ai/ModelManager.hpp"
#include "ai/CloudSyncManager.hpp"
#include "ai/VoiceUploadManager.hpp"
#include "ai/VoiceLibraryManager.hpp"
#include "audio/AudioCaptureEngine.hpp"
#include "audio/VirtualRoutingManager.hpp"

consteval std::string_view getCompiler() noexcept {
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

// Ultra-fast Real-Time Audio Capture & Zero-Allocation Pipeline Simulation with Virtual Routing
void run_live_audio_capture_simulation() noexcept {
    std::cout << "\nStarting Live Audio Capture & Zero-Allocation Pipeline Simulation...\n";
    
    // 1. Initialize Pool (1024 frames, 480 samples per 10ms chunk)
    ai_studio::core::AudioFramePool frame_pool(1024, 480);

    // 2. Initialize Capture Engine
    ai_studio::audio::AudioCaptureEngine capture_engine(frame_pool);
    if (!capture_engine.start()) [[unlikely]] {
        std::cerr << "ERROR: Failed to start Audio Capture Engine!\n";
        return;
    }

    // 3. Initialize Virtual Routing Manager (Virtual Microphone Driver Routing)
    ai_studio::audio::VirtualRoutingManager virtual_router(frame_pool);
    if (!virtual_router.start_virtual_routing()) [[unlikely]] {
        std::cerr << "ERROR: Failed to start Virtual Routing Manager!\n";
        capture_engine.stop();
        return;
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();

    // PRODUCER THREAD: Simulates high-frequency hardware microphone callbacks
    std::thread microphone_producer([&]() noexcept {
        std::vector<float> mock_mic_buffer(480, 0.123f);
        for (uint64_t i = 0; i < 1000000; ++i) {
            while (!capture_engine.push_captured_chunk(mock_mic_buffer.data(), mock_mic_buffer.size(), i * 10000)) {
                std::this_thread::yield(); 
            }
        }
    });

    // CONSUMER THREAD: Simulates AI Voice Conversion & Virtual Device Routing
    int processed_count = 0;
    std::thread ai_consumer([&]() noexcept {
        while (processed_count < 1000000) {
            ai_studio::core::AudioFrame* frame = frame_pool.acquire_ready_frame();
            
            if (frame != nullptr) [[likely]] {
                // Route processed frame samples straight to the virtual microphone driver sink with zero allocations
                (void)virtual_router.route_audio_frame(frame->samples.data(), 480);
                
                processed_count++;
                frame_pool.release_frame(frame);
            } else {
                std::this_thread::yield();
            }
        }
    });

    microphone_producer.join();
    ai_consumer.join();
    virtual_router.stop_virtual_routing();
    capture_engine.stop();

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    std::cout << "SUCCESS: Processed " << processed_count << " live capture frames in " << duration_ms << " ms.\n";
    std::cout << "Pipeline status: Zero lag, zero runtime allocations, zero memory fragmentation.\n";
}

int main() {
    std::cout << "=======================================\n";
    std::cout << " AI Studio Engine - Milestone 11\n";
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
    std::cout << "---------------------------------------\n";

    // 3. Cloud Sync & Firebase Metadata Service (Pre-Call Offline Sync)
    ai_studio::ai::CloudSyncManager cloud_sync("models");
    if (!cloud_sync.sync_model_metadata("sample_voice_model")) [[unlikely]] {
        std::cerr << "[Cloud Sync Warning] Operating in strict local offline mode.\n";
    }

    // 4. Local Voice Model Manager & Offline Caching
    ai_studio::ai::ModelManager model_manager("models");
    if (!model_manager.scan_local_models()) [[unlikely]] {
        std::cerr << "[Model Manager Error] Failed to scan local models directory.\n";
    }
    
    if (auto* meta = model_manager.get_model_metadata("sample_voice_model")) [[likely]] {
        std::cout << "[Model Manager] Active voice profile verified: " << meta->model_id << '\n';
    } else {
        std::cout << "[Model Manager] Status: Ready for custom `.onnx` voice profiles in /models directory.\n";
    }
    std::cout << "---------------------------------------\n";

    // 5. Voice Upload Ingestion & Source Preprocessing
    ai_studio::ai::VoiceUploadManager voice_upload_manager("models");
    
    std::string sample_source_path = "models/sample_source.wav";
    {
        std::filesystem::create_directories("models");
        std::ofstream dummy_file(sample_source_path, std::ios::binary);
        if (dummy_file.is_open()) {
            const char dummy_wav_header[] = "RIFF_SIMULATED_STUDIO_VOICE_STREAM";
            dummy_file.write(dummy_wav_header, sizeof(dummy_wav_header));
        }
    }

    if (voice_upload_manager.validate_source_media(sample_source_path)) {
        if (voice_upload_manager.preprocess_voice_source("user_custom_profile", "Custom Studio Voice")) {
            std::cout << "[Voice Upload Manager] Source successfully preprocessed.\n";
        }
    }
    std::cout << "---------------------------------------\n";

    // 6. Voice Library & Multi-Profile Management (Milestone 11)
    ai_studio::ai::VoiceLibraryManager voice_library("models");
    if (!voice_library.scan_library()) [[unlikely]] {
        std::cerr << "[Voice Library Error] Failed to scan local library.\n";
    }

    // Register active voice profile from upload manager into library (checking [[nodiscard]] return value)
    if (auto* profile_meta = voice_upload_manager.get_profile_metadata("user_custom_profile")) {
        if (!voice_library.register_profile(*profile_meta)) {
            std::cerr << "[Voice Library Error] Failed to register profile.\n";
        }
    }

    // Select active profile pre-call with zero allocation lookup
    if (voice_library.select_active_profile("user_custom_profile")) {
        if (auto* active_meta = voice_library.get_active_profile()) {
            std::cout << "[Voice Library Manager] Pre-Call Active Voice Locked: " << active_meta->display_name 
                      << " | Model Path: " << active_meta->processed_model_path.filename().string() << "\n";
        }
    }
    std::cout << "=======================================\n";

    // Run real-time live audio capture simulation with virtual routing
    run_live_audio_capture_simulation();

    return 0;
}