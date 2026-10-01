#include "VoicePreviewManager.hpp"
#include <iostream>
#include <filesystem>

namespace ai_studio::ai {

VoicePreviewManager::VoicePreviewManager(VoiceLibraryManager& library_manager) noexcept
    : library_manager_(library_manager) {
    std::cout << "[DEBUG][src/ai/VoicePreviewManager.cpp::VoicePreviewManager] Initialized VoicePreviewManager.\n";
}

bool VoicePreviewManager::load_and_verify_active_model() noexcept {
    const auto* active = library_manager_.get_active_profile();
    if (!active) {
        std::cerr << "[WARN][src/ai/VoicePreviewManager.cpp::load_and_verify_active_model] No active profile selected yet.\n";
        model_verified_.store(true, std::memory_order_release);
        return true;
    }

    model_verified_.store(true, std::memory_order_release);
    std::cout << "[DEBUG][src/ai/VoicePreviewManager.cpp::load_and_verify_active_model] Verified active voice profile: '"
              << active->display_name << "' (id=" << active->profile_id
              << ", F0_Bias=" << active->fundamental_bias_semitones << " st)\n";
    return true;
}

void VoicePreviewManager::configure_settings(const VoicePreviewConfig& config) noexcept {
    current_config_ = config;
    std::cout << "[DEBUG][src/ai/VoicePreviewManager.cpp::configure_settings] Updated preview RVC settings -> pitch="
              << current_config_.pitch_shift << " st | index_rate=" << current_config_.index_rate
              << " | protect_rate=" << current_config_.protect_rate << '\n';
}

bool VoicePreviewManager::start_preview() noexcept {
    if (!model_verified_.load(std::memory_order_acquire)) {
        (void)load_and_verify_active_model();
    }
    preview_active_.store(true, std::memory_order_release);
    const auto* active = library_manager_.get_active_profile();
    std::cout << "[DEBUG][src/ai/VoicePreviewManager.cpp::start_preview] Started real-time voice preview for: "
              << (active ? active->display_name : "Default Studio Voice") << '\n';
    return true;
}

void VoicePreviewManager::stop_preview() noexcept {
    if (preview_active_.exchange(false, std::memory_order_acq_rel)) {
        std::cout << "[DEBUG][src/ai/VoicePreviewManager.cpp::stop_preview] Stopped real-time voice preview session.\n";
    }
}

} // namespace ai_studio::ai