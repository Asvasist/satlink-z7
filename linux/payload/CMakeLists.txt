# satlink::payload - the payload manager (PUS services, TM downlink through the modem, IP over
# the link, LEO pass emulation) and the satlink-payloadd daemon.
# @implements SRS-PLM-001

add_library(satlink_payload STATIC
    src/leo_pass.cpp
    src/payload_manager.cpp
)
add_library(satlink::payload ALIAS satlink_payload)
target_include_directories(satlink_payload PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(satlink_payload PUBLIC satlink::pus satlink::amp_client satlink::modem)
target_compile_features(satlink_payload PUBLIC cxx_std_20)
satlink_target_defaults(satlink_payload)

# The daemon and the Linux I/O classes: on the target, and on Linux hosts for the simulator and
# the hardware-in-the-loop tests.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(satlink_payload_io STATIC src/posix_io.cpp)
    add_library(satlink::payload_io ALIAS satlink_payload_io)
    target_link_libraries(satlink_payload_io PUBLIC satlink::payload satlink::hal)
    satlink_target_defaults(satlink_payload_io)
    if(NOT SATLINK_TARGET STREQUAL "linux")
        # On a host the HAL has no SocketCAN backend; compile it here for the daemon.
        target_sources(satlink_payload_io PRIVATE ${PROJECT_SOURCE_DIR}/linux/hal/src/socket_can_bus.cpp)
    endif()

    include(GNUInstallDirs)
    add_executable(satlink-payloadd src/main.cpp)
    target_link_libraries(satlink-payloadd PRIVATE satlink::payload_io)
    if(SATLINK_TARGET STREQUAL "linux")
        target_compile_definitions(satlink-payloadd PRIVATE SATLINK_HAVE_AMP_DEVICE)
    endif()
    satlink_target_defaults(satlink-payloadd)
    install(TARGETS satlink-payloadd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
