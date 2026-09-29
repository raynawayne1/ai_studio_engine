#include "EngineIPCServer.hpp"
#include <iostream>
#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#endif

namespace ai_studio::ipc {

EngineIPCServer::EngineIPCServer(uint16_t port) noexcept
    : port_(port) {}

EngineIPCServer::~EngineIPCServer() noexcept {
    stop();
}

bool EngineIPCServer::start() noexcept {
    if (running_.load(std::memory_order_acquire)) {
        return true;
    }

#if defined(_WIN32)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[IPC Error] WSAStartup failed.\n";
        return false;
    }
#endif

    server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
#if defined(_WIN32)
    if (server_socket_ == static_cast<uint64_t>(INVALID_SOCKET)) {
#else
    if (server_socket_ < 0) {
#endif
        std::cerr << "[IPC Error] Failed to create IPC socket.\n";
        return false;
    }

    // Reuse port instantly without TIME_WAIT OS lag
    int opt = 1;
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // Strict localhost binding for security
    address.sin_port = htons(port_);

    if (bind(server_socket_, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "[IPC Error] Failed to bind IPC socket to port " << port_ << ".\n";
#if defined(_WIN32)
        closesocket(server_socket_);
        WSACleanup();
#else
        close(server_socket_);
#endif
        return false;
    }

    if (listen(server_socket_, 5) < 0) {
        std::cerr << "[IPC Error] Failed to listen on IPC socket.\n";
#if defined(_WIN32)
        closesocket(server_socket_);
        WSACleanup();
#else
        close(server_socket_);
#endif
        return false;
    }

    running_.store(true, std::memory_order_release);
    server_thread_ = std::thread(&EngineIPCServer::server_loop, this);

    std::cout << "[IPC Server] Local UI control bridge active on port " << port_ << " (Zero-Lag TCP Pipeline)...\n";
    return true;
}

void EngineIPCServer::stop() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    if (server_thread_.joinable()) {
        server_thread_.join();
    }

#if defined(_WIN32)
    if (server_socket_ != static_cast<uint64_t>(INVALID_SOCKET)) {
        closesocket(server_socket_);
        server_socket_ = static_cast<uint64_t>(INVALID_SOCKET);
    }
    WSACleanup();
#else
    if (server_socket_ >= 0) {
        close(server_socket_);
        server_socket_ = -1;
    }
#endif

    std::cout << "[IPC Server] Local UI control bridge shut down cleanly.\n";
}

void EngineIPCServer::set_command_callback(CommandCallback callback) noexcept {
    command_callback_ = std::move(callback);
}

void EngineIPCServer::server_loop() noexcept {
    while (running_.load(std::memory_order_relaxed)) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_socket_, &read_fds);

        // Non-blocking select timeout (50ms) to ensure instant thread shutdown
        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 50000;

        int activity = select(static_cast<int>(server_socket_ + 1), &read_fds, nullptr, nullptr, &timeout);
        if (activity <= 0) {
            continue; // Timeout or error, check running_ flag again
        }

        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        
#if defined(_WIN32)
        uint64_t client_socket = accept(server_socket_, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
        if (client_socket == static_cast<uint64_t>(INVALID_SOCKET)) continue;
#else
        int client_socket = accept(server_socket_, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
        if (client_socket < 0) continue;
#endif

        // Disable Nagle's Algorithm for instant, zero-lag UI command transmission
        int flag = 1;
        setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));

        char buffer[2048] = {0};
        
        // Native Windows (int) vs Native POSIX (ssize_t) routing
#if defined(_WIN32)
        int bytes_read = recv(client_socket, buffer, static_cast<int>(sizeof(buffer) - 1), 0);
#else
        ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
#endif

        if (bytes_read > 0) {
            std::string_view request(buffer, static_cast<size_t>(bytes_read));
            std::string response = "{\"status\":\"ok\",\"message\":\"AI Studio Engine Ready\"}";
            
            if (command_callback_) {
                response = command_callback_(request);
            }

            // Native Windows (int) vs Native POSIX (size_t) payload routing
#if defined(_WIN32)
            send(client_socket, response.c_str(), static_cast<int>(response.size()), 0);
#else
            send(client_socket, response.c_str(), response.size(), 0);
#endif
        }

#if defined(_WIN32)
        closesocket(client_socket);
#else
        close(client_socket);
#endif
    }
}

} // namespace ai_studio::ipc