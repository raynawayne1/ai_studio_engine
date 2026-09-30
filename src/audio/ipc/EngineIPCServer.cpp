#include "EngineIPCServer.hpp"
#include <iostream>
#include <cstring>
#include <string>
#include <charconv>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#if defined(_MSC_VER)
#pragma comment(lib, "Ws2_32.lib")
#endif
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <csignal>
#endif

namespace ai_studio::ipc {

// Pre-allocate static HTTP headers for zero-allocation response framing
constexpr std::string_view HTTP_CORS_OPTIONS_RESPONSE = 
    "HTTP/1.1 204 No Content\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
    "Access-Control-Allow-Headers: Content-Type\r\n"
    "Connection: close\r\n\r\n";

constexpr std::string_view HTTP_RESPONSE_HEADER_START = 
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
    "Access-Control-Allow-Headers: Content-Type\r\n"
    "Cache-Control: no-store\r\n"
    "Content-Length: ";

constexpr std::string_view HTTP_RESPONSE_HEADER_END = 
    "\r\nConnection: close\r\n\r\n";

// Fast zero-allocation Content-Length parser for split TCP segments
static size_t parse_content_length(std::string_view headers) noexcept {
    constexpr std::string_view cl_key_upper = "Content-Length:";
    constexpr std::string_view cl_key_lower = "content-length:";

    size_t pos = headers.find(cl_key_upper);
    if (pos == std::string_view::npos) {
        pos = headers.find(cl_key_lower);
    }
    if (pos == std::string_view::npos) {
        return 0;
    }

    pos += cl_key_upper.size();
    while (pos < headers.size() && (headers[pos] == ' ' || headers[pos] == '\t')) {
        ++pos;
    }

    size_t length = 0;
    std::from_chars(headers.data() + pos, headers.data() + headers.size(), length);
    return length;
}

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
#else
    // Ignore SIGPIPE globally on POSIX so broken client pipes never kill the daemon
    std::signal(SIGPIPE, SIG_IGN);
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

    // High-speed port binding reuse to avoid OS TIME_WAIT lockouts
    int opt = 1;
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // Strict localhost binding for security
    address.sin_port = htons(port_);

