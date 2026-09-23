# SatLink-Z7 — one CMake build for all four targets (ADR-0001).
#
#   host   : unit tests, coverage, sanitizers, host tools        (default)
#   linux  : Cortex-A9 Core 0, embedded Linux user space          (arm-linux-gnueabihf / Yocto SDK)
#   rtos   : Cortex-A9 Core 1, FreeRTOS firmware                  (arm-none-eabi)
#   hkc    : MicroBlaze V housekeeping controller, bare metal     (riscv64-unknown-elf, rv32)
#
# Pick a target with a preset:  cmake --preset host-debug   (see CMakePresets.json)
# @implements SRS-BLD-001

cmake_minimum_required(VERSION 3.25)

project(satlink_z7
    VERSION 0.1.0
    DESCRIPTION "Software-defined satellite payload and link testbed on the Zynq-7020"
    HOMEPAGE_URL "https://github.com/Asvasist/satlink-z7"
    LANGUAGES C
)

if(NOT DEFINED SATLINK_TARGET)
    set(SATLINK_TARGET host)
endif()
set(SATLINK_TARGET "${SATLINK_TARGET}" CACHE STRING "Build target: host, linux, rtos or hkc")
set_property(CACHE SATLINK_TARGET PROPERTY STRINGS host linux rtos hkc)
if(NOT SATLINK_TARGET MATCHES "^(host|linux|rtos|hkc)$")
    message(FATAL_ERROR "SATLINK_TARGET must be host, linux, rtos or hkc (got '${SATLINK_TARGET}')")
endif()

# C++ is used on the host (tests, tools) and in Linux user space (HAL, daemon).
if(SATLINK_TARGET MATCHES "^(host|linux)$")
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(SATLINK_TARGET STREQUAL "host")
    set(_satlink_tests_default ON)
else()
    set(_satlink_tests_default OFF)
endif()
option(SATLINK_BUILD_TESTS "Build the unit tests (host only)" ${_satlink_tests_default})
option(SATLINK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(SATLINK_ENABLE_COVERAGE "Instrument for gcov/gcovr coverage (host, GCC/Clang)" OFF)
option(SATLINK_ENABLE_SANITIZERS "Build with AddressSanitizer + UBSan (host)" OFF)
option(SATLINK_ENABLE_CLANG_TIDY "Run clang-tidy on SatLink targets while compiling" OFF)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules)
include(SatlinkBuildOptions)

find_package(Git QUIET)
set(SATLINK_GIT_REVISION "unknown")
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --always --dirty --tags
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE SATLINK_GIT_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

message(STATUS "SatLink-Z7 ${PROJECT_VERSION} (${SATLINK_GIT_REVISION}) target=${SATLINK_TARGET} "
               "tests=${SATLINK_BUILD_TESTS}")

add_subdirectory(libs/common)
add_subdirectory(libs/regs)

if(SATLINK_TARGET MATCHES "^(host|linux)$")
    add_subdirectory(linux/hal)
endif()

if(SATLINK_BUILD_TESTS)
    if(NOT SATLINK_TARGET STREQUAL "host")
        message(FATAL_ERROR "SATLINK_BUILD_TESTS is only supported for SATLINK_TARGET=host")
    endif()
    enable_testing()
    include(SatlinkTesting)
    add_subdirectory(tests)
endif()
