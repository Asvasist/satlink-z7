/**
 * @file test_hkc_link.cpp
 * @brief The Linux side of the housekeeping controller's CAN interface, run against the real
 *        bootloader receiver on a bus that loses, duplicates and corrupts frames.
 *
 * @verifies SRS-HKC-005
 * @verifies SRS-HKC-003
 */
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>

#include "satlink/boot/app_header.h"
#include "satlink/boot/can_boot.h"
#include "satlink/hal/hkc_link.hpp"

#include "fake_hkc_node.hpp"

namespace satlink::hal {
namespace {

using std::chrono::milliseconds;
using test::FakeHkcNode;

/// Produced by tools/hkc/mkapp.py from 16 zero bytes + "SatLink hkc app" (see test_app_header.c).
std::vector<std::uint8_t> GoldenApp()
{
    return {0x53, 0x4C, 0x41, 0x50, 0x01, 0x00, 0x10, 0x00, 0x1F, 0x00, 0x00,
            0x00, 0x4C, 0x6C, 0x00, 0x00, 0x53, 0x61, 0x74, 0x4C, 0x69, 0x6E,
            0x6B, 0x20, 0x68, 0x6B, 0x63, 0x20, 0x61, 0x70, 0x70};
}

std::vector<std::uint8_t> MakeImage(std::size_t size)
{
    std::vector<std::uint8_t> image(size);
    for (std::size_t i = 0; i < size; ++i)
    {
        image[i] = static_cast<std::uint8_t>((i * 7U) + 3U);
    }
    return image;
}

bool WasSent(const FakeHkcNode &node, std::uint8_t opcode)
{
    return std::any_of(node.sent.begin(), node.sent.end(), [opcode](const CanFrame &frame) {
        return frame.id == SATLINK_CANBOOT_ID_CMD && frame.dlc >= 1 && frame.data[0] == opcode;
    });
}

constexpr std::uint8_t kOpEnter = static_cast<std::uint8_t>(SATLINK_CANBOOT_OP_ENTER);
constexpr std::uint8_t kOpPing = static_cast<std::uint8_t>(SATLINK_CANBOOT_OP_PING);

// ---- Ping and ENTER ----------------------------------------------------------------------

TEST(HkcLinkTest, PingReportsTheBootloadersStateProtocolAndCapacity)
{
    FakeHkcNode node(FakeHkcNode::Mode::kBootloader, 2048U);
    HkcLink link(node);

    const std::optional<HkcNodeInfo> found = link.Ping();
    ASSERT_TRUE(found.has_value());
    const HkcNodeInfo info = found.value_or(HkcNodeInfo{});

    EXPECT_EQ(HkcReceiverState::kIdle, info.state);
    EXPECT_EQ(SATLINK_CANBOOT_VERSION, info.protocol_version);
    EXPECT_EQ(2048U, info.max_image_size);
}

TEST(HkcLinkTest, ARunningApplicationDoesNotAnswerPing)
{
    FakeHkcNode node(FakeHkcNode::Mode::kApplication);
    HkcLink link(node);

    EXPECT_FALSE(link.Ping().has_value());
    EXPECT_GE(node.waited, milliseconds(200)); // it waited for the answer
}

TEST(HkcLinkTest, EnterRestartsAnApplicationAndWaitsForTheBootloader)
{
    FakeHkcNode node(FakeHkcNode::Mode::kApplication);
    node.frames_until_ready = 6;
    HkcLink link(node);

    const std::optional<HkcNodeInfo> info = link.EnterBootloader();

    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(FakeHkcNode::Mode::kBootloader, node.mode());
    EXPECT_TRUE(WasSent(node, kOpEnter));
}

TEST(HkcLinkTest, EnterDoesNotDisturbABootloaderThatIsAlreadyRunning)
{
    FakeHkcNode node(FakeHkcNode::Mode::kBootloader);
    HkcLink link(node);

    ASSERT_TRUE(link.EnterBootloader().has_value());
    EXPECT_TRUE(WasSent(node, kOpPing));
    EXPECT_FALSE(WasSent(node, kOpEnter));
}

TEST(HkcLinkTest, EnterGivesUpWhenNothingAnswers)
{
    FakeHkcNode node(FakeHkcNode::Mode::kDead);
    HkcLink link(node);

    EXPECT_FALSE(link.EnterBootloader().has_value());
}

TEST(HkcLinkTest, EnterGivesUpWhenTheBootloaderNeverComesUp)
{
    FakeHkcNode node(FakeHkcNode::Mode::kApplication);
    node.frames_until_ready = 100000;
    HkcLinkOptions options;
    options.bootloader_wait = milliseconds(1000);
    HkcLink link(node, options);

    EXPECT_FALSE(link.EnterBootloader().has_value());
    EXPECT_EQ(FakeHkcNode::Mode::kStarting, node.mode());
}

TEST(HkcLinkTest, EnterSurvivesALossyBus)
{
    FakeHkcNode node(FakeHkcNode::Mode::kApplication);
    node.drop_percent = 30;
    HkcLinkOptions options;
    options.bootloader_wait = milliseconds(20000);
    HkcLink link(node, options);

    EXPECT_TRUE(link.EnterBootloader().has_value());
}

// ---- Upload ------------------------------------------------------------------------------

TEST(HkcLinkTest, UploadDeliversTheImageAndStartsIt)
{
    FakeHkcNode node;
    HkcLink link(node);
    std::vector<unsigned> progress;

    const HkcUploadResult result = link.Upload(
        GoldenApp(), true, [&progress](unsigned percent) { progress.push_back(percent); });

    EXPECT_TRUE(result.ok) << result.message;
    EXPECT_EQ(GoldenApp(), node.Received());
    EXPECT_EQ(SATLINK_CANBOOT_RX_VERIFIED, satlink_canboot_rx_state(&node.receiver()));
    EXPECT_TRUE(satlink_canboot_rx_boot_requested(&node.receiver()));

    std::size_t entry = 0;
    EXPECT_EQ(SATLINK_OK, satlink_app_validate(node.memory().data(), node.memory().size(), &entry));
    EXPECT_EQ(16U, entry);

    ASSERT_FALSE(progress.empty());
    EXPECT_EQ(0U, progress.front());
    EXPECT_EQ(100U, progress.back());
    for (std::size_t i = 1; i < progress.size(); ++i)
    {
        EXPECT_GT(progress[i], progress[i - 1]) << "progress is reported only when it changes";
    }
}

TEST(HkcLinkTest, UploadWithoutBootLeavesTheImageVerifiedButNotStarted)
{
    FakeHkcNode node;
    HkcLink link(node);

    const HkcUploadResult result = link.Upload(GoldenApp(), false);

    EXPECT_TRUE(result.ok) << result.message;
    EXPECT_EQ(SATLINK_CANBOOT_RX_VERIFIED, satlink_canboot_rx_state(&node.receiver()));
    EXPECT_FALSE(satlink_canboot_rx_boot_requested(&node.receiver()));
}

TEST(HkcLinkTest, UploadSurvivesALossyDuplicatingBus)
{
    for (const unsigned drop_percent : {10U, 25U, 40U})
    {
        FakeHkcNode node;
        node.drop_percent = drop_percent;
        node.duplicate = true;
        HkcLinkOptions options;
        options.max_retries = 60;
        HkcLink link(node, options);
        const std::vector<std::uint8_t> image = MakeImage(1500);

        const HkcUploadResult result = link.Upload(image, true);

        EXPECT_TRUE(result.ok) << "drop " << drop_percent << "%: " << result.message;
        EXPECT_EQ(image, node.Received()) << "drop " << drop_percent << "%";
    }
}

TEST(HkcLinkTest, UploadWorksWithASmallWindow)
{
    FakeHkcNode node;
    HkcLinkOptions options;
    options.window = 1;
    HkcLink link(node, options);
    const std::vector<std::uint8_t> image = MakeImage(300);

    EXPECT_TRUE(link.Upload(image, true).ok);
    EXPECT_EQ(image, node.Received());
}

TEST(HkcLinkTest, ACorruptedFrameIsCaughtByTheCrcAndBootIsRefused)
{
    FakeHkcNode node;
    node.corrupt_data_frame = 3;
    HkcLink link(node);

    const HkcUploadResult result = link.Upload(MakeImage(200), true);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(SATLINK_CANBOOT_ERR_CRC, result.node_error);
    EXPECT_NE(std::string::npos, result.message.find("CRC"));
    EXPECT_FALSE(satlink_canboot_rx_boot_requested(&node.receiver()));
}

TEST(HkcLinkTest, ANodeThatStopsAnsweringEndsTheUploadWithATimeout)
{
    FakeHkcNode node(FakeHkcNode::Mode::kDead);
    HkcLinkOptions options;
    options.max_retries = 4;
    HkcLink link(node, options);

    const HkcUploadResult result = link.Upload(MakeImage(50), true);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(SATLINK_CANBOOT_ERR_TIMEOUT, result.node_error);
    EXPECT_NE(std::string::npos, result.message.find("stopped answering"));
    EXPECT_GE(node.waited, milliseconds(4 * 200));
}

TEST(HkcLinkTest, AnImageThatIsTooLargeForTheNodeIsRefused)
{
    FakeHkcNode node(FakeHkcNode::Mode::kBootloader, 64U);
    HkcLink link(node);

    const HkcUploadResult result = link.Upload(MakeImage(100), true);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(SATLINK_CANBOOT_ERR_SIZE, result.node_error);
}

TEST(HkcLinkTest, EmptyAndOversizeImagesAreRejectedBeforeTheBusIsUsed)
{
    FakeHkcNode node;
    HkcLink link(node);

    const HkcUploadResult empty = link.Upload({}, true);
    EXPECT_FALSE(empty.ok);
    EXPECT_NE(std::string::npos, empty.message.find("empty or larger"));

    const std::vector<std::uint8_t> huge(SATLINK_CANBOOT_MAX_IMAGE + 1U, 0U);
    EXPECT_FALSE(link.Upload(huge, true).ok);

    EXPECT_TRUE(node.sent.empty());
}

/// A bus that is never quiet but never carries an answer.
class NoisyPort final : public CanPort
{
  public:
    void Send(const CanFrame & /*frame*/) override {}
    std::optional<CanFrame> Receive(std::chrono::milliseconds /*timeout*/) override
    {
        ++frames_delivered;
        CanFrame frame;
        frame.id = 0x123;
        frame.dlc = 2;
        return frame;
    }
    unsigned frames_delivered = 0;
};

TEST(HkcLinkTest, UploadDoesNotWaitForAQuietBusThatNeverComes)
{
    NoisyPort port;
    HkcLinkOptions options;
    options.max_retries = 3;
    HkcLink link(port, options);

    const HkcUploadResult result = link.Upload(MakeImage(20), true);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(SATLINK_CANBOOT_ERR_TIMEOUT, result.node_error);
    EXPECT_GT(port.frames_delivered, 0U);
}

TEST(HkcLinkTest, ProtocolErrorTextsAreDistinct)
{
    EXPECT_NE(DescribeCanBootError(SATLINK_CANBOOT_ERR_CRC),
              DescribeCanBootError(SATLINK_CANBOOT_ERR_SIZE));
    EXPECT_EQ("unknown error", DescribeCanBootError(0x1234));
}

// ---- Telemetry ---------------------------------------------------------------------------

satlink_hk_snapshot_t Snapshot()
{
    satlink_hk_snapshot_t snapshot{};
    snapshot.temp_mdegc = 41250;
    snapshot.vccint_mv = 1002;
    snapshot.vccaux_mv = 1801;
    snapshot.vccbram_mv = 999;
    snapshot.temp_level = SATLINK_HK_WARN;
    snapshot.vccint_level = SATLINK_HK_OK;
    snapshot.vccaux_level = SATLINK_HK_OK;
    snapshot.vccbram_level = SATLINK_HK_ALARM;
    snapshot.uptime_s = 12345;
    snapshot.watchdog_reset = true;
    return snapshot;
}

TEST(HkcLinkTest, TelemetryCollectsTheThreeFramesOfASnapshot)
{
    FakeHkcNode node(FakeHkcNode::Mode::kApplication);
    node.BroadcastTelemetry(Snapshot(), 9);
    HkcLink link(node);

    const std::optional<HkcTelemetry> found = link.ReadTelemetry(milliseconds(3000));
    ASSERT_TRUE(found.has_value());
    const HkcTelemetry telemetry = found.value_or(HkcTelemetry{});

    EXPECT_EQ(41250, telemetry.temperature_mdegc);
    EXPECT_EQ(1002U, telemetry.vccint_mv);
    EXPECT_EQ(1801U, telemetry.vccaux_mv);
    EXPECT_EQ(999U, telemetry.vccbram_mv);
    EXPECT_EQ(HkcLevel::kWarn, telemetry.temperature_level);
    EXPECT_EQ(HkcLevel::kOk, telemetry.vccint_level);
    EXPECT_EQ(HkcLevel::kAlarm, telemetry.vccbram_level);
    EXPECT_EQ(12345U, telemetry.uptime_s);
    EXPECT_TRUE(telemetry.watchdog_reset);
}

TEST(HkcLinkTest, TelemetrySkipsForeignFramesAndAnIncompleteOlderSnapshot)
{
    FakeHkcNode node(FakeHkcNode::Mode::kApplication);
    CanFrame foreign;
    foreign.id = 0x200;
    foreign.dlc = 8;
    node.Broadcast(foreign);

    satlink_hk_snapshot_t older = Snapshot();
    older.temp_mdegc = -5000;
    node.BroadcastTelemetry(older, 4, 0b011U); // the status frame of snapshot 4 was lost
    node.BroadcastTelemetry(Snapshot(), 5);
    HkcLink link(node);

    const std::optional<HkcTelemetry> found = link.ReadTelemetry(milliseconds(3000));
    ASSERT_TRUE(found.has_value());
    const HkcTelemetry telemetry = found.value_or(HkcTelemetry{});

    EXPECT_EQ(41250, telemetry.temperature_mdegc);
    EXPECT_EQ(12345U, telemetry.uptime_s);
}

TEST(HkcLinkTest, TelemetryTimesOutWithoutACompleteSnapshot)
{
    FakeHkcNode node(FakeHkcNode::Mode::kApplication);
    node.BroadcastTelemetry(Snapshot(), 1, 0b101U);
    HkcLink link(node);

    EXPECT_FALSE(link.ReadTelemetry(milliseconds(1500)).has_value());
    EXPECT_GE(node.waited, milliseconds(1500));
}

} // namespace
} // namespace satlink::hal
