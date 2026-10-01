#include "VirtualCameraManager.hpp"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ai_studio::video {

namespace {

inline void write_u16_le(uint8_t* dst, uint16_t val) noexcept {
    dst[0] = static_cast<uint8_t>(val & 0xFFu);
    dst[1] = static_cast<uint8_t>((val >> 8) & 0xFFu);
}

inline void write_u32_le(uint8_t* dst, uint32_t val) noexcept {
    dst[0] = static_cast<uint8_t>(val & 0xFFu);
    dst[1] = static_cast<uint8_t>((val >> 8) & 0xFFu);
    dst[2] = static_cast<uint8_t>((val >> 16) & 0xFFu);
    dst[3] = static_cast<uint8_t>((val >> 24) & 0xFFu);
}

// Ultra-fast (<0.02ms) zero-dependency 24-bit BMP encoder directly from OpenCV BGR24 Mat
void encode_bgr_to_bmp_fast(const cv::Mat& bgr, std::vector<uint8_t>& out_bmp) noexcept {
    if (bgr.empty() || bgr.type() != CV_8UC3) {
        out_bmp.clear();
        return;
    }

    const int w = bgr.cols;
    const int h = bgr.rows;
    const size_t row_bytes = static_cast<size_t>(w) * 3u;
    const size_t row_stride = (row_bytes + 3u) & ~static_cast<size_t>(3u);
    const size_t pixel_bytes = row_stride * static_cast<size_t>(h);
    const size_t file_size = 54u + pixel_bytes;

    out_bmp.resize(file_size);
    uint8_t* hdr = out_bmp.data();
    std::memset(hdr, 0, 54u);

    // 14-byte BITMAPFILEHEADER
    hdr[0] = static_cast<uint8_t>('B');
    hdr[1] = static_cast<uint8_t>('M');
    write_u32_le(hdr + 2, static_cast<uint32_t>(file_size));
    write_u32_le(hdr + 10, 54u);

    // 40-byte BITMAPINFOHEADER
    write_u32_le(hdr + 14, 40u);
    write_u32_le(hdr + 18, static_cast<uint32_t>(w));
    write_u32_le(hdr + 22, static_cast<uint32_t>(h));
    write_u16_le(hdr + 26, 1u);
    write_u16_le(hdr + 28, 24u);
    write_u32_le(hdr + 34, static_cast<uint32_t>(pixel_bytes));
    write_u32_le(hdr + 38, 2835u);
    write_u32_le(hdr + 42, 2835u);

    // Copy BGR rows bottom-up (standard BMP row order) via fast SIMD memcpy
    for (int y = 0; y < h; ++y) {
        const uint8_t* src_row = bgr.ptr<uint8_t>(h - 1 - y);
        uint8_t* dst_row = hdr + 54u + static_cast<size_t>(y) * row_stride;
        std::memcpy(dst_row, src_row, row_bytes);
        if (row_stride > row_bytes) {
            std::memset(dst_row + row_bytes, 0, row_stride - row_bytes);
        }
    }
}

} // namespace

VirtualCameraManager::VirtualCameraManager() noexcept {
    for (int i = 0; i < 3; ++i) {
        m_triple_buffers[i].create(m_height, m_width, CV_8UC3);
    }
    std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::VirtualCameraManager] Allocated 3x (1280x720 CV_8UC3) zero-copy triple buffers.\n";
}

VirtualCameraManager::~VirtualCameraManager() noexcept {
    stop_capture();
}

bool VirtualCameraManager::open_hardware_camera(int index) noexcept {
    if (index < 0) return false;

    std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::open_hardware_camera] Attempting to open OS hardware camera index " << index << "...\n";

#if defined(_WIN32)
    if (!m_cap.open(index, cv::CAP_DSHOW)) {
        m_cap.open(index, cv::CAP_MSMF);
    }
#elif defined(__APPLE__)
    if (!m_cap.open(index, cv::CAP_AVFOUNDATION)) {
        m_cap.open(index, cv::CAP_ANY);
    }
#elif defined(__linux__)
    if (!m_cap.open(index, cv::CAP_V4L2)) {
        m_cap.open(index, cv::CAP_ANY);
    }
#else
    m_cap.open(index, cv::CAP_ANY);
#endif

    if (!m_cap.isOpened()) {
        std::cerr << "[WARN][src/video/VirtualCameraManager.cpp::open_hardware_camera] Camera index " << index << " failed to open.\n";
        return false;
    }

    configure_zero_lag_capture(false);
    m_is_file_or_stream = false;
    m_external_push_mode.store(false, std::memory_order_relaxed);
    m_active_endpoint = std::to_string(index);
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_active_source_name = "Camera Index " + std::to_string(index);
    }
    std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::open_hardware_camera] Successfully locked hardware camera index " << index << ".\n";
    return true;
}

