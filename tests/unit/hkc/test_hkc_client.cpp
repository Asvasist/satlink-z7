/**
 * @file test_hkc_client.cpp
 * @brief satlink-hkc end to end on the host: the Linux client and CLI talk to the real
 *        bootloader session and housekeeping application logic through a simulated CAN bus
 *        that can lose frames.
 *
 * @verifies SRS-HKC-006
 * @verifies SRS-HKC-002
 */
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <deque>
#include <gtest/gtest.h>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "satlink/hkc/canboot.h"
#include "satlink/hkc/hkc_cli.hpp"
#include "satlink/hkc/hkc_client.hpp"

#include "hkc/hkc_app.h"
#include "image_builder.hpp"

namespace satlink::test {
namespace {

/// The housekeeping controller as seen from CAN: bootloader or application.
class VirtualHkc : public hal::CanBus
{
  public:
    VirtualHkc() : region_(kAppSize, 0xFF)
    {
        satlink_canboot_target_init(&boot_, region_.data(), kAppBase, kAppSize, 1, 0);
        const hkc_app_hw_t hw{this, &Sensor, &Alarm, &Switches, &Rgb, &AppSend, &EnterBoot};
        hkc_app_init(&app_, &hw, SATLINK_HK_RESET_POWER_ON);
    }

    int Send(const satlink_can_frame_t &frame) override
    {
        ++sent;
        if (drop_requests_every != 0 && (sent % drop_requests_every) == 0)
        {
            return 0; // lost on the bus
        }
        satlink_can_frame_t response{};
        if (in_app)
        {
            hkc_app_on_frame(&app_, &frame, now_ms);
            hkc_app_poll(&app_, now_ms);
        }
        else if (satlink_canboot_target_handle(&boot_, &frame, &response))
        {
            ++responses;
            if (drop_responses_every != 0 && (responses % drop_responses_every) == 0)
            {
                return 0; // response lost; the client must retry
            }
            rx_.push_back(response);
            if (boot_.boot_requested)
            {
                in_app = true;
            }
        }
        return 0;
    }

    int Receive(satlink_can_frame_t &frame, std::chrono::milliseconds) override
    {
        if (rx_.empty())
        {
            return -ETIMEDOUT;
        }
        frame = rx_.front();
        rx_.pop_front();
        return 0;
    }

    void Queue(const satlink_can_frame_t &frame)
    {
        rx_.push_back(frame);
    }

    std::vector<std::uint8_t> region_;
    satlink_canboot_target_t boot_{};
    hkc_app_t app_{};
    bool in_app = false;
    std::uint32_t now_ms = 0;
    int sent = 0;
    int responses = 0;
    int drop_requests_every = 0;
    int drop_responses_every = 0;
    int boot_entries = 0;

  private:
    static std::uint16_t Sensor(void *, hkc_sensor_t)
    {
        return 1365;
    }
    static bool Alarm(void *)
    {
        return false;
    }
    static std::uint8_t Switches(void *)
    {
        return 3;
    }
    static void Rgb(void *, std::uint8_t) {}
    static satlink_status_t AppSend(void *ctx, const satlink_can_frame_t *frame)
    {
        static_cast<VirtualHkc *>(ctx)->rx_.push_back(*frame);
        return SATLINK_OK;
    }
    static void EnterBoot(void *ctx)
    {
        auto *self = static_cast<VirtualHkc *>(ctx);
        ++self->boot_entries;
        self->in_app = false;
    }