    if (bind(server_socket_, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "[IPC Error] Failed to bind IPC socket to port " << port_ << ".\n";
#if defined(_WIN32)
        closesocket(static_cast<SOCKET>(server_socket_));
        WSACleanup();
#else
        close(server_socket_);
#endif
        return false;
    }

    if (listen(server_socket_, 16) < 0) {
        std::cerr << "[IPC Error] Failed to listen on IPC socket.\n";
#if defined(_WIN32)
        closesocket(static_cast<SOCKET>(server_socket_));
        WSACleanup();
#else
        close(server_socket_);
#endif
        return false;
    }

    running_.store(true, std::memory_order_release);
    server_thread_ = std::thread(&EngineIPCServer::server_loop, this);

    std::cout << "[IPC Server] Local UI control bridge active on port " << port_ << " (Ultra-Fast HTTP/CORS Enabled)...\n";
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
        closesocket(static_cast<SOCKET>(server_socket_));
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
        
#if defined(_WIN32)
        FD_SET(static_cast<SOCKET>(server_socket_), &read_fds);
#else
        FD_SET(server_socket_, &read_fds);
#endif

        // Non-blocking select timeout (25ms) for ultra-responsive shutdown and polling
        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 25000;

        int activity = select(static_cast<int>(server_socket_ + 1), &read_fds, nullptr, nullptr, &timeout);
        if (activity <= 0) {
            continue;
        }

        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        
#if defined(_WIN32)
        uint64_t client_socket = static_cast<uint64_t>(accept(static_cast<SOCKET>(server_socket_), reinterpret_cast<struct sockaddr*>(&client_addr), &client_len));
        if (client_socket == static_cast<uint64_t>(INVALID_SOCKET)) continue;
#else
        int client_socket = accept(server_socket_, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
        if (client_socket < 0) continue;
#endif

        // 1. Disable Nagle's Algorithm for instant sub-millisecond UI response
        int flag = 1;
#if defined(_WIN32)
        setsockopt(static_cast<SOCKET>(client_socket), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
        DWORD rcv_timeout_ms = 25;
        setsockopt(static_cast<SOCKET>(client_socket), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcv_timeout_ms), sizeof(rcv_timeout_ms));
#else
        setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
        #if defined(__APPLE__)
        setsockopt(client_socket, SOL_SOCKET, SO_NOSIGPIPE, &flag, sizeof(flag));
        #endif
        timeval rcv_tv{};
        rcv_tv.tv_sec = 0;
        rcv_tv.tv_usec = 25000;
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &rcv_tv, sizeof(rcv_tv));
#endif

        char buffer[8192] = {0};
        size_t total_read = 0;

#if defined(_WIN32)
        int bytes_read = recv(static_cast<SOCKET>(client_socket), buffer, static_cast<int>(sizeof(buffer) - 1), 0);
#else
        ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
#endif

        if (bytes_read > 0) {
            total_read = static_cast<size_t>(bytes_read);
            std::string_view request(buffer, total_read);

            // Ultra-fast Browser CORS Preflight validation
            if (request.starts_with("OPTIONS")) {
#if defined(_WIN32)
                send(static_cast<SOCKET>(client_socket), HTTP_CORS_OPTIONS_RESPONSE.data(), static_cast<int>(HTTP_CORS_OPTIONS_RESPONSE.size()), 0);
#elif defined(__linux__)
                send(client_socket, HTTP_CORS_OPTIONS_RESPONSE.data(), HTTP_CORS_OPTIONS_RESPONSE.size(), MSG_NOSIGNAL);
#else
                send(client_socket, HTTP_CORS_OPTIONS_RESPONSE.data(), HTTP_CORS_OPTIONS_RESPONSE.size(), 0);
#endif
            } else {
                // Ensure full HTTP payload is received if headers and body arrived in separate TCP frames
                auto header_end_pos = request.find("\r\n\r\n");
                if (header_end_pos != std::string_view::npos) {
                    size_t body_offset = header_end_pos + 4;
                    size_t expected_body_len = parse_content_length(request.substr(0, header_end_pos));
                    while (expected_body_len > 0 &&
                           (total_read - body_offset) < expected_body_len &&
                           total_read < (sizeof(buffer) - 1)) {
#if defined(_WIN32)
                        int more = recv(static_cast<SOCKET>(client_socket), buffer + total_read, static_cast<int>(sizeof(buffer) - 1 - total_read), 0);
#else
                        ssize_t more = recv(client_socket, buffer + total_read, sizeof(buffer) - 1 - total_read, 0);
#endif
                        if (more <= 0) break;
                        total_read += static_cast<size_t>(more);
                    }
                    request = std::string_view(buffer, total_read);
                }

                // Zero-copy HTTP payload extraction
                std::string_view body_json = request;
                if (auto pos = request.find("\r\n\r\n"); pos != std::string_view::npos) {
                    body_json = request.substr(pos + 4);
                }

                std::string json_response = "{\"status\":\"ok\",\"message\":\"AI Studio Engine Ready\"}";
                if (command_callback_) {
                    json_response = command_callback_(body_json);
                }

                // Minimum-allocation exact-size string reserve pipeline
                std::string http_response;
                std::string content_length_str = std::to_string(json_response.size());
                
                http_response.reserve(
                    HTTP_RESPONSE_HEADER_START.size() + 
                    content_length_str.size() + 
                    HTTP_RESPONSE_HEADER_END.size() + 
                    json_response.size()
                );
                
                http_response.append(HTTP_RESPONSE_HEADER_START);
                http_response.append(content_length_str);
                http_response.append(HTTP_RESPONSE_HEADER_END);
                http_response.append(json_response);

#if defined(_WIN32)
                send(static_cast<SOCKET>(client_socket), http_response.c_str(), static_cast<int>(http_response.size()), 0);
#elif defined(__linux__)
                send(client_socket, http_response.c_str(), http_response.size(), MSG_NOSIGNAL);
#else
                send(client_socket, http_response.c_str(), http_response.size(), 0);
#endif
            }
        }

#if defined(_WIN32)
        closesocket(static_cast<SOCKET>(client_socket));
#else
        close(client_socket);
#endif
    }
}

} // namespace ai_studio::ipc