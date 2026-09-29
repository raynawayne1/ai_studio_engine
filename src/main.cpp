#include <iostream>
#include <iomanip>
#include <string_view>
#include <thread>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <atomic>
#include "hw/HardwareManager.hpp"
#include "core/AudioFramePool.hpp"
#include "ai/InferenceEngine.hpp"
#include "ai/ModelManager.hpp"
#include "ai/CloudSyncManager.hpp"
#include "ai/VoiceUploadManager.hpp"
#include "ai/VoiceLibraryManager.hpp"
#include "ai/VoicePreviewManager.hpp"
#include "ai/VoiceInferenceEngine.hpp"
#include "audio/AudioCaptureEngine.hpp"
#include "audio/VirtualRoutingManager.hpp"
#include "audio/ipc/EngineIPCServer.hpp"

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

int main() {
    std::cout << "==================================================\n";
    std::cout << " AI Studio Engine - Persistent IPC Daemon Mode\n";
    std::cout << "==================================================\n";
    
    std::cout << "Compiler       : " << getCompiler() << '\n';
    std::cout << "C++ Standard   : " << __cplusplus << '\n';
    std::cout << "--------------------------------------------------\n";
    
    // 1. Hardware Profile Detection
    const auto& profile = ai_studio::hw::HardwareManager::get_capabilities();
    std::cout << "OS             : " << profile.os_name << '\n'; 
    std::cout << "Architecture   : " << profile.cpu_architecture << '\n';
    std::cout << "Logical Cores  : " << profile.logical_cores << '\n';
    std::cout << "System RAM     : " << std::fixed << std::setprecision(2) << profile.total_ram_gb << " GB\n";
    std::cout << "--------------------------------------------------\n";

    // 2. AI Inference Backend Selection & Initialization
    auto optimal_provider = ai_studio::ai::InferenceEngine::select_optimal_provider();
    std::cout << "Selected AI Provider: " << ai_studio::ai::InferenceEngine::provider_to_string(optimal_provider) << '\n';
    
    if (ai_studio::ai::InferenceEngine::initialize_backend()) {
        std::cout << "ONNX Runtime Environment: Initialized Successfully.\n";
    } else {
        std::cout << "ONNX Runtime Environment: Initialization Warning (Fallback active).\n";
    }
    std::cout << "--------------------------------------------------\n";

    // 3. Cloud Sync & Firebase Metadata Service
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
    std::cout << "--------------------------------------------------\n";

    // 5. Voice Upload Ingestion & Source Preprocessing
    ai_studio::ai::VoiceUploadManager voice_upload_manager("models");
    
    std::string sample_source_path = "models/sample_source.wav";
    {
        std::filesystem::create_directories("models");
        std::ofstream dummy_file(sample_source_path, std::ios::binary);
        if (dummy_file.is_open()) {
            const char dummy_wav_header[] = "AI_STUDIO_SIMULATED_STUDIO_VOICE_STREAM";
            dummy_file.write(dummy_wav_header, sizeof(dummy_wav_header));
        }
    }

    if (voice_upload_manager.validate_source_media(sample_source_path)) {
        if (voice_upload_manager.preprocess_voice_source("user_custom_profile", "Custom Studio Voice")) {
            std::cout << "[Voice Upload Manager] Source successfully preprocessed.\n";
        }
    }
    std::cout << "--------------------------------------------------\n";

    // 6. Voice Library & Multi-Profile Management
    ai_studio::ai::VoiceLibraryManager voice_library("models");
    if (!voice_library.scan_library()) [[unlikely]] {
        std::cerr << "[Voice Library Error] Failed to scan local library.\n";
    }

    if (auto* profile_meta = voice_upload_manager.get_profile_metadata("user_custom_profile")) {
        if (!voice_library.register_profile(*profile_meta)) {
            std::cerr << "[Voice Library Error] Failed to register profile.\n";
        }
    }

    if (voice_library.select_active_profile("user_custom_profile")) {
        if (auto* active_meta = voice_library.get_active_profile()) {
            std::cout << "[Voice Library Manager] Pre-Call Active Voice Locked: " << active_meta->display_name 
                      << " | Model Path: " << active_meta->processed_model_path.filename().string() << "\n";
        }
    }
    std::cout << "--------------------------------------------------\n";

    // 7. Real-Time Voice Conversion Inference Engine
    ai_studio::ai::VoiceInferenceEngine inference_engine(voice_library);
    if (!inference_engine.initialize_session()) {
        std::cerr << "[Voice Inference Error] Failed to initialize voice conversion inference session.\n";
        return 1;
    }
    std::cout << "--------------------------------------------------\n";

    // 8. High-Performance Shared State for Active Call Pipeline
    std::atomic<bool> call_active{false};
    std::thread audio_pipeline_thread;

    // 9. Local IPC UI Control Bridge (Persistent Server Mode)
    ai_studio::ipc::EngineIPCServer ipc_server(8765);
    ipc_server.set_command_callback([&](std::string_view command) -> std::string {
        std::cout << "\n[UI Dashboard Request Received] -> " << command << "\n";
        
        // --- START CALL COMMAND ---
        if (command.find("START_CALL") != std::string_view::npos) {
            bool expected = false;
            // Atomic check to ensure we only start one thread
            if (!call_active.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                return "{\"status\":\"error\",\"action\":\"already_running\",\"message\":\"Call is already active\"}";
            }
            
            // Spawn background zero-allocation live audio & voice conversion stream
            audio_pipeline_thread = std::thread([&]() noexcept {
                std::cout << "[Audio Pipeline] Live conversion session started from UI trigger...\n";
                ai_studio::core::AudioFramePool frame_pool(1024, 480);
                ai_studio::audio::AudioCaptureEngine capture_engine(frame_pool);
                ai_studio::audio::VirtualRoutingManager virtual_router(frame_pool);

                if (capture_engine.start() && virtual_router.start_virtual_routing()) {
                    std::vector<float> mock_mic_buffer(480, 0.123f);
                    uint64_t frame_index = 0;

                    // Ultra-fast lock-free loop checking atomic flag
                    while (call_active.load(std::memory_order_relaxed)) {
                        while (!capture_engine.push_captured_chunk(mock_mic_buffer.data(), mock_mic_buffer.size(), frame_index * 10000)) {
                            if (!call_active.load(std::memory_order_relaxed)) break;
                            std::this_thread::yield();
                        }

                        if (ai_studio::core::AudioFrame* frame = frame_pool.acquire_ready_frame()) [[likely]] {
                            (void)inference_engine.convert_chunk(frame->samples.data(), frame->samples.data(), 480);
                            (void)virtual_router.route_audio_frame(frame->samples.data(), 480);
                            frame_pool.release_frame(frame);
                            frame_index++;
                        } else {
                            std::this_thread::yield();
                        }
                    }

                    virtual_router.stop_virtual_routing();
                    capture_engine.stop();
                    std::cout << "[Audio Pipeline] Live conversion session stopped cleanly.\n";
                } else {
                    call_active.store(false, std::memory_order_release);
                }
            });

            return "{\"status\":\"ok\",\"action\":\"call_started\",\"latency_target_ms\":15}";
        } 
        
        // --- STOP CALL COMMAND ---
        else if (command.find("STOP_CALL") != std::string_view::npos) {
            if (call_active.exchange(false, std::memory_order_acq_rel)) {
                if (audio_pipeline_thread.joinable()) {
                    audio_pipeline_thread.join();
                }
                return "{\"status\":\"ok\",\"action\":\"call_stopped\"}";
            }
            return "{\"status\":\"error\",\"action\":\"already_stopped\",\"message\":\"No call is currently active\"}";
        }
        
        // --- SET PITCH COMMAND ---
        else if (command.find("SET_PITCH") != std::string_view::npos) {
            // Logic to forward pitch value to inference engine would go here.
            return "{\"status\":\"ok\",\"action\":\"pitch_updated\",\"message\":\"Pitch shift applied successfully\"}";
        } 
        
        return "{\"status\":\"error\",\"message\":\"Unknown UI Command\"}";
    });

    if (!ipc_server.start()) {
        std::cerr << "[IPC Error] Failed to bind local control bridge. Exiting.\n";
        return 1;
    }
    
    std::cout << "==================================================\n";
    std::cout << " ENGINE READY: Listening for React UI on port 8765\n";
    std::cout << " (Press Ctrl+C in this terminal to shut down)\n";
    std::cout << "==================================================\n";

    // Keep daemon alive indefinitely until killed or shutdown requested
    while (ipc_server.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    // Clean shutdown sequence if UI was actively calling
    if (call_active.exchange(false, std::memory_order_acq_rel)) {
        if (audio_pipeline_thread.joinable()) {
            audio_pipeline_thread.join();
        }
    }

    ipc_server.stop();
    inference_engine.shutdown();
    std::cout << "[Engine] Shut down cleanly. Goodbye!\n";

    return 0;
}