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
