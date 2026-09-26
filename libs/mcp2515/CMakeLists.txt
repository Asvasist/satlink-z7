# satlink::mcp2515 - portable MCP2515 CAN controller driver (MicroBlaze V firmware, host tests).
# @implements SRS-HKC-001

add_library(satlink_mcp2515 STATIC src/mcp2515.c)
add_library(satlink::mcp2515 ALIAS satlink_mcp2515)
target_include_directories(satlink_mcp2515 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(satlink_mcp2515 PUBLIC satlink::hkc_proto)
target_compile_features(satlink_mcp2515 PUBLIC c_std_11)
satlink_target_defaults(satlink_mcp2515)
