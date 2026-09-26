# satlink::amp_client - Linux side of the AMP link: typed modem client, the simulated Core 1,
# and (Linux targets) the /dev/satlink-amp backend and the satlink-amp tool.
# @implements SRS-AMP-003

add_library(satlink_amp_client STATIC
    src/modem_client.cpp
    src/simulated_core1.cpp
)
if(SATLINK_TARGET STREQUAL "linux")
    target_sources(satlink_amp_client PRIVATE src/amp_device.cpp)
endif()
add_library(satlink::amp_client ALIAS satlink_amp_client)
target_include_directories(satlink_amp_client
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../include/uapi
)
target_link_libraries(satlink_amp_client PUBLIC satlink::amp satlink::modem_app)
target_compile_features(satlink_amp_client PUBLIC cxx_std_20)
satlink_target_defaults(satlink_amp_client)

if(SATLINK_TARGET STREQUAL "linux")
    include(GNUInstallDirs)
    add_executable(satlink-amp src/amp_tool.cpp)
    target_link_libraries(satlink-amp PRIVATE satlink::amp_client)
    satlink_target_defaults(satlink-amp)
    install(TARGETS satlink-amp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
