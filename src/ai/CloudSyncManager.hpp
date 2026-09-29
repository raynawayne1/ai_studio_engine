#pragma once

#include <string>
#include <string_view>
#include <filesystem>
#include <atomic>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
#endif

namespace ai_studio::ai {

enum class SyncStatus {
    Idle,
    Synchronizing,
    UpToDate,
    Error
};

class CloudSyncManager {
public:
    explicit CloudSyncManager(std::filesystem::path local_cache_dir) noexcept;
    ~CloudSyncManager() noexcept = default;

    // Prevent copying and moving
    CloudSyncManager(const CloudSyncManager&) = delete;
    CloudSyncManager& operator=(const CloudSyncManager&) = delete;
    CloudSyncManager(CloudSyncManager&&) = delete;
    CloudSyncManager& operator=(CloudSyncManager&&) = delete;

    // Initiates asynchronous cloud metadata synchronization (Firestore/Storage abstraction)
    [[nodiscard]] bool sync_model_metadata(std::string_view model_id) noexcept;

    // Verifies local asset integrity against cloud synchronization targets
    [[nodiscard]] bool verify_local_asset(std::string_view model_id) const noexcept;

    [[nodiscard]] SyncStatus get_status() const noexcept {
        return status_.load(std::memory_order_relaxed);
    }

private:
    std::filesystem::path local_cache_dir_;
    alignas(64) std::atomic<SyncStatus> status_{SyncStatus::Idle};
};

} // namespace ai_studio::ai

#if defined(_MSC_VER)
#pragma warning(pop)
#endif