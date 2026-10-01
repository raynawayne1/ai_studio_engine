#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <atomic>
#include <thread>
#include <functional>
#include <cstdint>

namespace ai_studio::ipc {

class EngineIPCServer {
public:
    using CommandCallback = std::function<std::string(std::string_view command_json)>;
    using FrameProviderCallback = std::function<std::vector<uint8_t>()>;
    using FramePushCallback = std::function<bool(const uint8_t* data, size_t size)>;
    using AudioProcessCallback = std::function<std::vector<float>(const float* samples, size_t sample_count)>;

    explicit EngineIPCServer(uint16_t port = 8765) noexcept;
    ~EngineIPCServer() noexcept;

    EngineIPCServer(const EngineIPCServer&) = delete;
    EngineIPCServer& operator=(const EngineIPCServer&) = delete;
    EngineIPCServer(EngineIPCServer&&) = delete;
    EngineIPCServer& operator=(EngineIPCServer&&) = delete;

    [[nodiscard]] bool start() noexcept;
    void stop() noexcept;

    void set_command_callback(CommandCallback callback) noexcept;
    void set_frame_callbacks(
        FrameProviderCallback source_cb,
        FrameProviderCallback output_cb,
        FramePushCallback push_cb
    ) noexcept;
    void set_audio_process_callback(AudioProcessCallback audio_cb) noexcept;

    [[nodiscard]] inline bool is_running() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

private:
    void server_loop() noexcept;

    uint16_t port_;
    std::atomic<bool> running_{false};
    std::thread server_thread_;
    CommandCallback command_callback_;
    FrameProviderCallback source_frame_callback_;
    FrameProviderCallback output_frame_callback_;
    FramePushCallback frame_push_callback_;
    AudioProcessCallback audio_process_callback_;

#if defined(_WIN32)
    uint64_t server_socket_{0};
#else
    int server_socket_{-1};
#endif
};

} // namespace ai_studio::ipc