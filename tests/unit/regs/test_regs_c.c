/**
 * @file test_regs_c.c
 * @brief Checks that the generated C register headers compile cleanly as C11 and agree
 *        with the ICD (the YAML files in icd/).
 *
 * @verifies SRS-ICD-001
 * @verifies SRS-ICD-003
 * @verifies SRS-ICD-004
 */
#include <stdint.h>

#include "satlink/regdef.h"
#include "satlink/regs/address_map.h"
#include "satlink/regs/ccsds_frame_accel.h"
#include "satlink/regs/payload_ctrl.h"
#include "satlink/regs/spec_tap.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_payload_ctrl_layout_matches_icd(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x43C00000U, SATLINK_PS_PAYLOAD_CTRL_BASE);
    TEST_ASSERT_EQUAL_UINT32(63U, SATLINK_PS_PAYLOAD_CTRL_IRQ_STATUS);
    TEST_ASSERT_EQUAL_HEX32(0x20U, PAYLOAD_CTRL_ATTEN_OFFSET);
    TEST_ASSERT_EQUAL_HEX32(0x7FFFU, PAYLOAD_CTRL_ATTEN_RESET);
    TEST_ASSERT_EQUAL_HEX32(0x40000000U, PAYLOAD_CTRL_TX_PINC_RESET);
}

static void test_field_helpers_round_trip(void)
{
    uint32_t reg = 0xFFFFFFFFU;
    reg = satlink_field_set(reg, PAYLOAD_CTRL_CTRL_DIG_LOOPBACK_MASK,
                            PAYLOAD_CTRL_CTRL_DIG_LOOPBACK_SHIFT, 0U);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFDU, reg);
    TEST_ASSERT_EQUAL_UINT32(0U, satlink_field_get(reg, PAYLOAD_CTRL_CTRL_DIG_LOOPBACK_MASK,
                                                   PAYLOAD_CTRL_CTRL_DIG_LOOPBACK_SHIFT));
    TEST_ASSERT_EQUAL_UINT32(1U, satlink_field_get(reg, PAYLOAD_CTRL_CTRL_RX_EN_MASK,
                                                   PAYLOAD_CTRL_CTRL_RX_EN_SHIFT));
}

static void test_field_set_drops_excess_bits(void)
{
    const uint32_t reg = satlink_field_set(0U, SPEC_TAP_STATUS_FRAME_DROPPED_MASK,
                                           SPEC_TAP_STATUS_FRAME_DROPPED_SHIFT, 0x3U);
    TEST_ASSERT_EQUAL_HEX32(SPEC_TAP_STATUS_FRAME_DROPPED_MASK, reg);
}

static void test_ddr_partitions_are_contiguous_and_fill_ddr(void)
{
    TEST_ASSERT_EQUAL_HEX32(SATLINK_DDR_LINUX_BASE + SATLINK_DDR_LINUX_SIZE,
                            SATLINK_DDR_RTOS_FW_BASE);
    TEST_ASSERT_EQUAL_HEX32(SATLINK_DDR_RTOS_FW_BASE + SATLINK_DDR_RTOS_FW_SIZE,
                            SATLINK_DDR_RPMSG_SHM_BASE);
    TEST_ASSERT_EQUAL_HEX32(SATLINK_DDR_RPMSG_SHM_BASE + SATLINK_DDR_RPMSG_SHM_SIZE,
                            SATLINK_DDR_MODEM_DMA_BASE);
    TEST_ASSERT_EQUAL_HEX32(SATLINK_DDR_MODEM_DMA_BASE + SATLINK_DDR_MODEM_DMA_SIZE,
                            SATLINK_DDR_RESERVED_BASE);
    TEST_ASSERT_EQUAL_HEX32(SATLINK_PS_DDR_SIZE,
                            SATLINK_DDR_RESERVED_BASE + SATLINK_DDR_RESERVED_SIZE);
}

static void test_mcu_bootloader_and_app_share_the_lmb(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x00000000U, SATLINK_HKC_LMB_BOOTLOADER_BASE);
    TEST_ASSERT_EQUAL_HEX32(SATLINK_HKC_LMB_BOOTLOADER_SIZE, SATLINK_HKC_LMB_APP_BASE);
    TEST_ASSERT_EQUAL_HEX32(0x00020000U, SATLINK_HKC_LMB_APP_BASE + SATLINK_HKC_LMB_APP_SIZE);
}

static void test_frame_accel_crc_resets_to_crc_init(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xFFFFU, CCSDS_FRAME_ACCEL_CRC_RESET);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_payload_ctrl_layout_matches_icd);
    RUN_TEST(test_field_helpers_round_trip);
    RUN_TEST(test_field_set_drops_excess_bits);
    RUN_TEST(test_ddr_partitions_are_contiguous_and_fill_ddr);
    RUN_TEST(test_mcu_bootloader_and_app_share_the_lmb);
    RUN_TEST(test_frame_accel_crc_resets_to_crc_init);
    return UNITY_END();
}
