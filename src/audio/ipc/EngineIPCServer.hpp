#pragma once

#include <string>
#include <string_view>
#include <atomic>
#include <thread>
#include <functional>
#include <cstdint>

namespace ai_studio::ipc {

class EngineIPCServer {
public:
    using CommandCallback = std::function<std::string(std::string_view command_json)>;

    explicit EngineIPCServer(uint16_t port = 8765) noexcept;
    ~EngineIPCServer() noexcept;

    // Prevent copying and moving
    EngineIPCServer(const EngineIPCServer&) = delete;
    EngineIPCServer& operator=(const EngineIPCServer&) = delete;
    EngineIPCServer(EngineIPCServer&&) = delete;
    EngineIPCServer& operator=(EngineIPCServer&&) = delete;

    // Start local IPC server background listener thread
    [[nodiscard]] bool start() noexcept;

    // Stop local IPC server cleanly
    void stop() noexcept;

    // Register callback for incoming UI commands
    void set_command_callback(CommandCallback callback) noexcept;

    [[nodiscard]] inline bool is_running() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

private:
    void server_loop() noexcept;

    uint16_t port_;
    std::atomic<bool> running_{false};
    std::thread server_thread_;
    CommandCallback command_callback_;
    
#if defined(_WIN32)
    uint64_t server_socket_{0};
#else
    int server_socket_{-1};
#endif
};

} // namespace ai_studio::ipc