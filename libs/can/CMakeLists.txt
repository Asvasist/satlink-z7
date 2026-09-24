# satlink::can - classic CAN frame type and the MCP2515 driver (Stage 3).
# Portable C11 without dynamic memory: the SPI transfer is supplied by the caller.
# @implements SRS-HKC-002

add_library(satlink_can STATIC
    src/mcp2515.c
)
add_library(satlink::can ALIAS satlink_can)

target_include_directories(satlink_can PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(satlink_can PUBLIC satlink::common)
target_compile_features(satlink_can PUBLIC c_std_11)
satlink_target_defaults(satlink_can)
