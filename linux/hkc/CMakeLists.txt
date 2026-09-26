# satlink::hkc_client - Linux side of the housekeeping controller protocols, and the
# satlink-hkc tool on Linux targets. The logic takes a CanBus, so host tests run it against the
# bootloader and application logic directly.
# @implements SRS-HKC-006

add_library(satlink_hkc_client STATIC
    src/hkc_client.cpp
    src/hkc_cli.cpp
)
add_library(satlink::hkc_client ALIAS satlink_hkc_client)
target_include_directories(satlink_hkc_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(satlink_hkc_client PUBLIC satlink::hal satlink::hkc_proto satlink::regs)
target_compile_features(satlink_hkc_client PUBLIC cxx_std_20)
satlink_target_defaults(satlink_hkc_client)

if(SATLINK_TARGET STREQUAL "linux")
    include(GNUInstallDirs)
    add_executable(satlink-hkc src/main.cpp)
    target_link_libraries(satlink-hkc PRIVATE satlink::hkc_client)
    satlink_target_defaults(satlink-hkc)
    install(TARGETS satlink-hkc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
