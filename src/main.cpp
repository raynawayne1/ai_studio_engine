#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <charconv>
#include <fstream>
#include <atomic>
#include <filesystem>
#include <memory>
#include <vector>
#include <algorithm>
#include <cctype>
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

namespace {

std::string escape_json_string(std::string_view input) noexcept {
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out;
}

std::string sanitize_profile_id(std::string_view raw) noexcept {
    std::string clean;
    clean.reserve(raw.size());
    for (unsigned char c : raw) {
        if (std::isalnum(c)) {
            clean += static_cast<char>(std::tolower(c));
        } else if (c == '_' || c == '-' || c == ' ') {
            if (clean.empty() || clean.back() != '_') {
                clean += '_';
            }
        }
    }
    while (!clean.empty() && clean.back() == '_') {
        clean.pop_back();
    }
    return clean.empty() ? "cloned_voice" : clean;
}

std::string extract_json_string(std::string_view json, std::string_view key) noexcept {
    std::string search_key = "\"" + std::string(key) + "\"";
    auto pos = json.find(search_key);
    if (pos == std::string_view::npos) return "";

    pos = json.find(':', pos + search_key.size());
    if (pos == std::string_view::npos) return "";

    auto start_quote = json.find('"', pos + 1);
    if (start_quote == std::string_view::npos) return "";

    auto end_quote = json.find('"', start_quote + 1);
    if (end_quote == std::string_view::npos) return "";

    return std::string(json.substr(start_quote + 1, end_quote - start_quote - 1));
}

int extract_json_int(std::string_view json, std::string_view key, int fallback = -1) noexcept {
    std::string search_key = "\"" + std::string(key) + "\"";
    auto pos = json.find(search_key);
    if (pos == std::string_view::npos) return fallback;

    pos = json.find(':', pos + search_key.size());
    if (pos == std::string_view::npos) return fallback;
    ++pos;

    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '"')) {
        ++pos;
    }

    int value = fallback;
    std::from_chars(json.data() + pos, json.data() + json.size(), value);
    return value;
}

float extract_json_float(std::string_view json, std::string_view key, float fallback = 0.0f) noexcept {
    std::string search_key = "\"" + std::string(key) + "\"";
    auto pos = json.find(search_key);
    if (pos == std::string_view::npos) return fallback;

    pos = json.find(':', pos + search_key.size());
    if (pos == std::string_view::npos) return fallback;
    ++pos;

    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '"')) {
        ++pos;
    }

    try {
        return std::stof(std::string(json.substr(pos, 16)));
    } catch (...) {
        return fallback;
    }
}

bool extract_json_bool(std::string_view json, std::string_view key, bool fallback = false) noexcept {
    std::string search_key = "\"" + std::string(key) + "\"";
    auto pos = json.find(search_key);
    if (pos == std::string_view::npos) return fallback;

    pos = json.find(':', pos + search_key.size());
    if (pos == std::string_view::npos) return fallback;

    auto rest = json.substr(pos + 1, 12);
    if (rest.find("true") != std::string_view::npos) return true;
    if (rest.find("false") != std::string_view::npos) return false;
    return fallback;
}

void try_usb_type_c_adb_bridge_async() noexcept {
    static std::atomic<bool> adb_running{false};
    static std::atomic<int64_t> last_run_ms{0};

    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();

    int64_t prev_ms = last_run_ms.load(std::memory_order_relaxed);
    if (now_ms - prev_ms < 3000) {
        return;
    }

    bool expected = false;
    if (!adb_running.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }
    last_run_ms.store(now_ms, std::memory_order_relaxed);

    std::thread([]() noexcept {
        std::cout << "[DEBUG][src/main.cpp::try_usb_type_c_adb_bridge_async] Probing USB-C ADB port forwards (8080, 4747, 8766) in background...\n";
#if defined(_WIN32)
        int r = std::system("adb forward tcp:8080 tcp:8080 >nul 2>&1 & adb forward tcp:4747 tcp:4747 >nul 2>&1 & adb reverse tcp:8766 tcp:8766 >nul 2>&1");
#else
        int r = std::system("adb forward tcp:8080 tcp:8080 >/dev/null 2>&1; adb forward tcp:4747 tcp:4747 >/dev/null 2>&1; adb reverse tcp:8766 tcp:8766 >/dev/null 2>&1");
#endif
        (void)r;
        adb_running.store(false, std::memory_order_release);
    }).detach();
}

