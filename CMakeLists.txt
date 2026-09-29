cmake_minimum_required(VERSION 3.24)
project(AIStudioEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find system threading libraries (CRUCIAL for Linux CI compatibility)
find_package(Threads REQUIRED)

# Gather all source files
set(SOURCES
    src/main.cpp
    src/hw/HardwareManager.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

# Ensure the compiler can find our header files starting from the src/ folder
target_include_directories(${PROJECT_NAME} PRIVATE src)

# Link the threading library securely
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

# Cross-platform compiler warning flags (Treat warnings as errors)
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /WX)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Werror -pedantic)
endif()