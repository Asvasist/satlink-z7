# satlink::acm - adaptive coding and modulation controller (firmware, simulator, tests).
# @implements SRS-ACM-001
add_library(satlink_acm STATIC src/acm.c)
add_library(satlink::acm ALIAS satlink_acm)
target_include_directories(satlink_acm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(satlink_acm PUBLIC satlink::modem)
target_compile_features(satlink_acm PUBLIC c_std_11)
satlink_target_defaults(satlink_acm)
