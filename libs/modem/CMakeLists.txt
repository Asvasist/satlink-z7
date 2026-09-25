# satlink::modem - software modem: CCSDS convolutional FEC, PSK mapping, RRC, framing, receiver
# and the channel model. Portable C11, no heap; runs on FreeRTOS (Core 1) and on the host.
# @implements SRS-MDM-001

add_library(satlink_modem STATIC
    src/conv.c
    src/mapper.c
    src/modcod.c
    src/prbs.c
    src/rrc.c
    src/channel.c
    src/frame.c
    src/receiver.c
)
add_library(satlink::modem ALIAS satlink_modem)
target_include_directories(satlink_modem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(satlink_modem PUBLIC satlink::common)
if(NOT MSVC)
    target_link_libraries(satlink_modem PUBLIC m)
endif()
target_compile_features(satlink_modem PUBLIC c_std_11)
satlink_target_defaults(satlink_modem)