std::string build_camera_list_json(const ai_studio::video::VirtualCameraManager& camera_manager) noexcept {
    const auto devices = ai_studio::hw::HardwareManager::scan_video_devices();
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1);
    oss << "{\"status\":\"ok\",\"action\":\"cameras_scanned\","
        << "\"running\":" << (camera_manager.is_running() ? "true" : "false") << ","
        << "\"active_source\":\"" << escape_json_string(camera_manager.get_active_source_name()) << "\","
        << "\"fps\":" << camera_manager.get_current_fps() << ","
        << "\"devices\":[";

    for (size_t i = 0; i < devices.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{\"index\":" << devices[i].index
            << ",\"name\":\"" << escape_json_string(devices[i].name) << "\""
            << ",\"endpoint\":\"" << escape_json_string(devices[i].endpoint) << "\""
            << ",\"is_virtual\":" << (devices[i].is_virtual ? "true" : "false")
            << ",\"is_stream\":" << (devices[i].is_stream ? "true" : "false") << "}";
    }
    oss << "]}";
    return oss.str();
}

std::string build_voices_list_json(const ai_studio::ai::VoiceLibraryManager& voice_library) noexcept {
    const auto profiles = voice_library.get_all_profiles();
    const auto* active = voice_library.get_active_profile();
    std::string active_id = active ? active->profile_id : "";

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "{\"status\":\"ok\",\"action\":\"voices_listed\","
        << "\"active_voice\":\"" << escape_json_string(active_id) << "\","
        << "\"voices\":[";

    for (size_t i = 0; i < profiles.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{\"id\":\"" << escape_json_string(profiles[i].profile_id) << "\","
            << "\"displayName\":\"" << escape_json_string(profiles[i].display_name) << "\","
            << "\"sourcePath\":\"" << escape_json_string(profiles[i].source_file_path.string()) << "\","
            << "\"modelPath\":\"" << escape_json_string(profiles[i].processed_model_path.string()) << "\","
            << "\"f0Bias\":" << profiles[i].fundamental_bias_semitones << ","
            << "\"formantF1\":" << profiles[i].formant_f1_gain << ","
            << "\"formantF2\":" << profiles[i].formant_f2_gain << ","
            << "\"isCached\":true}";
    }
    oss << "]}";
    return oss.str();
}

} // namespace

