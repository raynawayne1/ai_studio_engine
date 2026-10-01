#include "EngineIPCServer.hpp"
#include <iostream>
#include <cstring>
#include <string>
#include <vector>
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

constexpr std::string_view HTTP_JPEG_HEADER_START = 
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: image/jpeg\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Cache-Control: no-store, no-cache, must-revalidate\r\n"
    "Content-Length: ";

constexpr std::string_view HTTP_BINARY_HEADER_START = 
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/octet-stream\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Cache-Control: no-store\r\n"
    "Content-Length: ";

constexpr std::string_view HTTP_RESPONSE_HEADER_END = 
    "\r\nConnection: close\r\n\r\n";

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

static void send_all_bytes(
#if defined(_WIN32)
    uint64_t sock,
#else
    int sock,
#endif
    const char* data,
    size_t len
) noexcept {
    size_t total_sent = 0;
    while (total_sent < len) {
#if defined(_WIN32)
        int sent = send(static_cast<SOCKET>(sock), data + total_sent, static_cast<int>(len - total_sent), 0);
#elif defined(__linux__)
        ssize_t sent = send(sock, data + total_sent, len - total_sent, MSG_NOSIGNAL);
#else
        ssize_t sent = send(sock, data + total_sent, len - total_sent, 0);
#endif
        if (sent <= 0) break;
        total_sent += static_cast<size_t>(sent);
    }
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
        std::cerr << "[ERROR][src/audio/ipc/EngineIPCServer.cpp::start] WSAStartup failed.\n";
        return false;
    }
#else
    std::signal(SIGPIPE, SIG_IGN);
#endif

    server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