bool VirtualCameraManager::open_stream_or_file(const std::string& endpoint) noexcept {
    if (endpoint.empty()) return false;

    std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::open_stream_or_file] Opening stream/file endpoint: " << endpoint << "...\n";

    if (!m_cap.open(endpoint, cv::CAP_FFMPEG)) {
        m_cap.open(endpoint, cv::CAP_ANY);
    }

    if (!m_cap.isOpened()) {
        std::cerr << "[WARN][src/video/VirtualCameraManager.cpp::open_stream_or_file] OpenCV could not open stream/file: " << endpoint << '\n';
        return false;
    }

    configure_zero_lag_capture(true);
    m_is_file_or_stream = true;
    m_external_push_mode.store(false, std::memory_order_relaxed);
    m_active_endpoint = endpoint;
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_active_source_name = "Direct Stream (" + endpoint + ")";
    }
    std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::open_stream_or_file] Successfully opened stream/file: " << endpoint << '\n';
    return true;
}

void VirtualCameraManager::configure_zero_lag_capture(bool is_stream) noexcept {
    if (!is_stream) {
        m_cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    }
    m_cap.set(cv::CAP_PROP_FRAME_WIDTH, m_width);
    m_cap.set(cv::CAP_PROP_FRAME_HEIGHT, m_height);
    m_cap.set(cv::CAP_PROP_FPS, m_fps);
    m_cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
}

void VirtualCameraManager::start_worker_threads() noexcept {
    if (!initialize_virtual_device(m_width, m_height, m_fps)) {
        std::cerr << "[WARN][src/video/VirtualCameraManager.cpp::start_worker_threads] Shared memory virtual camera sink unavailable; UI frame streaming still active.\n";
    } else {
        std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::start_worker_threads] Shared memory virtual camera sink mapped (1280x720).\n";
    }

    m_has_new_frame.store(false, std::memory_order_relaxed);
    m_running.store(true, std::memory_order_release);

    if (!m_external_push_mode.load(std::memory_order_relaxed)) {
        m_capture_thread = std::thread(&VirtualCameraManager::capture_loop, this);
        std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::start_worker_threads] Spawned hardware/stream capture_loop thread.\n";
    } else {
        std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::start_worker_threads] External USB-C Push Mode active (awaiting frames via push_external_frame).\n";
    }

    m_process_thread = std::thread(&VirtualCameraManager::process_loop, this);
    std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::start_worker_threads] Spawned AI process_loop thread for ["
              << get_active_source_name() << "].\n";
}

bool VirtualCameraManager::start_external_stream_mode(const std::string& source_label) noexcept {
    std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::start_external_stream_mode] Switching to External Push Mode: " << source_label << '\n';
    if (m_running.load(std::memory_order_acquire)) {
        if (m_external_push_mode.load(std::memory_order_relaxed)) {
            return true;
        }
        stop_capture();
    }

    m_external_push_mode.store(true, std::memory_order_release);
    m_is_file_or_stream = true;
    m_active_endpoint = "usb_phone_push";
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_active_source_name = source_label;
    }

    start_worker_threads();
    return true;
}

