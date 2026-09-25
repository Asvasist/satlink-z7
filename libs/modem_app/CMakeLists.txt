# satlink::modem_app - the Core 1 modem application logic (FreeRTOS firmware, payload simulator,
# host tests).
# @implements SRS-MDM-007

add_library(satlink_modem_app STATIC src/modem_app.c)
add_library(satlink::modem_app ALIAS satlink_modem_app)
target_include_directories(satlink_modem_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(satlink_modem_app PUBLIC satlink::modem satlink::amp)
target_compile_features(satlink_modem_app PUBLIC c_std_11)
satlink_target_defaults(satlink_modem_app)
