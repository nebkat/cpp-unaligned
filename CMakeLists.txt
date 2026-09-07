cmake_minimum_required(VERSION 3.21)

project(unaligned CXX)

option(UNALIGNED_TEST_SANITIZE "Build the tests with AddressSanitizer and UndefinedBehaviorSanitizer (AddressSanitizer only on MSVC)" ON)
option(UNALIGNED_TEST_COVERAGE "Build the tests with gcov instrumentation (GCC and Clang)" OFF)

# Reading and writing values that are not aligned for their type, without ever forming a
# pointer or reference to an object that is not there.
add_library(unaligned INTERFACE)
add_library(unaligned::unaligned ALIAS unaligned)

target_include_directories(unaligned INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
        $<INSTALL_INTERFACE:include>)
target_compile_features(unaligned INTERFACE cxx_std_23)

if(PROJECT_IS_TOP_LEVEL)
    enable_testing()
    set(BUILD_TESTING ON)
endif()

if(BUILD_TESTING)
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
