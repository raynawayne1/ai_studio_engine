cmake_minimum_required(VERSION 3.24)
project(AIStudioEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)
include(CheckCXXCompilerFlag)

# =========================================================================
# LOCAL THIRD-PARTY SDK PATH RESOLUTION (macOS, Windows, Linux CI)
# =========================================================================
set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# 1. ONNX Runtime Local vs System Paths
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

# 2. OpenCV Platform-Specific Resolution
if(APPLE)
    set(OPENCV_INCLUDE_DIRS "${THIRD_PARTY_DIR}/opencv/include")
    set(OPENCV_LIBS "") # Resolved via runtime dynamic loader for Python wheel bundle
elseif(WIN32)
    set(OPENCV_LOCAL_DIR "${THIRD_PARTY_DIR}/opencv")
    set(OPENCV_INCLUDE_DIRS "${OPENCV_LOCAL_DIR}/include")
    if(EXISTS "${OPENCV_LOCAL_DIR}/lib/opencv_world490.lib")
        set(OPENCV_LIBS "${OPENCV_LOCAL_DIR}/lib/opencv_world490.lib")
    else()
        file(GLOB OPENCV_LIBS "${OPENCV_LOCAL_DIR}/lib/*.lib")
    endif()
else()
    # Linux (Ubuntu) - Link only the 4 fast core OpenCV libraries
    set(OPENCV_INCLUDE_DIRS "${THIRD_PARTY_DIR}/opencv/include" "/usr/include/opencv4")
    set(OPENCV_LIBS opencv_core opencv_imgproc opencv_imgcodecs opencv_videoio)
endif()

# =========================================================================
# AUTOMATIC RECURSIVE SOURCE GATHERING
# =========================================================================
file(GLOB_RECURSE VALID_SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
)

if(NOT VALID_SOURCES)
    message(FATAL_ERROR "No source files found to compile!")
endif()

add_executable(${PROJECT_NAME} ${VALID_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE src)
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE 
    ${OPENCV_INCLUDE_DIRS}
    ${ONNXRUNTIME_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE 
    Threads::Threads 
    ${OPENCV_LIBS}
    ${ONNXRUNTIME_LIBRARY}
)

if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
    
    if(EXISTS "${ONNXRUNTIME_LOCAL_DIR}/bin/onnxruntime.dll")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRUNTIME_LOCAL_DIR}/bin/onnxruntime.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        )
    endif()

    if(EXISTS "${OPENCV_LOCAL_DIR}/bin")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${OPENCV_LOCAL_DIR}/bin"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        )
    endif()
elseif(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "-framework CoreML"
        "-framework Metal"
        "-framework Foundation"
        "-framework Accelerate"
        ${CMAKE_DL_LIBS}
    )
    target_link_options(${PROJECT_NAME} PRIVATE "-Wl,-undefined,dynamic_lookup")
elseif(UNIX)
    target_link_libraries(${PROJECT_NAME} PRIVATE rt ${CMAKE_DL_LIBS})
endif()

# =========================================================================
# MAXIMUM HARDWARE-NATIVE COMPILER OPTIMIZATIONS FOR ZERO LAG (<15ms)
# =========================================================================
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W4 /WX /O2 /Ob3 /fp:fast /Oi /Ot /Gy /GL /DNDEBUG /D_CRT_SECURE_NO_WARNINGS
    )
    target_link_options(${PROJECT_NAME} PRIVATE /LTCG)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE 
        -Wall 
        -Wextra 
        -Werror 
        -pedantic 
        -Wno-unknown-pragmas
        -O3 
        -ffast-math 
        -funroll-loops 
        -ftree-vectorize 
        -fomit-frame-pointer 
        -DNDEBUG
    )
    check_cxx_compiler_flag("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
    if(COMPILER_SUPPORTS_MARCH_NATIVE)
        target_compile_options(${PROJECT_NAME} PRIVATE -march=native -mtune=native)
    elseif(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        target_compile_options(${PROJECT_NAME} PRIVATE -mcpu=apple-m1)
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${PROJECT_NAME} PRIVATE 
            -Wno-c11-extensions 
            -Wno-deprecated-anon-enum-enum-conversion
        )
    endif()
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()