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

option(BUILD_TESTS "Build tests" ON)
option(ENABLE_TSAN "Enable Thread Sanitizer" OFF)

if(ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -g)
    add_link_options(-fsanitize=thread)
endif()

if(BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_executable(test_basic tests/test_basic.cpp)
    target_link_libraries(test_basic PRIVATE spsc_queue GTest::gtest_main)

    add_executable(test_stress tests/test_stress.cpp)
    target_link_libraries(test_stress PRIVATE spsc_queue GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(test_basic)
    gtest_discover_tests(test_stress)
endif()
