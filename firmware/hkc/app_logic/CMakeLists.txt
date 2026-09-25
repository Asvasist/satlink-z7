# hkc::app_logic - housekeeping application logic without register access (firmware + host tests).
# @implements SRS-HKC-005

add_library(hkc_app_logic STATIC src/hkc_app.c)
add_library(hkc::app_logic ALIAS hkc_app_logic)
target_include_directories(hkc_app_logic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(hkc_app_logic PUBLIC satlink::hkc_proto)
satlink_target_defaults(hkc_app_logic)