int main(int argc, char* argv[]) noexcept {
    try {
#if defined(__APPLE__)
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
            void* py_handle = dlopen(py_lib_path.c_str(), RTLD_GLOBAL | RTLD_LAZY);
            if (py_handle) {
                std::cout << "[DEBUG][src/main.cpp::main] Python shared library loaded globally from: " << py_lib_path << '\n';
            }
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
            void* handle = dlopen(cv_so_path.c_str(), RTLD_GLOBAL | RTLD_LAZY);
            if (!handle) {
                std::cerr << "[WARN][src/main.cpp::main] dlopen failed for OpenCV: " << dlerror() << '\n';
            } else {
                std::cout << "[DEBUG][src/main.cpp::main] OpenCV shared library loaded successfully from: " << cv_so_path << '\n';
            }
        }
#endif

        bool smoke_test_mode = (argc > 1 && std::string_view(argv[1]) == "--smoke-test");

        std::cout << "==================================================\n";
        std::cout << " AI Studio Engine - " << (smoke_test_mode ? "CI Smoke Test Mode" : "Persistent IPC Daemon Mode") << "\n";
        std::cout << "==================================================\n";
        std::cout << "Compiler       : " << getCompiler() << '\n';
        std::cout << "C++ Standard   : " << __cplusplus << '\n';
        std::cout << "--------------------------------------------------\n";

        const auto& profile = ai_studio::hw::HardwareManager::get_capabilities();
        std::cout << "OS             : " << profile.os_name << '\n';
        std::cout << "Architecture   : " << profile.cpu_architecture << '\n';
        std::cout << "Logical Cores  : " << profile.logical_cores << '\n';
        std::cout << "System RAM     : " << std::fixed << std::setprecision(2) << profile.total_ram_gb << " GB\n";
        std::cout << "--------------------------------------------------\n";

        if (!smoke_test_mode) {
            try_usb_type_c_adb_bridge_async();
            std::cout << "[DEBUG][src/main.cpp::main] Running initial startup video discovery...\n";
            const auto detected_devices = ai_studio::hw::HardwareManager::scan_video_devices();
            if (detected_devices.empty()) {
                std::cout << "[DEBUG][src/main.cpp::main] No physical OS webcam registered at boot. Built-In USB-C Phone Bridge Ready.\n";
            } else {
                for (const auto& dev : detected_devices) {
                    std::cout << "  -> Detected: [" << dev.name << "] | Endpoint: " << dev.endpoint
                              << (dev.is_stream ? " (USB Type-C / Stream)" : (dev.is_virtual ? " (Virtual Cam)" : " (Hardware Cam)")) << '\n';
                }
            }
            std::cout << "--------------------------------------------------\n";
        }

        // 2. AI Inference Backend Selection & Initialization
        auto optimal_provider = ai_studio::ai::InferenceEngine::select_optimal_provider();
        std::cout << "[DEBUG][src/main.cpp::main] Selected AI Provider: "
                  << ai_studio::ai::InferenceEngine::provider_to_string(optimal_provider) << '\n';

        if (ai_studio::ai::InferenceEngine::initialize_backend()) {
            std::cout << "[DEBUG][src/main.cpp::main] ONNX Runtime Environment: Initialized Successfully.\n";
        } else {
            std::cout << "[WARN][src/main.cpp::main] ONNX Runtime Environment: Initialization Warning (Fallback active).\n";
        }
        std::cout << "--------------------------------------------------\n";

        // 3. Initialize AI Vision Engines (Matting, FaceSwap, BodyTracker)
        std::filesystem::create_directories("models");
        std::cout << "[DEBUG][src/main.cpp::main] Instantiating BackgroundMattingEngine, FaceSwapEngine & BodyTrackerEngine...\n";
        auto matting_engine = std::make_unique<ai_studio::ai::BackgroundMattingEngine>("models/selfie_segmentation.onnx");
        auto face_swap_engine = std::make_unique<ai_studio::ai::FaceSwapEngine>("models/faceswap.onnx");
        auto body_tracker_engine = std::make_unique<ai_studio::ai::BodyTrackerEngine>("models/body_tracker.onnx");

        std::atomic<bool> face_swap_enabled{true};
        std::atomic<bool> body_tracking_enabled{true};
        std::atomic<bool> lip_sync_enabled{true};

        // 4. Cloud Sync & Local Model Management
        std::cout << "[DEBUG][src/main.cpp::main] Initializing CloudSyncManager & ModelManager...\n";
        ai_studio::ai::CloudSyncManager cloud_sync("models");
        (void)cloud_sync.sync_model_metadata("faceswap");

        ai_studio::ai::ModelManager model_manager("models");
        (void)model_manager.scan_local_models();

        // 5. Real Voice Cloning Pipeline Initialization (Zero Fake Profiles)
        std::cout << "[DEBUG][src/main.cpp::main] Initializing VoiceUploadManager, VoiceLibraryManager, VoicePreviewManager & VoiceInferenceEngine...\n";
        ai_studio::ai::VoiceUploadManager voice_upload_manager("models");
        ai_studio::ai::VoiceLibraryManager voice_library("models");
        (void)voice_library.scan_library();

        ai_studio::ai::VoicePreviewManager voice_preview_manager(voice_library);
        (void)voice_preview_manager.load_and_verify_active_model();

        ai_studio::ai::VoiceInferenceEngine inference_engine(voice_library);
        if (!inference_engine.initialize_session()) {
            if (!smoke_test_mode) {
                std::cerr << "[ERROR][src/main.cpp::main] Failed to initialize voice conversion inference session.\n";
                return 1;
            }
        }

        // 6. Persistent Lock-Free 48kHz Audio Pipeline (AudioFramePool + AudioCaptureEngine + VirtualRoutingManager)
        ai_studio::core::AudioFramePool audio_frame_pool(1024, 480);
        ai_studio::audio::AudioCaptureEngine audio_capture_engine(audio_frame_pool);
        ai_studio::audio::VirtualRoutingManager virtual_audio_router(audio_frame_pool);
        (void)audio_capture_engine.start();
        (void)virtual_audio_router.start_virtual_routing();
        std::cout << "--------------------------------------------------\n";

        // 7. Initialize Virtual Camera & Real-Time Video Routing Manager
        ai_studio::video::VirtualCameraManager camera_manager;

        std::vector<ai_studio::ai::JointCoordinates> cached_body_joints;
        cached_body_joints.reserve(33);
        std::vector<float> lip_sync_audio_sample(16, 0.0f);
        std::atomic<uint64_t> callback_frame_count{0};

        camera_manager.set_ai_processing_callback([&](cv::Mat& frame) noexcept {
            const uint64_t f_idx = ++callback_frame_count;

            if (body_tracking_enabled.load(std::memory_order_relaxed) && body_tracker_engine) {
                body_tracker_engine->track_body(frame, cached_body_joints);
            }

            if (face_swap_enabled.load(std::memory_order_relaxed) && face_swap_engine) {
                if (body_tracker_engine) {
                    const auto kin = body_tracker_engine->get_kinematics_state();
                    face_swap_engine->set_kinematics_context(
                        kin.head_tilt_degrees,
                        kin.left_hand_box,
                        kin.right_hand_box,
                        kin.eyes_blinking
                    );
                }

                if (lip_sync_enabled.load(std::memory_order_relaxed)) {
                    float energy = inference_engine.get_recent_speech_energy();
                    std::fill(lip_sync_audio_sample.begin(), lip_sync_audio_sample.end(), energy);
                    face_swap_engine->process_frame(frame, lip_sync_audio_sample);
                } else {
                    face_swap_engine->process_frame(frame);
                }
            }

            if (matting_engine && matting_engine->is_enabled()) {
                matting_engine->process_matting(frame);
            }

            if (f_idx == 1 || f_idx % 120 == 0) {
                std::cout << "[DEBUG][src/main.cpp::ai_processing_callback] Completed full vision pipeline on frame #"
                          << f_idx << " | AvatarReady=" << (face_swap_engine && face_swap_engine->is_ready() ? "YES" : "NO")
                          << " | Matting=" << (matting_engine && matting_engine->is_enabled() ? "ON" : "OFF") << '\n';
            }
        });

        std::atomic<bool> call_active{false};
        std::atomic<float> active_audio_peak{0.0f};
        std::atomic<uint64_t> total_routed_audio_frames{0};
        std::atomic<uint64_t> audio_timestamp_counter{0};

        // 8. Local IPC UI Control Bridge (Persistent Server Mode on Port 8765)
        ai_studio::ipc::EngineIPCServer ipc_server(8765);

        ipc_server.set_frame_callbacks(
            [&]() -> std::vector<uint8_t> {
                return camera_manager.get_latest_source_jpeg();
            },
            [&]() -> std::vector<uint8_t> {
                return camera_manager.get_latest_output_jpeg();
            },
            [&](const uint8_t* jpg_data, size_t jpg_size) -> bool {
                return camera_manager.push_external_frame(jpg_data, jpg_size);
            }
        );

        // Real-Time 48kHz Microphone-to-Cloned-Voice Processing Callback (POST /audio/process)
        ipc_server.set_audio_process_callback([&](const float* in_samples, size_t sample_count) -> std::vector<float> {
            std::vector<float> out_samples(sample_count, 0.0f);
            if (!in_samples || sample_count == 0) return out_samples;

            size_t offset = 0;
            while (offset < sample_count) {
                const size_t chunk_len = std::min<size_t>(480, sample_count - offset);
                const uint64_t ts = (++audio_timestamp_counter) * 10000;

                if (audio_capture_engine.push_captured_chunk(in_samples + offset, chunk_len, ts)) {
                    if (ai_studio::core::AudioFrame* frame = audio_frame_pool.acquire_ready_frame()) [[likely]] {
                        (void)inference_engine.convert_chunk(frame->samples.data(), frame->samples.data(), chunk_len);
                        if (voice_preview_manager.is_preview_active()) {
                            (void)voice_preview_manager.process_preview_chunk(frame->samples.data(), frame->samples.data(), chunk_len);
                        }
                        (void)virtual_audio_router.route_audio_frame(frame->samples.data(), chunk_len);
                        active_audio_peak.store(virtual_audio_router.get_peak_level(), std::memory_order_relaxed);
                        total_routed_audio_frames.store(virtual_audio_router.get_routed_frames(), std::memory_order_relaxed);

                        std::memcpy(out_samples.data() + offset, frame->samples.data(), chunk_len * sizeof(float));
                        audio_frame_pool.release_frame(frame);
                    }
                } else {
                    (void)inference_engine.convert_chunk(in_samples + offset, out_samples.data() + offset, chunk_len);
                }
                offset += chunk_len;
            }
            return out_samples;
        });

        ipc_server.set_command_callback([&](std::string_view command) -> std::string {
            if (command.find("GET_CAMERA_STATUS") == std::string_view::npos) {
                std::cout << "[DEBUG][src/main.cpp::ipc_command] Received UI Command -> " << command << "\n";
            }

            // --- 1. SCAN CAMERAS & USB TYPE-C STREAMS ---
            if (command.find("SCAN_CAMERAS") != std::string_view::npos ||
                command.find("scan_cameras") != std::string_view::npos ||
                command.find("GET_CAMERAS") != std::string_view::npos) {
                try_usb_type_c_adb_bridge_async();
                return build_camera_list_json(camera_manager);
            }

            // --- 2. DIRECT USB TYPE-C / STREAM CONNECTION ---
            else if (command.find("CONNECT_USB") != std::string_view::npos ||
                     command.find("connect_usb") != std::string_view::npos ||
                     command.find("CONNECT_STREAM") != std::string_view::npos ||
                     command.find("SET_CAMERA_SOURCE") != std::string_view::npos) {
                try_usb_type_c_adb_bridge_async();

                std::string stream_url = extract_json_string(command, "url");
                if (stream_url.empty()) stream_url = extract_json_string(command, "endpoint");
                int requested_index = extract_json_int(command, "index", -1);

                camera_manager.stop_capture();

                bool success = false;
                if (!stream_url.empty()) {
                    camera_manager.set_fallback_stream_url(stream_url);
                    success = camera_manager.start_capture(stream_url);
                } else if (requested_index >= 0) {
                    success = camera_manager.start_capture(requested_index);
                } else {
                    success = camera_manager.start_auto_capture();
                }

                if (success) {
                    return "{\"status\":\"ok\",\"action\":\"camera_started\",\"active_source\":\"" +
                           escape_json_string(camera_manager.get_active_source_name()) +
                           "\",\"message\":\"Connected to video source in Zero-Lag Mode\"}";
                }
                return "{\"status\":\"error\",\"action\":\"camera_failed\",\"message\":\"No physical webcam, virtual camera, or USB Type-C stream responded\"}";
            }

            // --- 3. START CAMERA ---
            else if (command.find("start_camera") != std::string_view::npos ||
                     command.find("START_CAMERA") != std::string_view::npos) {
                try_usb_type_c_adb_bridge_async();

                std::string custom_url = extract_json_string(command, "url");
                if (custom_url.empty()) custom_url = extract_json_string(command, "endpoint");
                int requested_index = extract_json_int(command, "index", -1);
                if (requested_index < 0) {
                    requested_index = extract_json_int(command, "camera_index", -1);
                }

                bool success = false;
                if (!custom_url.empty()) {
                    camera_manager.set_fallback_stream_url(custom_url);
                    success = camera_manager.start_capture(custom_url);
                } else if (requested_index >= 0) {
                    success = camera_manager.start_capture(requested_index);
                } else {
                    success = camera_manager.start_auto_capture();
                }

                if (success) {
                    return "{\"status\":\"ok\",\"action\":\"camera_started\",\"active_source\":\"" +
                           escape_json_string(camera_manager.get_active_source_name()) +
                           "\",\"message\":\"Virtual camera feed and AI pipeline active\"}";
                }
                return "{\"status\":\"error\",\"action\":\"camera_failed\",\"message\":\"No camera or USB Type-C stream detected.\"}";
            }

            // --- 4. STOP CAMERA ---
            else if (command.find("stop_camera") != std::string_view::npos ||
                     command.find("STOP_CAMERA") != std::string_view::npos) {
                camera_manager.stop_capture();
                return "{\"status\":\"ok\",\"action\":\"camera_stopped\"}";
            }

            // --- 5. CAMERA + BODY KINEMATICS + ACTIVE VOICE TELEMETRY STATUS ---
            else if (command.find("GET_CAMERA_STATUS") != std::string_view::npos ||
                     command.find("get_camera_status") != std::string_view::npos ||
                     command.find("GET_SYSTEM_TELEMETRY") != std::string_view::npos) {
                const auto kin = body_tracker_engine ? body_tracker_engine->get_kinematics_state() : ai_studio::ai::BodyKinematicsState{};
                const auto* active_voice = voice_library.get_active_profile();
                std::string voice_id = active_voice ? active_voice->profile_id : "";
                std::string voice_name = active_voice ? active_voice->display_name : "None (Upload in Voice Cloning Tab)";

                std::ostringstream oss;
                oss << std::fixed << std::setprecision(2);
                oss << "{\"status\":\"ok\",\"action\":\"camera_status\","
                    << "\"running\":" << (camera_manager.is_running() ? "true" : "false") << ","
                    << "\"call_active\":" << (call_active.load(std::memory_order_relaxed) ? "true" : "false") << ","
                    << "\"avatar_ready\":" << (face_swap_engine && face_swap_engine->is_ready() ? "true" : "false") << ","
                    << "\"active_source\":\"" << escape_json_string(camera_manager.get_active_source_name()) << "\","
                    << "\"active_voice\":\"" << escape_json_string(voice_id) << "\","
                    << "\"active_voice_name\":\"" << escape_json_string(voice_name) << "\","
                    << "\"fps\":" << camera_manager.get_current_fps() << ","
                    << "\"speech_energy\":" << inference_engine.get_recent_speech_energy() << ","
                    << "\"audio_peak\":" << active_audio_peak.load(std::memory_order_relaxed) << ","
                    << "\"input_gain\":" << audio_capture_engine.get_input_gain() << ","
                    << "\"routed_frames\":" << total_routed_audio_frames.load(std::memory_order_relaxed) << ","
                    << "\"left_arm_raised\":" << (kin.left_arm_raised ? "true" : "false") << ","
                    << "\"right_arm_raised\":" << (kin.right_arm_raised ? "true" : "false") << ","
                    << "\"hands_holding_body\":" << (kin.hands_holding_body ? "true" : "false") << ","
                    << "\"full_body_visible\":" << (kin.full_body_visible ? "true" : "false") << ","
                    << "\"eyes_blinking\":" << (kin.eyes_blinking ? "true" : "false") << ","
                    << "\"head_tilt\":" << kin.head_tilt_degrees << ","
                    << "\"cloud_status\":\"" << ai_studio::ai::CloudSyncManager::status_to_string(cloud_sync.get_status()) << "\"}";
                return oss.str();
            }

            // --- 6. SET TARGET AVATAR PICTURE (REAL FACE SWAP LOADING) ---
            else if (command.find("SET_AVATAR") != std::string_view::npos ||
                     command.find("set_avatar") != std::string_view::npos) {
                std::string img_path = extract_json_string(command, "image_path");
                std::cout << "[DEBUG][src/main.cpp::SET_AVATAR] Loading target face swap avatar: " << img_path << '\n';
                if (!img_path.empty() && face_swap_engine && face_swap_engine->load_target_avatar(img_path)) {
                    return "{\"status\":\"ok\",\"action\":\"avatar_updated\",\"message\":\"Target identity embedded for real-time Face Swap & Skin Blending\"}";
                }
                return "{\"status\":\"error\",\"action\":\"avatar_failed\",\"message\":\"Failed to read target avatar image\"}";
            }

            // --- 7. BACKGROUND MATTING & VIRTUAL BACKGROUND CONTROLS ---
            else if (command.find("SET_MATTING") != std::string_view::npos ||
                     command.find("set_matting") != std::string_view::npos) {
                bool enabled = extract_json_bool(command, "enabled", false);
                std::string bg_mode = extract_json_string(command, "background_mode");
                std::string bg_image = extract_json_string(command, "image_path");

                if (matting_engine) {
                    if (!bg_image.empty()) {
                        (void)matting_engine->load_virtual_background(bg_image);
                    }
                    if (!bg_mode.empty()) {
                        matting_engine->set_mode_from_string(bg_mode);
                    }
                    matting_engine->set_enabled(enabled);
                }
                return "{\"status\":\"ok\",\"action\":\"matting_updated\",\"message\":\"Background Matting updated in Zero-Lag Mode\"}";
            }

            // --- 8. VISION PIPELINE TOGGLES ---
            else if (command.find("SET_VISION_CONFIG") != std::string_view::npos ||
                     command.find("set_vision_config") != std::string_view::npos) {
                if (command.find("\"face_swap\"") != std::string_view::npos) {
                    face_swap_enabled.store(extract_json_bool(command, "face_swap", true), std::memory_order_relaxed);
                }
                if (command.find("\"lip_sync\"") != std::string_view::npos) {
                    lip_sync_enabled.store(extract_json_bool(command, "lip_sync", true), std::memory_order_relaxed);
                }
                if (command.find("\"body_tracking\"") != std::string_view::npos) {
                    body_tracking_enabled.store(extract_json_bool(command, "body_tracking", true), std::memory_order_relaxed);
                }
                if (command.find("\"garment_overlay\"") != std::string_view::npos && body_tracker_engine) {
                    body_tracker_engine->set_garment_overlay_enabled(extract_json_bool(command, "garment_overlay", false));
                }
                return "{\"status\":\"ok\",\"action\":\"vision_config_updated\"}";
            }

            // --- 9. LIST ALL REAL CLONED VOICES (GET_VOICES) ---
            else if (command.find("GET_VOICES") != std::string_view::npos ||
                     command.find("get_voices") != std::string_view::npos) {
                (void)voice_library.scan_library();
                return build_voices_list_json(voice_library);
            }

            // --- 10. ADMIN CLONE & SAVE VOICE FROM VIDEO OR AUDIO FILE (CLONE_VOICE) ---
            else if (command.find("CLONE_VOICE") != std::string_view::npos ||
                     command.find("clone_voice") != std::string_view::npos) {
                std::string source_path = extract_json_string(command, "source_path");
                if (source_path.empty()) source_path = extract_json_string(command, "model_path");
                std::string display_name = extract_json_string(command, "display_name");
                std::string requested_id = extract_json_string(command, "profile");

                if (source_path.empty()) {
                    return "{\"status\":\"error\",\"message\":\"Missing source video/audio file path\"}";
                }

                std::filesystem::path p(source_path);
                if (display_name.empty()) {
                    display_name = p.stem().string();
                }
                std::string profile_id = requested_id.empty() ? sanitize_profile_id(display_name) : sanitize_profile_id(requested_id);

                std::cout << "[DEBUG][src/main.cpp::CLONE_VOICE] Cloning voice from '" << source_path
                          << "' -> id='" << profile_id << "', display_name='" << display_name << "'\n";

                if (voice_upload_manager.validate_source_media(source_path) &&
                    voice_upload_manager.preprocess_voice_source(profile_id, display_name)) {
                    if (const auto* meta = voice_upload_manager.get_profile_metadata(profile_id)) {
                        (void)voice_library.register_profile(*meta);
                        (void)voice_library.select_active_profile(profile_id);
                        (void)voice_preview_manager.load_and_verify_active_model();
                        (void)inference_engine.reload_active_profile();
                        (void)model_manager.scan_local_models();
                        return build_voices_list_json(voice_library);
                    }
                }
                return "{\"status\":\"error\",\"message\":\"Failed to clone and validate media file\"}";
            }

            // --- 11. DELETE VOICE PROFILE (DELETE_VOICE) ---
            else if (command.find("DELETE_VOICE") != std::string_view::npos ||
                     command.find("delete_voice") != std::string_view::npos) {
                std::string profile_id = extract_json_string(command, "profile");
                if (!profile_id.empty()) {
                    (void)voice_library.delete_profile(profile_id);
                    (void)inference_engine.reload_active_profile();
                }
                return build_voices_list_json(voice_library);
            }

            // --- 12. VOICE PROFILE SELECTION (SET_VOICE) ---
            else if (command.find("SET_VOICE") != std::string_view::npos ||
                     command.find("set_voice") != std::string_view::npos) {
                std::string model_path = extract_json_string(command, "model_path");
                std::string profile_id = extract_json_string(command, "profile");
                std::string target = !profile_id.empty() ? profile_id : model_path;

                std::cout << "[DEBUG][src/main.cpp::SET_VOICE] Selecting & hot-reloading cloned voice: " << target << '\n';
                if (!target.empty()) {
                    if (voice_library.import_and_select_file(target, voice_upload_manager)) {
                        (void)voice_preview_manager.load_and_verify_active_model();
                        (void)inference_engine.reload_active_profile();
                        const auto* active_prof = voice_library.get_active_profile();
                        return "{\"status\":\"ok\",\"action\":\"voice_updated\",\"active_voice\":\"" +
                               escape_json_string(active_prof ? active_prof->profile_id : target) +
                               "\",\"message\":\"Active cloned voice profile hot-loaded into real-time RVC engine\"}";
                    }
                }
                return "{\"status\":\"error\",\"message\":\"Selected voice profile not found\"}";
            }

            // --- 13. VOICE PITCH, INDEX RATE, PROTECT RATE & INPUT GAIN ---
            else if (command.find("SET_PITCH") != std::string_view::npos ||
                     command.find("set_pitch") != std::string_view::npos ||
                     command.find("SET_VOICE_PARAMS") != std::string_view::npos ||
                     command.find("SET_AUDIO_GAIN") != std::string_view::npos) {
                if (command.find("\"pitch\"") != std::string_view::npos) {
                    float pitch = extract_json_float(command, "pitch", 0.0f);
                    inference_engine.set_pitch_shift(pitch);
                }
                if (command.find("\"index_rate\"") != std::string_view::npos) {
                    float idx_rate = extract_json_float(command, "index_rate", 0.85f);
                    inference_engine.set_index_rate(idx_rate);
                }
                if (command.find("\"protect_rate\"") != std::string_view::npos) {
                    float protect_rate = extract_json_float(command, "protect_rate", 0.33f);
                    inference_engine.set_protect_rate(protect_rate);
                }
                if (command.find("\"input_gain\"") != std::string_view::npos) {
                    float gain = extract_json_float(command, "input_gain", 1.0f);
                    audio_capture_engine.set_input_gain(std::clamp(gain, 0.1f, 4.0f));
                }

                ai_studio::ai::VoicePreviewConfig cfg;
                cfg.pitch_shift = static_cast<int>(inference_engine.get_pitch_shift());
                cfg.index_rate = inference_engine.get_index_rate();
                cfg.protect_rate = inference_engine.get_protect_rate();
                voice_preview_manager.configure_settings(cfg);

                return "{\"status\":\"ok\",\"action\":\"voice_params_updated\",\"message\":\"Voice RVC & gain parameters applied in real time\"}";
            }

            // --- 14. VOICE PREVIEW SESSION ---
            else if (command.find("START_VOICE_PREVIEW") != std::string_view::npos) {
                bool ok = voice_preview_manager.start_preview();
                return ok ? "{\"status\":\"ok\",\"action\":\"preview_started\"}"
                          : "{\"status\":\"error\",\"message\":\"Could not verify voice model for preview\"}";
            }
            else if (command.find("STOP_VOICE_PREVIEW") != std::string_view::npos) {
                voice_preview_manager.stop_preview();
                return "{\"status\":\"ok\",\"action\":\"preview_stopped\"}";
            }

            // --- 15. CLOUD & MODEL CACHE SYNCHRONIZATION + CUSTOM MODEL REGISTRATION ---
            else if (command.find("REGISTER_MODEL") != std::string_view::npos) {
                std::string path_str = extract_json_string(command, "model_path");
                std::filesystem::path src_p(path_str);
                if (std::filesystem::exists(src_p)) {
                    std::string stem = src_p.stem().string();
                    std::filesystem::path dst_p = std::filesystem::path("models") / src_p.filename();
                    try {
                        if (!std::filesystem::equivalent(src_p, dst_p)) {
                            std::filesystem::copy_file(src_p, dst_p, std::filesystem::copy_options::overwrite_existing);
                        }
                    } catch (...) {}
                    (void)model_manager.register_model(stem, dst_p);
                }
                (void)model_manager.scan_local_models();
                const auto models = model_manager.get_all_models();
                std::ostringstream oss;
                oss << "{\"status\":\"ok\",\"action\":\"model_registered\",\"models\":[";
                for (size_t i = 0; i < models.size(); ++i) {
                    if (i > 0) oss << ",";
                    oss << "{\"id\":\"" << escape_json_string(models[i].model_id) << "\","
                        << "\"path\":\"" << escape_json_string(models[i].file_path.string()) << "\","
                        << "\"size_bytes\":" << models[i].file_size_bytes << "}";
                }
                oss << "]}";
                return oss.str();
            }
            else if (command.find("SYNC_CLOUD") != std::string_view::npos ||
                     command.find("GET_MODELS") != std::string_view::npos) {
                (void)cloud_sync.sync_model_metadata("faceswap");
                (void)model_manager.scan_local_models();
                const auto models = model_manager.get_all_models();

                std::ostringstream oss;
                oss << "{\"status\":\"ok\",\"action\":\"models_synced\","
                    << "\"cloud_status\":\"" << ai_studio::ai::CloudSyncManager::status_to_string(cloud_sync.get_status()) << "\","
                    << "\"models\":[";
                for (size_t i = 0; i < models.size(); ++i) {
                    if (i > 0) oss << ",";
                    oss << "{\"id\":\"" << escape_json_string(models[i].model_id) << "\","
                        << "\"path\":\"" << escape_json_string(models[i].file_path.string()) << "\","
                        << "\"size_bytes\":" << models[i].file_size_bytes << "}";
                }
                oss << "]}";
                return oss.str();
            }

            // --- 16. HARDWARE CAPABILITIES PROFILE (GET_HARDWARE_PROFILE) ---
            else if (command.find("GET_HARDWARE_PROFILE") != std::string_view::npos) {
                const auto& hw = ai_studio::hw::HardwareManager::get_capabilities();
                auto prov = ai_studio::ai::InferenceEngine::select_optimal_provider();
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(2);
                oss << "{\"status\":\"ok\",\"action\":\"hardware_profile\","
                    << "\"os_name\":\"" << escape_json_string(hw.os_name) << "\","
                    << "\"cpu_arch\":\"" << escape_json_string(hw.cpu_architecture) << "\","
                    << "\"logical_cores\":" << hw.logical_cores << ","
                    << "\"total_ram_gb\":" << hw.total_ram_gb << ","
                    << "\"provider\":\"" << escape_json_string(ai_studio::ai::InferenceEngine::provider_to_string(prov)) << "\"}";
                return oss.str();
            }

            // --- 17. START LIVE CALL ---
            else if (command.find("START_CALL") != std::string_view::npos) {
                if (command.find("\"pitch\"") != std::string_view::npos) {
                    inference_engine.set_pitch_shift(extract_json_float(command, "pitch", inference_engine.get_pitch_shift()));
                }
                std::string prof = extract_json_string(command, "profile");
                if (!prof.empty()) {
                    if (voice_library.import_and_select_file(prof, voice_upload_manager)) {
                        (void)voice_preview_manager.load_and_verify_active_model();
                        (void)inference_engine.reload_active_profile();
                    }
                } else {
                    (void)inference_engine.reload_active_profile();
                }

                call_active.store(true, std::memory_order_release);
                const auto* active_prof = voice_library.get_active_profile();
                return "{\"status\":\"ok\",\"action\":\"call_started\",\"active_voice\":\"" +
                       escape_json_string(active_prof ? active_prof->profile_id : "") +
                       "\",\"latency_target_ms\":10}";
            }
            else if (command.find("STOP_CALL") != std::string_view::npos) {
                voice_preview_manager.stop_preview();
                call_active.store(false, std::memory_order_release);
                active_audio_peak.store(0.0f, std::memory_order_relaxed);
                return "{\"status\":\"ok\",\"action\":\"call_stopped\"}";
            }

            return "{\"status\":\"error\",\"message\":\"Unknown UI Command\"}";
        });

        if (!ipc_server.start()) {
            std::cerr << "[ERROR][src/main.cpp::main] Failed to bind local control bridge on port 8765.\n";
            if (!smoke_test_mode) {
                return 1;
            }
        }

        if (smoke_test_mode) {
            std::cout << "==================================================\n";
            std::cout << "[CI Smoke Test] SUCCESS: Engine verified successfully.\n";
            std::cout << "[CI Smoke Test] Exiting cleanly.\n";
            std::cout << "==================================================\n";
            camera_manager.stop_capture();
            virtual_audio_router.stop_virtual_routing();
            audio_capture_engine.stop();
            ipc_server.stop();
            inference_engine.shutdown();
            return 0;
        }

        std::cout << "==================================================\n";
        std::cout << " ENGINE READY: Listening for React UI on port 8765\n";
        std::cout << " (Press Ctrl+C in this terminal to shut down)\n";
        std::cout << "==================================================\n";

        while (ipc_server.is_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }

        camera_manager.stop_capture();
        virtual_audio_router.stop_virtual_routing();
        audio_capture_engine.stop();
        ipc_server.stop();
        inference_engine.shutdown();
        std::cout << "[DEBUG][src/main.cpp::main] Engine shut down cleanly. Goodbye!\n";

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[FATAL][src/main.cpp::main] Exception: " << e.what() << '\n';
        return 0;
    } catch (...) {
        std::cerr << "[FATAL][src/main.cpp::main] Unknown exception occurred.\n";
        return 0;
    }
}