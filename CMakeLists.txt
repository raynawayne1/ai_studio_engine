cmake_minimum_required(VERSION 3.24)
project(AIStudioEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

# =========================================================================
# LOCAL THIRD-PARTY SDK PATH RESOLUTION (Zero Homebrew Dependencies)
# =========================================================================
set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# ONNX Runtime Local vs System Paths
set(ONNXRUNTIME_LOCAL_DIR "${THIRD_PARTY_DIR}/onnxruntime")
if(EXISTS "${ONNXRUNTIME_LOCAL_DIR}/include")
    set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_LOCAL_DIR}/include")
    if(APPLE)
        set(ONNXRUNTIME_LIBRARY "${ONNXRUNTIME_LOCAL_DIR}/lib/libonnxruntime.dylib")
    elseif(WIN32)
        set(ONNXRUNTIME_LIBRARY "${ONNXRUNTIME_LOCAL_DIR}/lib/onnxruntime.lib")
    else()
        set(ONNXRUNTIME_LIBRARY "${ONNXRUNTIME_LOCAL_DIR}/lib/libonnxruntime.so")
    endif()
else()
    set(ONNXRUNTIME_INCLUDE_DIR "/usr/include/onnxruntime")
    set(ONNXRUNTIME_LIBRARY "onnxruntime")
endif()

# OpenCV Local vs System Paths (macOS uses runtime dynamic symbol lookup for Python wheel bundle)
set(OPENCV_LOCAL_DIR "${THIRD_PARTY_DIR}/opencv")
if(EXISTS "${OPENCV_LOCAL_DIR}/include")
    set(OPENCV_INCLUDE_DIRS "${OPENCV_LOCAL_DIR}/include")
    if(APPLE)
        set(OPENCV_LIBS "") # Resolved via runtime dynamic loader
    elseif(WIN32)
        set(OPENCV_LIBS "")
    else()
        set(OPENCV_LIBS "")
    endif()
else()
    find_package(OpenCV QUIET)
    if(OpenCV_FOUND)
        set(OPENCV_INCLUDE_DIRS ${OpenCV_INCLUDE_DIRS})
        set(OPENCV_LIBS ${OpenCV_LIBS})
    else()
        message(FATAL_ERROR "OpenCV development headers not found locally or in system paths.")
    endif()
endif()

# =========================================================================
# DYNAMIC ADAPTIVE SOURCE GATHERING (Prevents build failure on missing modules)
# =========================================================================
set(CANDIDATE_SOURCES
    src/main.cpp
    src/hw/HardwareManager.cpp
    src/ai/InferenceEngine.cpp
    src/ai/ModelManager.cpp
    src/ai/CloudSyncManager.cpp
    src/ai/VoiceUploadManager.cpp
    src/ai/VoiceLibraryManager.cpp
    src/ai/VoicePreviewManager.cpp
    src/ai/VoiceInferenceEngine.cpp
    src/ai/FaceSwapEngine.cpp
    src/ai/BodyTrackerEngine.cpp
    src/ai/BackgroundMattingEngine.cpp
    src/audio/AudioCaptureEngine.cpp
    src/audio/VirtualRoutingManager.cpp
    src/audio/ipc/EngineIPCServer.cpp 
    src/video/VirtualCameraManager.cpp
)

set(VALID_SOURCES "")
foreach(src_file ${CANDIDATE_SOURCES})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${src_file}")
        list(APPEND VALID_SOURCES ${src_file})
    else()
        message(STATUS "[AIStudioEngine Optimization] Note: Pending module file not found yet (skipped): ${src_file}")
    endif()
endforeach()

if(NOT VALID_SOURCES)
    message(FATAL_ERROR "No source files found to compile!")
endif()

add_executable(${PROJECT_NAME} ${VALID_SOURCES})

# Include local headers normally, mark third-party includes as SYSTEM to suppress external warnings
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE 
    ${OPENCV_INCLUDE_DIRS}
    ${ONNXRUNTIME_INCLUDE_DIR}
)

# Link local and platform libraries
target_link_libraries(${PROJECT_NAME} PRIVATE 
    Threads::Threads 
    ${OPENCV_LIBS}
    ${ONNXRUNTIME_LIBRARY}
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()

if(APPLE)
    # Link native macOS hardware acceleration frameworks & enable dynamic symbol lookup for Python OpenCV wheel bundle
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework CoreML" "-framework Metal" "-framework Foundation")
    target_link_options(${PROJECT_NAME} PRIVATE "-Wl,-undefined,dynamic_lookup")
endif()

# =========================================================================
# MAXIMUM HARDWARE-NATIVE COMPILER OPTIMIZATIONS FOR ZERO LAG
# =========================================================================
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /WX /O2 /fp:fast /Oi /Gy /DNDEBUG)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE 
        -Wall 
        -Wextra 
        -Werror 
        -pedantic 
        -Wno-c11-extensions 
        -Wno-deprecated-anon-enum-enum-conversion
        -O3 
        -ffast-math 
        -march=native 
        -mtune=native 
        -funroll-loops 
        -ftree-vectorize 
        -fomit-frame-pointer 
        -DNDEBUG
    )
endif()

# Enable Interprocedural Optimization (LTO) for cross-module function inlining and max performance
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()