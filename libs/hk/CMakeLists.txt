# satlink::hk - housekeeping logic: XADC conversion, limits, watchdog supervisor, telemetry
# (Stage 3). Portable C11 without dynamic memory.
# @implements SRS-HKC-001

add_library(satlink_hk STATIC
    src/xadc.c
    src/limits.c
    src/supervisor.c
    src/telemetry.c
)
add_library(satlink::hk ALIAS satlink_hk)

target_include_directories(satlink_hk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(satlink_hk PUBLIC satlink::common satlink::can)
target_compile_features(satlink_hk PUBLIC c_std_11)
satlink_target_defaults(satlink_hk)