    std::deque<satlink_can_frame_t> rx_;
};

hkc::ClientOptions FastOptions()
{
    return {std::chrono::milliseconds{1}, 3};
}

TEST(HkcClientTest, PingThenFlashThenBoot)
{
    VirtualHkc hkc;
    hkc::HkcClient client(hkc, FastOptions());
    auto info = client.Ping();
    EXPECT_EQ(1, info.version_major);
    EXPECT_FALSE(info.app_valid);

    const auto image = BuildImage(3000);
    std::size_t last_done = 0;
    client.Flash(image, [&](std::size_t done, std::size_t total) {
        EXPECT_GT(done, last_done);
        EXPECT_EQ(image.size(), total);
        last_done = done;
    });
    EXPECT_EQ(image.size(), last_done);
    EXPECT_TRUE(std::equal(image.begin(), image.end(), hkc.region_.begin()));
    EXPECT_TRUE(client.Ping().app_valid);

    client.Boot();
    EXPECT_TRUE(hkc.in_app);
}

TEST(HkcClientTest, FlashSurvivesLostResponses)
{
    VirtualHkc hkc;
    hkc.drop_responses_every = 7;
    hkc::HkcClient client(hkc, FastOptions());
    const auto image = BuildImage(2000);
    client.Flash(image);
    EXPECT_TRUE(std::equal(image.begin(), image.end(), hkc.region_.begin()));
}

TEST(HkcClientTest, FlashSurvivesLostRequests)
{
    VirtualHkc hkc;
    hkc.drop_requests_every = 5;
    hkc::HkcClient client(hkc, FastOptions());
    const auto image = BuildImage(500);
    client.Flash(image);
    EXPECT_TRUE(hkc.boot_.image_valid);
}

TEST(HkcClientTest, SilentTargetTimesOutAfterRetries)
{
    VirtualHkc hkc;
    hkc.drop_requests_every = 1;
    hkc::HkcClient client(hkc, FastOptions());
    EXPECT_THROW(client.Ping(), hkc::HkcTimeout);
    EXPECT_EQ(4, hkc.sent); // first attempt + 3 retries
}

TEST(HkcClientTest, InvalidImageIsRefusedBeforeAnyTraffic)
{
    VirtualHkc hkc;
    hkc::HkcClient client(hkc, FastOptions());
    auto image = BuildImage(100);
    image.back() ^= 0x01U;
    EXPECT_THROW(client.Flash(image), std::invalid_argument);
    EXPECT_EQ(0, hkc.sent);
}

TEST(HkcClientTest, BootWithoutImageReportsError)
{
    VirtualHkc hkc;
    hkc::HkcClient client(hkc, FastOptions());
    try
    {
        client.Boot();
        FAIL() << "expected HkcError";
    }
    catch (const hkc::HkcError &e)
    {
        EXPECT_EQ(SATLINK_CANBOOT_ST_BAD_IMAGE, e.Status());
    }
}

TEST(HkcClientTest, CommandSkipsUnrelatedFrames)
{
    VirtualHkc hkc;
    hkc.in_app = true;
    hkc::HkcClient client(hkc, FastOptions());
    // The first poll after the command also emits three housekeeping frames; the client must
    // pick the acknowledgement out of them.
    const auto ack = client.Command(SATLINK_HK_CMD_GET_VERSION);
    EXPECT_EQ(SATLINK_HK_ACK_OK, ack.status);
    EXPECT_EQ(3, ack.datac);
}

TEST(HkcClientTest, DescribeFrameFormatsHousekeeping)
{
    satlink_can_frame_t frame{};
    const satlink_hk_env_t env{-505, 1000, 1800, 1000};
    satlink_hk_encode_env(&env, &frame);
    EXPECT_EQ("env    temp=-5.05C vccint=1000mV vccaux=1800mV vbram=1000mV",
              hkc::DescribeFrame(frame).value_or(""));

    const satlink_hk_status_t status{42, SATLINK_HK_RESET_WATCHDOG, 1, 2, 0x03};
    satlink_hk_encode_status(&status, &frame);
    EXPECT_EQ("status uptime=42s reset=watchdog switches=1 cmds=2 errors=0x03",
              hkc::DescribeFrame(frame).value_or(""));

    frame.id = 0x333;
    EXPECT_FALSE(hkc::DescribeFrame(frame).has_value());
}

class FakeEnv : public hkc::Environment
{
  public:
    std::unique_ptr<hal::CanBus> OpenCan(const std::string &interface) override
    {
        opened = interface;
        return std::make_unique<Forward>(hkc);
    }
    std::vector<std::uint8_t> ReadFile(const std::string &path) override
    {
        if (path != "app.slhk")
        {
            throw std::system_error(ENOENT, std::generic_category(), path);
        }
        return file;
    }
    std::chrono::system_clock::time_point Now() override
    {
        return std::chrono::system_clock::time_point{std::chrono::seconds{1760000000}};
    }