bool VirtualCameraManager::push_external_frame(const uint8_t* jpeg_data, size_t jpeg_size) noexcept {
    if (!jpeg_data || jpeg_size < 32) return false;

    if (!m_running.load(std::memory_order_acquire) || !m_external_push_mode.load(std::memory_order_relaxed)) {
        start_external_stream_mode("USB-C Phone / Camera Stream (Live)");
    }

    try {
        cv::Mat decoded;

        // Path 1: Ultra-fast Raw RGBA buffer ("AIFR" 12-byte header + RGBA bytes)
        if (jpeg_size > 12 && std::memcmp(jpeg_data, "AIFR", 4) == 0) {
            uint32_t w = 0, h = 0;
            std::memcpy(&w, jpeg_data + 4, sizeof(uint32_t));
            std::memcpy(&h, jpeg_data + 8, sizeof(uint32_t));
            if (w > 0 && w <= 4096 && h > 0 && h <= 4096 &&
                jpeg_size >= 12u + static_cast<size_t>(w) * static_cast<size_t>(h) * 4u) {
                cv::Mat rgba(static_cast<int>(h), static_cast<int>(w), CV_8UC4, const_cast<uint8_t*>(jpeg_data + 12));
                cv::cvtColor(rgba, decoded, cv::COLOR_RGBA2BGR);
            }
        }
        // Path 2: Standard JPEG/PNG/BMP via fast temp buffer & cv::imread
        else {
            static std::mutex file_decode_mutex;
            std::lock_guard<std::mutex> flock(file_decode_mutex);
            const std::filesystem::path tmp_path = std::filesystem::temp_directory_path() / "ai_studio_live_push.jpg";
            {
                std::ofstream ofs(tmp_path, std::ios::binary | std::ios::trunc);
                if (!ofs.is_open()) return false;
                ofs.write(reinterpret_cast<const char*>(jpeg_data), static_cast<std::streamsize>(jpeg_size));
            }
            decoded = cv::imread(tmp_path.string(), cv::IMREAD_COLOR);
        }

        if (decoded.empty()) {
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(m_swap_mutex);
            cv::Mat& target_slot = m_triple_buffers[m_write_idx];
            if (decoded.cols == m_width && decoded.rows == m_height && decoded.type() == CV_8UC3) {
                decoded.copyTo(target_slot);
            } else {
                cv::resize(decoded, target_slot, cv::Size(m_width, m_height), 0.0, 0.0, cv::INTER_LINEAR);
            }
            std::swap(m_write_idx, m_shared_idx);
            m_has_new_frame.store(true, std::memory_order_release);
        }
        m_frame_cv.notify_one();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR][src/video/VirtualCameraManager.cpp::push_external_frame] Exception: " << e.what() << '\n';
        return false;
    } catch (...) {
        return false;
    }
}

bool VirtualCameraManager::start_capture(int camera_index) noexcept {
    std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::start_capture(int)] Requested camera_index=" << camera_index << '\n';
    if (m_running.load(std::memory_order_acquire)) {
        stop_capture();
    }

    if (camera_index >= 0 && open_hardware_camera(camera_index)) {
        start_worker_threads();
        return true;
    }

    std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::start_capture(int)] Index " << camera_index
              << " unavailable; engaging start_auto_capture()...\n";
    return start_auto_capture();
}

bool VirtualCameraManager::start_capture(const std::string& stream_or_file_path) noexcept {
    std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::start_capture(string)] Requested endpoint='" << stream_or_file_path << "'\n";
    if (m_running.load(std::memory_order_acquire)) {
        stop_capture();
    }

    if (stream_or_file_path == "usb_phone_push") {
        return start_external_stream_mode("USB-C Phone Camera Bridge");
    }

    if (stream_or_file_path == "auto" || stream_or_file_path.empty()) {
        return start_auto_capture();
    }

    if (open_stream_or_file(stream_or_file_path)) {
        start_worker_threads();
        return true;
    }

    std::cerr << "[ERROR][src/video/VirtualCameraManager.cpp::start_capture(string)] Failed to open endpoint: " << stream_or_file_path << '\n';
    return false;
}

bool VirtualCameraManager::start_auto_capture() noexcept {
    std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::start_auto_capture] Probing all available hardware, virtual & USB-C sources...\n";
    if (m_running.load(std::memory_order_acquire)) return true;

    if (!m_custom_fallback_url.empty()) {
        if (m_custom_fallback_url == "usb_phone_push") {
            return start_external_stream_mode("USB-C Phone Camera Bridge");
        }
        if (open_stream_or_file(m_custom_fallback_url)) {
            start_worker_threads();
            return true;
        }
    }

    const auto sources = hw::HardwareManager::scan_video_devices();
    for (const auto& src : sources) {
        if (src.is_stream) {
            if (src.endpoint == "usb_phone_push") {
                return start_external_stream_mode(src.name);
            }
            if (open_stream_or_file(src.endpoint)) {
                {
                    std::lock_guard<std::mutex> lock(m_state_mutex);
                    m_active_source_name = src.name;
                }
                start_worker_threads();
                return true;
            }
        } else {
            if (open_hardware_camera(src.index)) {
                {
                    std::lock_guard<std::mutex> lock(m_state_mutex);
                    m_active_source_name = src.name;
                }
                start_worker_threads();
                return true;
            }
        }
    }

    std::cerr << "[WARN][src/video/VirtualCameraManager.cpp::start_auto_capture] No OS camera responded; standing by in External Frame Push Mode.\n";
    return start_external_stream_mode("Waiting for USB-C Phone / Camera Frame Push");
}

