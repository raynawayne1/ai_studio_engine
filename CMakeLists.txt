cmake_minimum_required(VERSION 3.24)
project(AIStudioEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

# Gather all core source files
set(SOURCES
    src/main.cpp
    src/hw/HardwareManager.cpp
    src/ai/InferenceEngine.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE src)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

# Optional local ONNX Runtime path linking if available on the host machine
find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
    HINTS /usr/include /usr/local/include /opt/homebrew/include
)
if(ONNXRUNTIME_INCLUDE_DIR)
    target_include_directories(${PROJECT_NAME} PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
endif()

find_library(ONNXRUNTIME_LIBRARY NAMES onnxruntime
    HINTS /usr/lib /usr/local/lib /opt/homebrew/lib
)
if(ONNXRUNTIME_LIBRARY)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${ONNXRUNTIME_LIBRARY})
endif()

# Maximum Compiler Optimizations for Extreme Speed & Zero Lag
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /WX /O2 /fp:fast)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Werror -pedantic -O3 -ffast-math)
endif()