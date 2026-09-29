#include "VoicePreviewManager.hpp"
#include <iostream>
#include <fstream>
#include <algorithm>

namespace ai_studio::ai {

VoicePreviewManager::VoicePreviewManager(VoiceLibraryManager& library_manager) noexcept
    : library_manager_(library_manager) {}

bool VoicePreviewManager::load_and_verify_active_model() noexcept {
    const auto* active_profile = library_manager_.get_active_profile();
    if (!active_profile) [[unlikely]] {
        std::cerr << "[Voice Preview Error] No active voice profile selected in library.\n";
        model_verified_.store(false, std::memory_order_release);
        return false;
    }

    const auto& model_path = active_profile->processed_model_path;
    if (!std::filesystem::exists(model_path) || std::filesystem::file_size(model_path) == 0) [[unlikely]] {
        std::cerr << "[Voice Preview Error] Model file missing or empty on disk: " << model_path.string() << "\n";
        model_verified_.store(false, std::memory_order_release);
        return false;
    }

    // Verify local model cache signature header (Zero-allocation file peek)
    try {
        std::ifstream model_file(model_path, std::ios::binary);
        if (!model_file.is_open()) [[unlikely]] {
            model_verified_.store(false, std::memory_order_release);
            return false;
        }

        char header_buffer[32] = {0};
        model_file.read(header_buffer, sizeof(header_buffer) - 1);
        std::string_view header_str(header_buffer);

        if (header_str.find("AI_STUDIO") == std::string_view::npos) [[unlikely]] {
            std::cerr << "[Voice Preview Error] Invalid local model header signature.\n";
            model_verified_.store(false, std::memory_order_release);
            return false;
        }

        std::cout << "[Voice Preview] Model verified successfully for profile: " << active_profile->display_name 
                  << " (" << model_path.filename().string() << ")\n";
        
        model_verified_.store(true, std::memory_order_release);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Voice Preview Exception] Model verification failed: " << e.what() << "\n";
        model_verified_.store(false, std::memory_order_release);
        return false;
    }
}

void VoicePreviewManager::configure_settings(const VoicePreviewConfig& config) noexcept {
    current_config_ = config;
    std::cout << "[Voice Preview] Configuration updated -> Pitch Shift: " << current_config_.pitch_shift 
              << " semitones | Index Rate: " << current_config_.index_rate 
              << " | Protection: " << current_config_.protect_rate << "\n";
}

bool VoicePreviewManager::start_preview() noexcept {
    if (!model_verified_.load(std::memory_order_acquire)) [[unlikely]] {
        std::cerr << "[Voice Preview Error] Cannot start preview. Model not verified.\n";
        return false;
    }

    preview_active_.store(true, std::memory_order_release);
    std::cout << "[Voice Preview] Local microphone preview session started (Zero-lag conversion active)...\n";
    return true;
}

void VoicePreviewManager::stop_preview() noexcept {
    preview_active_.store(false, std::memory_order_release);
    std::cout << "[Voice Preview] Local microphone preview session stopped cleanly.\n";
}

} // namespace ai_studio::ai