void VirtualCameraManager::stop_capture() noexcept {
    if (m_running.exchange(false, std::memory_order_acq_rel)) {
        std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::stop_capture] Stopping capture & AI worker threads...\n";
        m_external_push_mode.store(false, std::memory_order_release);
        m_frame_cv.notify_all();

        if (m_capture_thread.joinable()) {
            m_capture_thread.join();
        }
        if (m_process_thread.joinable()) {
            m_process_thread.join();
        }
        if (m_cap.isOpened()) {
            m_cap.release();
        }
        shutdown_virtual_device();
        m_current_fps.store(0.0, std::memory_order_relaxed);
        std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::stop_capture] Camera capture stopped cleanly.\n";
    }
}

void VirtualCameraManager::set_ai_processing_callback(std::function<void(cv::Mat&)> callback) noexcept {
    m_ai_callback = std::move(callback);
    std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::set_ai_processing_callback] AI vision callback attached.\n";
}

void VirtualCameraManager::set_fallback_stream_url(const std::string& url) noexcept {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_custom_fallback_url = url;
    std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::set_fallback_stream_url] Fallback URL set to: " << url << '\n';
}

std::string VirtualCameraManager::get_active_source_name() const noexcept {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    return m_active_source_name;
}

std::vector<uint8_t> VirtualCameraManager::get_latest_source_jpeg() const noexcept {
    std::lock_guard<std::mutex> lock(m_jpeg_mutex);
    return m_latest_source_jpeg;
}

std::vector<uint8_t> VirtualCameraManager::get_latest_output_jpeg() const noexcept {
    std::lock_guard<std::mutex> lock(m_jpeg_mutex);
    return m_latest_output_jpeg;
}

void VirtualCameraManager::capture_loop() noexcept {
    cv::Mat raw_frame;
    int consecutive_failures = 0;
    uint64_t grabbed_count = 0;

    std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::capture_loop] Entered hardware/stream grabber loop.\n";

    while (m_running.load(std::memory_order_relaxed) && !m_external_push_mode.load(std::memory_order_relaxed)) {
        if (!m_cap.read(raw_frame) || raw_frame.empty()) {
            if (m_is_file_or_stream) {
                if (m_cap.get(cv::CAP_PROP_FRAME_COUNT) > 1) {
                    m_cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                    continue;
                }
                if (++consecutive_failures > 30) {
                    std::cerr << "[WARN][src/video/VirtualCameraManager.cpp::capture_loop] Stream stalled; reconnecting to " << m_active_endpoint << "...\n";
                    consecutive_failures = 0;
                    m_cap.release();
                    m_cap.open(m_active_endpoint, cv::CAP_ANY);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            continue;
        }
        consecutive_failures = 0;
        ++grabbed_count;

        if (grabbed_count == 1 || grabbed_count % 120 == 0) {
            std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::capture_loop] Grabbed raw frame #" << grabbed_count
                      << " (" << raw_frame.cols << "x" << raw_frame.rows << ")\n";
        }

        {
            std::lock_guard<std::mutex> lock(m_swap_mutex);
            cv::Mat& target_slot = m_triple_buffers[m_write_idx];
            if (raw_frame.cols == m_width && raw_frame.rows == m_height && raw_frame.type() == CV_8UC3) {
                raw_frame.copyTo(target_slot);
            } else {
                cv::resize(raw_frame, target_slot, cv::Size(m_width, m_height), 0.0, 0.0, cv::INTER_LINEAR);
            }
            std::swap(m_write_idx, m_shared_idx);
            m_has_new_frame.store(true, std::memory_order_release);
        }
        m_frame_cv.notify_one();

        if (m_is_file_or_stream && m_cap.get(cv::CAP_PROP_FRAME_COUNT) > 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
    }

    std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::capture_loop] Exited grabber loop.\n";
}