#if defined(_WIN32)
    if (server_socket_ == static_cast<uint64_t>(INVALID_SOCKET)) {
#else
    if (server_socket_ < 0) {
#endif
        std::cerr << "[ERROR][src/audio/ipc/EngineIPCServer.cpp::start] Failed to create IPC socket.\n";
        return false;
    }

    int opt = 1;
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port_);

    if (bind(server_socket_, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "[ERROR][src/audio/ipc/EngineIPCServer.cpp::start] Failed to bind IPC socket to 127.0.0.1:" << port_ << ".\n";
#if defined(_WIN32)
        closesocket(static_cast<SOCKET>(server_socket_));
        WSACleanup();
#else
        close(server_socket_);
#endif
        return false;
    }

    if (listen(server_socket_, 64) < 0) {
        std::cerr << "[ERROR][src/audio/ipc/EngineIPCServer.cpp::start] Failed to listen on IPC socket.\n";
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

    std::cout << "[DEBUG][src/audio/ipc/EngineIPCServer.cpp::start] IPC Server listening on http://127.0.0.1:" << port_
              << " (Endpoints: POST /, GET /frame/source, GET /frame/output, POST /frame/push, POST /audio/process)\n";
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

    std::cout << "[DEBUG][src/audio/ipc/EngineIPCServer.cpp::stop] Local UI control bridge shut down cleanly.\n";
}

void EngineIPCServer::set_command_callback(CommandCallback callback) noexcept {
    command_callback_ = std::move(callback);
    std::cout << "[DEBUG][src/audio/ipc/EngineIPCServer.cpp::set_command_callback] JSON command handler registered.\n";
}

void EngineIPCServer::set_frame_callbacks(
    FrameProviderCallback source_cb,
    FrameProviderCallback output_cb,
    FramePushCallback push_cb
) noexcept {
    source_frame_callback_ = std::move(source_cb);
    output_frame_callback_ = std::move(output_cb);
    frame_push_callback_ = std::move(push_cb);
    std::cout << "[DEBUG][src/audio/ipc/EngineIPCServer.cpp::set_frame_callbacks] Binary JPEG frame callbacks registered.\n";
}

void EngineIPCServer::set_audio_process_callback(AudioProcessCallback audio_cb) noexcept {
    audio_process_callback_ = std::move(audio_cb);
    std::cout << "[DEBUG][src/audio/ipc/EngineIPCServer.cpp::set_audio_process_callback] Real-time 48kHz PCM audio callback registered.\n";
}

void EngineIPCServer::server_loop() noexcept {
    std::vector<char> rx_buffer(524288); // 512 KB reusable receive buffer
    uint64_t served_source_frames = 0;
    uint64_t served_output_frames = 0;
    uint64_t pushed_phone_frames = 0;
    uint64_t processed_audio_chunks = 0;

    while (running_.load(std::memory_order_relaxed)) {
        fd_set read_fds;
        FD_ZERO(&read_fds);

#if defined(_WIN32)
        FD_SET(static_cast<SOCKET>(server_socket_), &read_fds);
#else
        FD_SET(server_socket_, &read_fds);
#endif

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 15000;

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

        int flag = 1;
#if defined(_WIN32)
        setsockopt(static_cast<SOCKET>(client_socket), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
        DWORD rcv_timeout_ms = 50;
        setsockopt(static_cast<SOCKET>(client_socket), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcv_timeout_ms), sizeof(rcv_timeout_ms));
#else
        setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
        #if defined(__APPLE__)
        setsockopt(client_socket, SOL_SOCKET, SO_NOSIGPIPE, &flag, sizeof(flag));
        #endif
        timeval rcv_tv{};
        rcv_tv.tv_sec = 0;
        rcv_tv.tv_usec = 50000;
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &rcv_tv, sizeof(rcv_tv));
#endif

        size_t total_read = 0;
#if defined(_WIN32)
        int bytes_read = recv(static_cast<SOCKET>(client_socket), rx_buffer.data(), static_cast<int>(rx_buffer.size() - 1), 0);
#else
        ssize_t bytes_read = recv(client_socket, rx_buffer.data(), rx_buffer.size() - 1, 0);
#endif

        if (bytes_read > 0) {
            total_read = static_cast<size_t>(bytes_read);
            std::string_view request(rx_buffer.data(), total_read);

            if (request.starts_with("OPTIONS")) {
                send_all_bytes(client_socket, HTTP_CORS_OPTIONS_RESPONSE.data(), HTTP_CORS_OPTIONS_RESPONSE.size());
            }
            // Fast Binary GET /frame/source or GET /frame/output
            else if (request.starts_with("GET /frame/source") || request.starts_with("GET /frame/output")) {
                bool is_source = request.starts_with("GET /frame/source");
                std::vector<uint8_t> jpeg_bytes;
                if (is_source && source_frame_callback_) {
                    jpeg_bytes = source_frame_callback_();
                } else if (!is_source && output_frame_callback_) {
                    jpeg_bytes = output_frame_callback_();
                }

                if (jpeg_bytes.empty()) {
                    send_all_bytes(client_socket, HTTP_CORS_OPTIONS_RESPONSE.data(), HTTP_CORS_OPTIONS_RESPONSE.size());
                } else {
                    if (is_source) {
                        ++served_source_frames;
                        if (served_source_frames == 1 || served_source_frames % 120 == 0) {
                            std::cout << "[DEBUG][src/audio/ipc/EngineIPCServer.cpp::server_loop] Served GET /frame/source #"
                                      << served_source_frames << " (" << jpeg_bytes.size() << " bytes)\n";
                        }
                    } else {
                        ++served_output_frames;
                        if (served_output_frames == 1 || served_output_frames % 120 == 0) {
                            std::cout << "[DEBUG][src/audio/ipc/EngineIPCServer.cpp::server_loop] Served GET /frame/output #"
                                      << served_output_frames << " (" << jpeg_bytes.size() << " bytes)\n";
                        }
                    }

                    std::string header;
                    std::string len_str = std::to_string(jpeg_bytes.size());
                    header.reserve(HTTP_JPEG_HEADER_START.size() + len_str.size() + HTTP_RESPONSE_HEADER_END.size());
                    header.append(HTTP_JPEG_HEADER_START);
                    header.append(len_str);
                    header.append(HTTP_RESPONSE_HEADER_END);

                    send_all_bytes(client_socket, header.data(), header.size());
                    send_all_bytes(client_socket, reinterpret_cast<const char*>(jpeg_bytes.data()), jpeg_bytes.size());
                }
            }
            else {
                auto header_end_pos = request.find("\r\n\r\n");
                size_t body_offset = 0;
                size_t expected_body_len = 0;

                if (header_end_pos != std::string_view::npos) {
                    body_offset = header_end_pos + 4;
                    expected_body_len = parse_content_length(request.substr(0, header_end_pos));
                    while (expected_body_len > 0 &&
                           (total_read - body_offset) < expected_body_len &&
                           total_read < (rx_buffer.size() - 1)) {
#if defined(_WIN32)
                        int more = recv(static_cast<SOCKET>(client_socket), rx_buffer.data() + total_read, static_cast<int>(rx_buffer.size() - 1 - total_read), 0);
#else
                        ssize_t more = recv(client_socket, rx_buffer.data() + total_read, rx_buffer.size() - 1 - total_read, 0);
#endif
                        if (more <= 0) break;
                        total_read += static_cast<size_t>(more);
                    }
                    request = std::string_view(rx_buffer.data(), total_read);
                }

                // Real-time binary float32 microphone audio conversion endpoint: POST /audio/process
                if (request.starts_with("POST /audio/process")) {
                    if (audio_process_callback_ && body_offset > 0 && total_read > body_offset) {
                        const size_t byte_len = total_read - body_offset;
                        const size_t sample_count = byte_len / sizeof(float);
                        std::vector<float> aligned_in(sample_count, 0.0f);
                        std::memcpy(aligned_in.data(), rx_buffer.data() + body_offset, sample_count * sizeof(float));

                        std::vector<float> converted = audio_process_callback_(aligned_in.data(), sample_count);
                        const size_t out_bytes = converted.size() * sizeof(float);

                        ++processed_audio_chunks;
                        if (processed_audio_chunks == 1 || processed_audio_chunks % 120 == 0) {
                            std::cout << "[DEBUG][src/audio/ipc/EngineIPCServer.cpp::server_loop] Processed POST /audio/process chunk #"
                                      << processed_audio_chunks << " (" << sample_count << " float32 samples)\n";
                        }

                        std::string header;
                        std::string len_str = std::to_string(out_bytes);
                        header.reserve(HTTP_BINARY_HEADER_START.size() + len_str.size() + HTTP_RESPONSE_HEADER_END.size());
                        header.append(HTTP_BINARY_HEADER_START);
                        header.append(len_str);
                        header.append(HTTP_RESPONSE_HEADER_END);

                        send_all_bytes(client_socket, header.data(), header.size());
                        if (out_bytes > 0) {
                            send_all_bytes(client_socket, reinterpret_cast<const char*>(converted.data()), out_bytes);
                        }
                    } else {
                        send_all_bytes(client_socket, HTTP_CORS_OPTIONS_RESPONSE.data(), HTTP_CORS_OPTIONS_RESPONSE.size());
                    }
                }
                else {
                    std::string json_response = "{\"status\":\"ok\"}";

                    if (request.starts_with("POST /frame/push")) {
                        bool pushed = false;
                        if (frame_push_callback_ && body_offset > 0 && total_read > body_offset) {
                            const uint8_t* jpg_ptr = reinterpret_cast<const uint8_t*>(rx_buffer.data() + body_offset);
                            size_t jpg_len = total_read - body_offset;
                            pushed = frame_push_callback_(jpg_ptr, jpg_len);
                            if (pushed) {
                                ++pushed_phone_frames;
                                if (pushed_phone_frames == 1 || pushed_phone_frames % 120 == 0) {
                                    std::cout << "[DEBUG][src/audio/ipc/EngineIPCServer.cpp::server_loop] Received POST /frame/push #"
                                              << pushed_phone_frames << " (" << jpg_len << " bytes)\n";
                                }
                            }
                        }
                        json_response = pushed
                            ? "{\"status\":\"ok\",\"action\":\"frame_pushed\"}"
                            : "{\"status\":\"error\",\"message\":\"Invalid JPEG frame\"}";
                    } else {
                        std::string_view body_json = (body_offset > 0 && total_read >= body_offset)
                            ? request.substr(body_offset)
                            : request;
                        if (command_callback_) {
                            json_response = command_callback_(body_json);
                        }
                    }

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

                    send_all_bytes(client_socket, http_response.data(), http_response.size());
                }
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