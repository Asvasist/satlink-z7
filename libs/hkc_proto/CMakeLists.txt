# satlink::hkc_proto - housekeeping CAN protocol, CAN bootloader protocol and HKC image format.
# Portable C11 shared by the MicroBlaze V firmware, the Linux tools and the host tests.
# @implements SRS-HKC-003

add_library(satlink_hkc_proto STATIC
    src/hk_proto.c
    src/canboot.c
    src/image.c
)
add_library(satlink::hkc_proto ALIAS satlink_hkc_proto)
target_include_directories(satlink_hkc_proto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(satlink_hkc_proto PUBLIC satlink::common)
target_compile_features(satlink_hkc_proto PUBLIC c_std_11)
satlink_target_defaults(satlink_hkc_proto)
