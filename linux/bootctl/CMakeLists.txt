# satlink::bootctl - A/B boot slot control (logic + satlink-bootctl on Linux targets).
# @implements SRS-BOOT-002

add_library(satlink_bootctl STATIC
    src/boot_control.cpp
    src/bootctl_cli.cpp
)
add_library(satlink::bootctl ALIAS satlink_bootctl)
target_include_directories(satlink_bootctl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(satlink_bootctl PUBLIC cxx_std_20)
satlink_target_defaults(satlink_bootctl)

if(SATLINK_TARGET STREQUAL "linux")
    include(GNUInstallDirs)
    add_executable(satlink-bootctl src/main.cpp)
    target_link_libraries(satlink-bootctl PRIVATE satlink::bootctl)
    satlink_target_defaults(satlink-bootctl)
    install(TARGETS satlink-bootctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
