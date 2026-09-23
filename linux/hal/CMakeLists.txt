# satlink::hal — typed C++20 wrapper around the Linux driver char devices (Stage 2).
#
# The portable logic (ccsds_frame_accel.*) builds for both "host" (unit tests, CharDeviceIo
# mocked) and "linux" (real devices) - see ADR reasoning in char_device_io.hpp. The POSIX
# backend is linux-only: it is the one translation unit allowed to call open()/ioctl()/close().
# @implements SRS-HAL-001
# @implements SRS-HAL-002

add_library(satlink_hal STATIC
    src/ccsds_frame_accel.cpp
)

if(SATLINK_TARGET STREQUAL "linux")
    target_sources(satlink_hal PRIVATE src/posix_char_device.cpp)
endif()

add_library(satlink::hal ALIAS satlink_hal)

target_include_directories(satlink_hal
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../include/uapi
)
target_compile_features(satlink_hal PUBLIC cxx_std_20)
satlink_target_defaults(satlink_hal)
