# satlink::hal - typed C++20 wrappers around the Linux driver char devices and the codec's I2C
# bus (Stage 2).
#
# The portable logic builds for both "host" (unit tests, devices mocked) and "linux" (real
# devices). The POSIX backends are linux-only: they are the only translation units allowed to
# call open()/ioctl()/write()/close().
# @implements SRS-HAL-001
# @implements SRS-HAL-002

add_library(satlink_hal STATIC
    src/ccsds_frame_accel.cpp
    src/spec_tap.cpp
    src/ssm2603.cpp
)

if(SATLINK_TARGET STREQUAL "linux")
    target_sources(satlink_hal PRIVATE
        src/posix_char_device.cpp
        src/i2c_dev_bus.cpp
        src/socket_can_bus.cpp
    )
endif()

add_library(satlink::hal ALIAS satlink_hal)

target_include_directories(satlink_hal
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../include/uapi
)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # No kernel headers on this host (for example a Windows build): supply the few definitions the
    # UAPI headers need, so the HAL and its unit tests still build and run.
    target_include_directories(satlink_hal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/compat)
endif()

target_link_libraries(satlink_hal PUBLIC satlink::hkc_proto)
target_compile_features(satlink_hal PUBLIC cxx_std_20)
satlink_target_defaults(satlink_hal)
