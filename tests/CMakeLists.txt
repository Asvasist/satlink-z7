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