    struct Forward : hal::CanBus
    {
        explicit Forward(VirtualHkc &t) : target(t) {}
        int Send(const satlink_can_frame_t &f) override
        {
            return target.Send(f);
        }
        int Receive(satlink_can_frame_t &f, std::chrono::milliseconds t) override
        {
            return target.Receive(f, t);
        }
        VirtualHkc &target;
    };

    VirtualHkc hkc;
    std::vector<std::uint8_t> file = BuildImage(600);
    std::string opened;
};

int RunCli(FakeEnv &env, std::vector<std::string> args, std::string *out_text = nullptr)
{
    std::ostringstream out;
    std::ostringstream err;
    const int rc = hkc::RunHkc(args, env, out, err);
    if (out_text != nullptr)
    {
        *out_text = out.str() + err.str();
    }
    return rc;
}

TEST(HkcCliTest, FlashAndBootThroughTheCli)
{
    FakeEnv env;
    std::string text;
    EXPECT_EQ(hkc::kExitOk, RunCli(env, {"--if", "can1", "flash", "app.slhk", "--boot"}, &text));
    EXPECT_EQ("can1", env.opened);
    EXPECT_NE(std::string::npos, text.find("100%"));
    EXPECT_NE(std::string::npos, text.find("application started"));
    EXPECT_TRUE(env.hkc.in_app);

    EXPECT_EQ(hkc::kExitOk, RunCli(env, {"version"}, &text));
    EXPECT_EQ("application 1.0.0\n", text);
    EXPECT_EQ(hkc::kExitOk, RunCli(env, {"period", "500"}));
    EXPECT_EQ(500, env.hkc.app_.period_ms);
    EXPECT_EQ(hkc::kExitOk, RunCli(env, {"enter-boot"}));
    EXPECT_FALSE(env.hkc.in_app);
}

TEST(HkcCliTest, UsageErrorsAndMissingFiles)
{
    FakeEnv env;
    EXPECT_EQ(hkc::kExitUsageError, RunCli(env, {}));
    EXPECT_EQ(hkc::kExitOk, RunCli(env, {"help"}));
    EXPECT_EQ(hkc::kExitUsageError, RunCli(env, {"led", "8"}));
    EXPECT_EQ(hkc::kExitUsageError, RunCli(env, {"frobnicate"}));
    EXPECT_EQ(hkc::kExitUsageError, RunCli(env, {"flash"}));
    EXPECT_EQ(hkc::kExitUsageError, RunCli(env, {"--if"}));
    EXPECT_EQ(hkc::kExitRuntimeError, RunCli(env, {"flash", "missing.slhk"}));
    EXPECT_TRUE(env.opened.empty()); // nothing touched the bus
}

TEST(HkcCliTest, RejectedCommandAndTimeouts)
{
    FakeEnv env;
    env.hkc.in_app = true;
    std::string text;
    EXPECT_EQ(hkc::kExitRuntimeError, RunCli(env, {"period", "50"}, &text));
    EXPECT_NE(std::string::npos, text.find("bad argument"));

    env.hkc.drop_requests_every = 1;
    EXPECT_EQ(hkc::kExitTimeout, RunCli(env, {"version"}));
}

TEST(HkcCliTest, MonitorAndTimeSync)
{
    FakeEnv env;
    env.hkc.in_app = true;
    hkc_app_poll(&env.hkc.app_, 0); // queues env, supply and status
    std::string text;
    EXPECT_EQ(hkc::kExitOk, RunCli(env, {"monitor", "count=3"}, &text));
    EXPECT_NE(std::string::npos, text.find("env    temp="));
    EXPECT_NE(std::string::npos, text.find("status uptime=0s"));
    EXPECT_EQ(hkc::kExitTimeout, RunCli(env, {"monitor", "count=1", "timeout_ms=1"}));
    EXPECT_EQ(hkc::kExitUsageError, RunCli(env, {"monitor", "bogus"}));

    EXPECT_EQ(hkc::kExitOk, RunCli(env, {"time-sync"}));
    EXPECT_EQ(1760000000U, env.hkc.app_.unix_offset_s);
}

} // namespace
} // namespace satlink::test
