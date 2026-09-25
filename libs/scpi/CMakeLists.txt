# satlink::scpi - SCPI / IEEE 488.2 command parser with error queue and status bytes.
# Portable C++20, no I/O: the payload daemon feeds it lines from TCP port 5025.
# @implements SRS-SCPI-001
add_library(satlink_scpi STATIC
    src/parser.cpp
)
add_library(satlink::scpi ALIAS satlink_scpi)
target_include_directories(satlink_scpi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(satlink_scpi PUBLIC cxx_std_20)
satlink_target_defaults(satlink_scpi)
