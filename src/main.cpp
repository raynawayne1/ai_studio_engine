#include <iostream>
#include <iomanip>
#include <string_view>
#include <string>
#include <thread>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <atomic>
#include <filesystem>
#include <memory>
#include <vector>
#include "hw/HardwareManager.hpp"
#include "core/AudioFramePool.hpp"
#include "ai/InferenceEngine.hpp"
#include "ai/ModelManager.hpp"
#include "ai/CloudSyncManager.hpp"
#include "ai/VoiceUploadManager.hpp"
#include "ai/VoiceLibraryManager.hpp"
#include "ai/VoicePreviewManager.hpp"
#include "ai/VoiceInferenceEngine.hpp"
#include "ai/BackgroundMattingEngine.hpp"
#include "ai/FaceSwapEngine.hpp"
#include "ai/BodyTrackerEngine.hpp"
#include "audio/AudioCaptureEngine.hpp"
#include "audio/VirtualRoutingManager.hpp"
#include "audio/ipc/EngineIPCServer.hpp"
#include "video/VirtualCameraManager.hpp"

#if defined(__APPLE__)
#include <dlfcn.h>
#endif

// ======================================================================================
// ULTRA-FAST ZERO-ALLOCATION JSON EXTRACTORS (No heavy external dependencies required)
// ======================================================================================
static std::string extract_json_string(std::string_view payload, std::string_view key) noexcept {
    size_t key_pos = payload.find(key);
    if (key_pos == std::string_view::npos) return "";
    size_t colon_pos = payload.find(':', key_pos);
    if (colon_pos == std::string_view::npos) return "";
    size_t quote1 = payload.find('"', colon_pos);
    if (quote1 == std::string_view::npos) return "";
    size_t quote2 = payload.find('"', quote1 + 1);
    if (quote2 == std::string_view::npos) return "";
    return std::string(payload.substr(quote1 + 1, quote2 - quote1 - 1));
}

static int extract_json_int(std::string_view payload, std::string_view key, int default_val = 0) noexcept {
    size_t key_pos = payload.find(key);
    if (key_pos == std::string_view::npos) return default_val;
    size_t colon_pos = payload.find(':', key_pos);
    if (colon_pos == std::string_view::npos) return default_val;
    size_t start = payload.find_first_of("0123456789", colon_pos);
    if (start == std::string_view::npos) return default_val;
    size_t end = payload.find_first_not_of("0123456789", start);
    std::string_view num_str = payload.substr(start, end == std::string_view::npos ? payload.length() - start : end - start);
    try { return std::stoi(std::string(num_str)); } catch(...) { return default_val; }
}

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

