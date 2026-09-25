# satlink::common — portable C11 library used by every target (Linux, FreeRTOS, MicroBlaze V, host).
# @implements SRS-LIB-003

configure_file(include/satlink/common/version.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/include/satlink/common/version.h @ONLY)

add_library(satlink_common STATIC
    src/crc16_ccitt.c
    src/ccsds_randomizer.c
    src/crc32.c
)
add_library(satlink::common ALIAS satlink_common)

target_include_directories(satlink_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/include
)
target_compile_features(satlink_common PUBLIC c_std_11)
satlink_target_defaults(satlink_common)
