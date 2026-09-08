cmake_minimum_required(VERSION 3.21)

project(unaligned CXX)

# PROJECT_IS_TOP_LEVEL is true of any project() call, including one made inside a build that
# merely includes us - an ESP-IDF component, say. Comparing the source directories asks the
# question actually meant: is this the build being configured, or somebody's dependency?
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(UNALIGNED_IS_MAIN_BUILD ON)
else()
    set(UNALIGNED_IS_MAIN_BUILD OFF)
endif()

option(UNALIGNED_BUILD_TESTS "Build the tests" ${UNALIGNED_IS_MAIN_BUILD})
option(UNALIGNED_TEST_SANITIZE "Build the tests with AddressSanitizer and UndefinedBehaviorSanitizer (AddressSanitizer only on MSVC)" ON)
option(UNALIGNED_TEST_COVERAGE "Build the tests with gcov instrumentation (GCC and Clang)" OFF)

# Reading and writing values that are not aligned for their type, without ever forming a
# pointer or reference to an object that is not there.
add_library(unaligned INTERFACE)
add_library(unaligned::unaligned ALIAS unaligned)

target_include_directories(unaligned INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
        $<INSTALL_INTERFACE:include>)
# Declaring the minimum puts a -std flag on every consumer's command line, which is right for a
# CMake-managed build and wrong for one that sets the standard itself with raw flags - ESP-IDF
# passes -std=gnu++26, and whichever lands last wins. Turn this off there and set your own.
option(UNALIGNED_SET_CXX_STANDARD "Declare the C++23 minimum on consumers" ON)
if(UNALIGNED_SET_CXX_STANDARD)
    target_compile_features(unaligned INTERFACE cxx_std_23)
endif()

if(UNALIGNED_BUILD_TESTS)
    enable_testing()
    set(BUILD_TESTING ON)
endif()

if(UNALIGNED_BUILD_TESTS)
    # Boost.UT: a single header, macro-free test framework. Test-only; consumers never see it.
    include(FetchContent)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    FetchContent_Declare(ut
            URL https://github.com/boost-ext/ut/archive/refs/tags/v2.3.1.tar.gz
            URL_HASH SHA256=e51bf1873705819730c3f9d2d397268d1c26128565478e2e65b7d0abb45ea9b1)
    FetchContent_MakeAvailable(ut)

    # One executable per test file, registered with ctest under the file's stem.
    function(unaligned_add_test name)
        set(target ${name}_test)
        add_executable(${target} test/${target}.cpp)
        target_link_libraries(${target} PRIVATE unaligned Boost::ut)
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/test")
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /permissive-)
            if(UNALIGNED_TEST_SANITIZE)
                # MSVC has AddressSanitizer only. It is incompatible with /RTC1, so build Release.
                target_compile_options(${target} PRIVATE /fsanitize=address)
            endif()
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra)
            if(UNALIGNED_TEST_SANITIZE)
                target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
                target_link_options(${target} PRIVATE -fsanitize=address,undefined)
            endif()
            if(UNALIGNED_TEST_COVERAGE)
                target_compile_options(${target} PRIVATE --coverage)
                target_link_options(${target} PRIVATE --coverage)
            endif()
        endif()
        add_test(NAME ${name} COMMAND ${target})
    endfunction()

    unaligned_add_test(unaligned)
    unaligned_add_test(unaligned_ptr)
endif()