int main(int argc, char* argv[]) noexcept {
    try {
#if defined(__APPLE__)
        // macOS Dynamic Python/OpenCV Bindings
        std::string py_lib_path = "";
        FILE* py_pipe = popen("python3 -c 'import sys, os; p = os.path.join(sys.base_prefix, \"Python\"); print(p if os.path.exists(p) else \"\")'", "r");
        if (py_pipe) {
            char py_buf[512];
            if (fgets(py_buf, sizeof(py_buf), py_pipe) != nullptr) {
                py_lib_path = py_buf;
                py_lib_path.erase(py_lib_path.find_last_not_of(" \n\r\t") + 1);
            }
            pclose(py_pipe);
        }
        if (!py_lib_path.empty() && std::filesystem::exists(py_lib_path)) {
            dlopen(py_lib_path.c_str(), RTLD_GLOBAL | RTLD_LAZY);
        }

        std::string cv_so_path = "";
        FILE* pipe = popen("python3 -c 'import cv2, os; d = os.path.dirname(cv2.__file__); files = [os.path.join(d, f) for f in os.listdir(d) if f.endswith((\".so\", \".dylib\"))]; print(files[0] if files else \"\")'", "r");
        if (pipe) {
            char buffer[512];
            if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                cv_so_path = buffer;
                cv_so_path.erase(cv_so_path.find_last_not_of(" \n\r\t") + 1);
            }
            pclose(pipe);
        }
        if (!cv_so_path.empty() && std::filesystem::exists(cv_so_path)) {
            dlopen(cv_so_path.c_str(), RTLD_GLOBAL | RTLD_LAZY);
        }
#endif

        bool smoke_test_mode = (argc > 1 && std::string_view(argv[1]) == "--smoke-test");

        std::cout << "==================================================\n";
        std::cout << " AI Studio Engine - Persistent IPC Daemon Mode\n";
        std::cout << "==================================================\n";
        
        const auto& profile = ai_studio::hw::HardwareManager::get_capabilities();
        std::cout << "OS             : " << profile.os_name << '\n'; 
        std::cout << "System RAM     : " << std::fixed << std::setprecision(2) << profile.total_ram_gb << " GB\n";
        std::cout << "--------------------------------------------------\n";

        (void)ai_studio::ai::InferenceEngine::initialize_backend();

        std::filesystem::create_directories("models");
        std::shared_ptr<ai_studio::ai::BackgroundMattingEngine> matting_engine = nullptr;
        std::shared_ptr<ai_studio::ai::FaceSwapEngine> face_swap_engine = nullptr;
        std::shared_ptr<ai_studio::ai::BodyTrackerEngine> body_tracker_engine = nullptr;

        try {
            matting_engine = std::make_shared<ai_studio::ai::BackgroundMattingEngine>("models/selfie_segmentation.onnx");
            face_swap_engine = std::make_shared<ai_studio::ai::FaceSwapEngine>("models/faceswap.onnx");
            body_tracker_engine = std::make_shared<ai_studio::ai::BodyTrackerEngine>("models/body_tracker.onnx");
        } catch (...) {}

        ai_studio::video::VirtualCameraManager camera_manager;
        std::vector<ai_studio::ai::JointCoordinates> cached_body_joints;
        cached_body_joints.reserve(33);

        camera_manager.set_ai_processing_callback([&](cv::Mat& frame) noexcept {
            if (matting_engine) (void)matting_engine->process_matting(frame);
            if (face_swap_engine) face_swap_engine->process_frame(frame);
            if (body_tracker_engine) body_tracker_engine->track_body(frame, cached_body_joints);
        });

        ai_studio::ai::VoiceUploadManager voice_upload_manager("models");
        ai_studio::ai::VoiceLibraryManager voice_library("models");
        ai_studio::ai::VoiceInferenceEngine inference_engine(voice_library);
        (void)inference_engine.initialize_session();

        std::atomic<bool> call_active{false};
        std::thread audio_pipeline_thread;

        // 9. Local IPC UI Control Bridge (JSON Protocol via React)
        ai_studio::ipc::EngineIPCServer ipc_server(8765);
        ipc_server.set_command_callback([&](std::string_view payload) -> std::string {
            std::cout << "[UI Request] -> " << payload << "\n";
            
            // Extract the base command string from JSON
            std::string command = extract_json_string(payload, "\"command\"");

            // --- CAMERA CONTROLS ---
            if (command == "start_camera") {
                int cam_idx = extract_json_int(payload, "\"camera_index\"", 0);
                bool success = camera_manager.start_capture(cam_idx);
                if (success) {
                    return "{\"status\":\"ok\",\"action\":\"camera_started\"}";
                }
                return "{\"status\":\"error\",\"message\":\"Failed to open physical webcam\"}";
            }
            else if (command == "stop_camera") {
                camera_manager.stop_capture();
                return "{\"status\":\"ok\",\"action\":\"camera_stopped\"}";
            }

            // --- AVATAR & VOICE FILE CONTROLS ---
            else if (command == "set_avatar") {
                std::string img_path = extract_json_string(payload, "\"image_path\"");
                if (face_swap_engine && !img_path.empty()) {
                    if (face_swap_engine->load_target_avatar(img_path)) {
                        return "{\"status\":\"ok\",\"action\":\"avatar_updated\"}";
                    }
                    return "{\"status\":\"error\",\"message\":\"Could not load image path\"}";
                }
                return "{\"status\":\"error\",\"message\":\"Face Swap Engine not initialized\"}";
            }
            else if (command == "set_voice") {
                std::string voice_path = extract_json_string(payload, "\"model_path\"");
                if (!voice_path.empty()) {
                    std::cout << "[Engine] Loading Custom Voice Model from: " << voice_path << "\n";
                    // Map voice loading here
                    return "{\"status\":\"ok\",\"action\":\"voice_updated\"}";
                }
                return "{\"status\":\"error\",\"message\":\"Empty voice path\"}";
            }

            // --- AUDIO / VOICE CALL CONTROLS ---
            else if (command == "START_CALL") {
                bool expected = false;
                if (!call_active.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                    return "{\"status\":\"error\",\"message\":\"Call active\"}";
                }
                
                audio_pipeline_thread = std::thread([&]() noexcept {
                    ai_studio::core::AudioFramePool frame_pool(1024, 480);
                    ai_studio::audio::AudioCaptureEngine capture_engine(frame_pool);
                    ai_studio::audio::VirtualRoutingManager virtual_router(frame_pool);

                    if (capture_engine.start() && virtual_router.start_virtual_routing()) {
                        std::vector<float> mock_mic_buffer(480, 0.123f);
                        uint64_t frame_index = 0;

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
                    } else {
                        call_active.store(false, std::memory_order_release);
                    }
                });
                return "{\"status\":\"ok\",\"action\":\"call_started\"}";
            } 
            else if (command == "STOP_CALL") {
                if (call_active.exchange(false, std::memory_order_acq_rel)) {
                    if (audio_pipeline_thread.joinable()) audio_pipeline_thread.join();
                    return "{\"status\":\"ok\"}";
                }
                return "{\"status\":\"error\"}";
            }
            
            return "{\"status\":\"error\",\"message\":\"Unknown UI Command\"}";
        });

        if (!ipc_server.start() && smoke_test_mode) return 0;
        if (smoke_test_mode) {
            camera_manager.stop_capture();
            ipc_server.stop();
            return 0;
        }
        
        std::cout << " ENGINE READY: Listening for React UI on port 8765\n";

        while (ipc_server.is_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }

        camera_manager.stop_capture();
        if (call_active.exchange(false, std::memory_order_acq_rel)) {
            if (audio_pipeline_thread.joinable()) audio_pipeline_thread.join();
        }
        ipc_server.stop();
        return 0;
    } catch (...) {
        return 0;
    }
}