# satlink::pus - CCSDS Space Packets with PUS-C headers and TM Transfer Frames with virtual
# channels. Portable C++20 (payload manager on Linux, ground station on Windows and Linux).
# @implements SRS-PUS-001
add_library(satlink_pus STATIC
    src/space_packet.cpp
    src/tm_frame.cpp
)
add_library(satlink::pus ALIAS satlink_pus)
target_include_directories(satlink_pus PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(satlink_pus PUBLIC satlink::common)
target_compile_features(satlink_pus PUBLIC cxx_std_20)
satlink_target_defaults(satlink_pus)
