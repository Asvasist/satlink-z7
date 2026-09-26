# Host unit tests (ctest). Every test file carries @verifies tags for the traceability matrix.

satlink_add_unity_test(test_crc16_ccitt
    SOURCES unit/common/test_crc16_ccitt.c
    LIBS satlink::common
)
satlink_add_unity_test(test_ccsds_randomizer
    SOURCES unit/common/test_ccsds_randomizer.c
    LIBS satlink::common
)
satlink_add_unity_test(test_regs_c
    SOURCES unit/regs/test_regs_c.c
    LIBS satlink::regs
)
satlink_add_gtest(test_regdef
    SOURCES unit/regs/test_regdef.cpp
    LIBS satlink::regs
)
satlink_add_gtest(test_ccsds_frame_accel_hal
    SOURCES unit/hal/test_ccsds_frame_accel_hal.cpp
    LIBS satlink::hal
)
satlink_add_gtest(test_spec_tap_hal
    SOURCES unit/hal/test_spec_tap_hal.cpp
    LIBS satlink::hal
)
satlink_add_gtest(test_ssm2603_hal
    SOURCES unit/hal/test_ssm2603_hal.cpp
    LIBS satlink::hal
)
satlink_add_gtest(test_diag
    SOURCES unit/hal/test_diag.cpp
    LIBS satlink::diag
)

# Stage 3: housekeeping controller protocols and drivers.
satlink_add_unity_test(test_crc32
    SOURCES unit/common/test_crc32.c
    LIBS satlink::common
)
satlink_add_unity_test(test_hk_proto
    SOURCES unit/hkc/test_hk_proto.c
    LIBS satlink::hkc_proto
)
satlink_add_gtest(test_hkc_image
    SOURCES unit/hkc/test_hkc_image.cpp
    LIBS satlink::hkc_proto
)
satlink_add_gtest(test_canboot
    SOURCES unit/hkc/test_canboot.cpp
    LIBS satlink::hkc_proto
)
satlink_add_gtest(test_mcp2515
    SOURCES unit/hkc/test_mcp2515.cpp
    LIBS satlink::mcp2515
)
satlink_add_gtest(test_hkc_app
    SOURCES unit/hkc/test_hkc_app.cpp
    LIBS hkc::app_logic
)
satlink_add_gtest(test_hkc_client
    SOURCES unit/hkc/test_hkc_client.cpp
    LIBS satlink::hkc_client hkc::app_logic
)
satlink_add_gtest(test_boot_control
    SOURCES unit/boot/test_boot_control.cpp
    LIBS satlink::bootctl
)
target_compile_definitions(test_boot_control PRIVATE
    SATLINK_UBOOT_ENV_FILE="${PROJECT_SOURCE_DIR}/yocto/meta-satlink/recipes-bsp/u-boot/files/satlink-env.txt")

# Stage 4: software modem.
satlink_add_gtest(test_conv
    SOURCES unit/modem/test_conv.cpp
    LIBS satlink::modem
)
satlink_add_gtest(test_mapper_frame
    SOURCES unit/modem/test_mapper_frame.cpp
    LIBS satlink::modem
)
satlink_add_gtest(test_receiver
    SOURCES unit/modem/test_receiver.cpp
    LIBS satlink::modem
)
satlink_add_gtest(test_ipc_ring
    SOURCES unit/amp/test_ipc_ring.cpp
    LIBS satlink::amp
)
satlink_add_gtest(test_msg
    SOURCES unit/amp/test_msg.cpp
    LIBS satlink::amp
)
find_package(Threads REQUIRED)
target_link_libraries(test_ipc_ring PRIVATE Threads::Threads)
satlink_add_gtest(test_modem_app
    SOURCES unit/modem/test_modem_app.cpp
    LIBS satlink::modem_app
)
satlink_add_gtest(test_axi_dma
    SOURCES unit/rtos/test_axi_dma.cpp
    LIBS rtos::axi_dma
)
satlink_add_gtest(test_modem_client
    SOURCES unit/amp/test_modem_client.cpp
    LIBS satlink::amp_client
)

# Stage 5: adaptive link, payload management.
satlink_add_gtest(test_acm
    SOURCES unit/modem/test_acm.cpp
    LIBS satlink::acm satlink::amp_client
)
satlink_add_gtest(test_space_packet
    SOURCES unit/pus/test_space_packet.cpp
    LIBS satlink::pus
)
satlink_add_gtest(test_tm_frame
    SOURCES unit/pus/test_tm_frame.cpp
    LIBS satlink::pus
)
satlink_add_gtest(test_leo_pass
    SOURCES unit/payload/test_leo_pass.cpp
    LIBS satlink::payload
)
satlink_add_gtest(test_payload_manager
    SOURCES unit/payload/test_payload_manager.cpp
    LIBS satlink::payload
)

# Stage 6: instrument control.
satlink_add_gtest(test_scpi_parser
    SOURCES unit/scpi/test_scpi_parser.cpp
    LIBS satlink::scpi
)
satlink_add_gtest(test_scpi_instrument
    SOURCES unit/payload/test_scpi_instrument.cpp
    LIBS satlink::payload
)
satlink_add_gtest(test_mission_tm
    SOURCES unit/gs/test_mission_tm.cpp
    LIBS satlink::gs_core satlink::payload
)
satlink_add_gtest(test_posix_io
    SOURCES unit/payload/test_posix_io.cpp
    LIBS satlink::payload_io
)
