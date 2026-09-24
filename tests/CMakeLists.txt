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

satlink_add_unity_test(test_mcp2515
    SOURCES unit/can/test_mcp2515.c
    LIBS satlink::can
)
satlink_add_unity_test(test_can_boot
    SOURCES unit/boot/test_can_boot.c
    LIBS satlink::boot
)
satlink_add_unity_test(test_app_header
    SOURCES unit/boot/test_app_header.c
    LIBS satlink::boot
)
satlink_add_unity_test(test_ab_slot
    SOURCES unit/boot/test_ab_slot.c
    LIBS satlink::boot
)
satlink_add_unity_test(test_hk
    SOURCES unit/hk/test_hk.c
    LIBS satlink::hk
)
