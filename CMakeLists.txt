cmake_minimum_required(VERSION 3.16)
project(SPSCQueue
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "SPSC Queue"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(spsc_queue INTERFACE)
target_include_directories(spsc_queue INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wshadow -Wconversion -Wsign-conversion
)

add_executable(main src/main.cpp)
target_link_libraries(main PRIVATE spsc_queue)
