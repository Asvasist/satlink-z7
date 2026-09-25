# satlink::amp - AMP inter-core communication: shared-memory rings (also compiled into the
# satlink_amp kernel module) and the Linux <-> FreeRTOS message set.
# @implements SRS-AMP-002

add_library(satlink_amp STATIC
    src/ipc_ring.c
    src/msg.c
)
add_library(satlink::amp ALIAS satlink_amp)
target_include_directories(satlink_amp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(satlink_amp PUBLIC satlink::common)
target_compile_features(satlink_amp PUBLIC c_std_11)
satlink_target_defaults(satlink_amp)