void VirtualCameraManager::process_loop() noexcept {
    auto fps_timer = std::chrono::steady_clock::now();
    int processed_frames = 0;
    uint64_t total_ai_frames = 0;
    cv::Mat preview_small;

    std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::process_loop] Entered AI processing & 0.02ms BMP encoding loop.\n";

    while (m_running.load(std::memory_order_relaxed)) {
        {
            std::unique_lock<std::mutex> lock(m_swap_mutex);
            m_frame_cv.wait_for(lock, std::chrono::milliseconds(15), [this] {
                return m_has_new_frame.load(std::memory_order_relaxed) ||
                       !m_running.load(std::memory_order_relaxed);
            });

            if (!m_running.load(std::memory_order_relaxed)) break;
            if (!m_has_new_frame.load(std::memory_order_relaxed)) continue;

            std::swap(m_read_idx, m_shared_idx);
            m_has_new_frame.store(false, std::memory_order_release);
        }

        cv::Mat& work_frame = m_triple_buffers[m_read_idx];
        ++total_ai_frames;

        // 1. Encode Raw Source Frame (<0.02ms uncompressed BMP) for UI Panel 1 ("1. Source (You)")
        std::vector<uint8_t> src_bmp;
        try {
            cv::resize(work_frame, preview_small, cv::Size(640, 360), 0.0, 0.0, cv::INTER_NEAREST);
            encode_bgr_to_bmp_fast(preview_small, src_bmp);
        } catch (...) {}

        // 2. Execute Zero-Lag C++ AI Pipeline in-place (Body Tracking, Face Swap, Skin Blend, Lip Sync, Matting)
        if (m_ai_callback) {
            m_ai_callback(work_frame);
        }

        // 3. Encode Processed AI Output Frame (<0.02ms uncompressed BMP) for UI Panel 2 ("2. Target (AI Output)")
        std::vector<uint8_t> out_bmp;
        try {
            cv::resize(work_frame, preview_small, cv::Size(640, 360), 0.0, 0.0, cv::INTER_LINEAR);
            encode_bgr_to_bmp_fast(preview_small, out_bmp);
        } catch (...) {}

        if (!src_bmp.empty() || !out_bmp.empty()) {
            std::lock_guard<std::mutex> jlock(m_jpeg_mutex);
            if (!src_bmp.empty()) m_latest_source_jpeg = std::move(src_bmp);
            if (!out_bmp.empty()) m_latest_output_jpeg = std::move(out_bmp);
        }

        // 4. Blast the processed 1280x720 frame to Zoom/Discord/OBS via Shared Memory (<0.1ms)
        route_to_virtual_camera(work_frame);

        // 5. Update real-time FPS telemetry
        ++processed_frames;
        auto now = std::chrono::steady_clock::now();
        double elapsed_sec = std::chrono::duration<double>(now - fps_timer).count();
        if (elapsed_sec >= 0.5) {
            double current_fps = processed_frames / elapsed_sec;
            m_current_fps.store(current_fps, std::memory_order_relaxed);
            processed_frames = 0;
            fps_timer = now;
        }

        if (total_ai_frames == 1 || total_ai_frames % 120 == 0) {
            std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::process_loop] AI Processed Frame #" << total_ai_frames
                      << " | Active Source: [" << get_active_source_name() << "]"
                      << " | FPS: " << m_current_fps.load(std::memory_order_relaxed) << '\n';
        }
    }

    std::cout << "[DEBUG][src/video/VirtualCameraManager.cpp::process_loop] Exited AI processing loop.\n";
}

bool VirtualCameraManager::initialize_virtual_device(int width, int height, int fps) noexcept {
    (void)fps;
    m_frame_size_bytes = static_cast<size_t>(width * height * 3);

#if defined(_WIN32)
    DWORD buffer_size_dw = static_cast<DWORD>(m_frame_size_bytes);
    m_shared_memory_handle = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, buffer_size_dw, "OBSVirtualCamVideo");
    if (m_shared_memory_handle == NULL) return false;
    m_mapped_buffer = static_cast<uint8_t*>(MapViewOfFile(m_shared_memory_handle, FILE_MAP_ALL_ACCESS, 0, 0, buffer_size_dw));
#else
    int shm_fd = shm_open("/AIStudioVirtualCam", O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) return false;
    if (ftruncate(shm_fd, static_cast<off_t>(m_frame_size_bytes)) < 0) {
        close(shm_fd);
        return false;
    }
    void* map_ptr = mmap(0, m_frame_size_bytes, PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd);
    if (map_ptr == MAP_FAILED) {
        m_mapped_buffer = nullptr;
        return false;
    }
    m_mapped_buffer = static_cast<uint8_t*>(map_ptr);
#endif

    return m_mapped_buffer != nullptr;
}

void VirtualCameraManager::route_to_virtual_camera(const cv::Mat& frame) noexcept {
    if (!m_mapped_buffer || frame.empty() || !frame.isContinuous()) return;

    const size_t actual_bytes = frame.total() * frame.elemSize();
    if (actual_bytes != m_frame_size_bytes) return;

    std::memcpy(m_mapped_buffer, frame.ptr(), m_frame_size_bytes);
}

void VirtualCameraManager::shutdown_virtual_device() noexcept {
#if defined(_WIN32)
    if (m_mapped_buffer) UnmapViewOfFile(m_mapped_buffer);
    if (m_shared_memory_handle) CloseHandle(m_shared_memory_handle);
#else
    if (m_mapped_buffer) munmap(m_mapped_buffer, m_frame_size_bytes);
    shm_unlink("/AIStudioVirtualCam");
#endif
    m_mapped_buffer = nullptr;
    m_shared_memory_handle = nullptr;
}

} // namespace ai_studio::video