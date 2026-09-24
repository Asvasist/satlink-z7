# satlink::boot - CAN upload protocol, application image header, A/B slot model (Stage 3).
# Portable C11 without dynamic memory: runs in the MicroBlaze V bootloader, in the Linux uploader
# and in the host unit tests.
# @implements SRS-HKC-003
# @implements SRS-BOOT-003

add_library(satlink_boot STATIC
    src/can_boot.c
    src/app_header.c
    src/ab_slot.c
)
add_library(satlink::boot ALIAS satlink_boot)

target_include_directories(satlink_boot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(satlink_boot PUBLIC satlink::common satlink::can)
target_compile_features(satlink_boot PUBLIC c_std_11)
satlink_target_defaults(satlink_boot)
