/**
 * @file hkc_client.cpp
 * @implements SRS-HKC-006
 */
#include "satlink/hkc/hkc_client.hpp"

#include <algorithm>
#include <cerrno>
#include <sstream>
#include <system_error>

#include "satlink/common/byte_order.h"
#include "satlink/common/crc32.h"
#include "satlink/hkc/canboot.h"
#include "satlink/hkc/image.h"
#include "satlink/regs/address_map.h"

namespace satlink::hkc {
namespace {

const char *ImageResultName(satlink_hkc_image_result_t result)
{
    switch (result)
    {
    case SATLINK_HKC_IMAGE_OK:
        return "ok";
    case SATLINK_HKC_IMAGE_TOO_SHORT:
        return "truncated image";
    case SATLINK_HKC_IMAGE_BAD_MAGIC:
        return "not an HKC image (bad magic)";
    case SATLINK_HKC_IMAGE_BAD_HEADER:
        return "unsupported header";
    case SATLINK_HKC_IMAGE_HEADER_CRC:
        return "header CRC mismatch";
    case SATLINK_HKC_IMAGE_BAD_LOCATION:
        return "wrong load address, entry point or size";
    case SATLINK_HKC_IMAGE_PAYLOAD_CRC:
        return "payload CRC mismatch";
    }
    return "unknown";
}

std::string Millivolts(std::uint16_t mv)
{
    return std::to_string(mv) + "mV";
}

std::string Hundredths(int value)
{
    const int whole = value / 100;
    const int frac = (value < 0 ? -value : value) % 100;
    std::string text = (value < 0 && whole == 0) ? "-" : "";
    text += std::to_string(whole) + ".";
    text += (frac < 10 ? "0" : "") + std::to_string(frac);
    return text;
}

} // namespace

const char *CanbootStatusName(std::uint8_t status)
{
    switch (status)
    {
    case SATLINK_CANBOOT_ST_OK:
        return "ok";
    case SATLINK_CANBOOT_ST_BAD_STATE:
        return "not allowed in this state";
    case SATLINK_CANBOOT_ST_BAD_SEQ:
        return "frame out of sequence";
    case SATLINK_CANBOOT_ST_TOO_LARGE:
        return "image too large";
    case SATLINK_CANBOOT_ST_CRC:
        return "CRC mismatch";
    case SATLINK_CANBOOT_ST_BAD_IMAGE:
        return "no valid image";
    case SATLINK_CANBOOT_ST_BAD_LENGTH:
        return "bad length";
    case SATLINK_CANBOOT_ST_UNKNOWN:
        return "unknown command";
    default:
        return "unknown status";
    }
}

satlink_can_frame_t
HkcClient::Transact(const satlink_can_frame_t &request,
                    const std::function<bool(const satlink_can_frame_t &)> &match)
{
    for (int attempt = 0; attempt <= options_.retries; ++attempt)
    {
        const int rc = bus_.Send(request);
        if (rc != 0)
        {
            throw std::system_error(-rc, std::generic_category(), "CAN send");
        }
        const auto deadline = std::chrono::steady_clock::now() + options_.response_timeout;
        for (;;)
        {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            satlink_can_frame_t response{};
            const int rx = bus_.Receive(response, std::max(left, std::chrono::milliseconds{0}));
            if (rx == -ETIMEDOUT)
            {
                break; // retry
            }
            if (rx != 0)
            {
                throw std::system_error(-rx, std::generic_category(), "CAN receive");
            }
            if (match(response))
            {
                return response;
            }
        }
    }
    throw HkcTimeout("no response from the housekeeping controller");
}

satlink_can_frame_t HkcClient::BootRequest(std::uint8_t opcode,
                                           std::span<const std::uint8_t> payload)
{
    satlink_can_frame_t request{};
    satlink_canboot_encode_request(opcode, payload.data(),
                                   static_cast<std::uint8_t>(payload.size()), &request);
    const auto expected = static_cast<std::uint8_t>(opcode | SATLINK_CANBOOT_RESPONSE_FLAG);
    const bool is_data = opcode == SATLINK_CANBOOT_OP_DATA;
    const std::uint8_t seq = is_data ? payload[0] : 0;
    return Transact(request, [&](const satlink_can_frame_t &r) {
        return (r.id == SATLINK_CANBOOT_ID_RESPONSE) && (r.dlc >= 2) && (r.data[0] == expected) &&
               (!is_data || ((r.dlc >= 3) && (r.data[2] == seq)));
    });
}

BootloaderInfo HkcClient::Ping()
{
    const auto r = BootRequest(SATLINK_CANBOOT_OP_PING, {});
    if (r.dlc < 6)
    {
        throw HkcError("short PING response", SATLINK_CANBOOT_ST_BAD_LENGTH);
    }
    return {r.data[2], r.data[3], r.data[4] != 0, r.data[5]};
}

void HkcClient::Flash(std::span<const std::uint8_t> image, const Progress &progress)
{
    const auto check = satlink_hkc_image_verify(
        image.data(), image.size(), SATLINK_HKC_LMB_APP_BASE, SATLINK_HKC_LMB_APP_SIZE, nullptr);
    if (check != SATLINK_HKC_IMAGE_OK)
    {
        throw std::invalid_argument(std::string("refusing to flash: ") + ImageResultName(check));
    }

    auto expect_ok = [](const satlink_can_frame_t &r, const char *step) {
        if (r.data[1] != SATLINK_CANBOOT_ST_OK)
        {
            throw HkcError(std::string(step) + ": " + CanbootStatusName(r.data[1]), r.data[1]);
        }
    };

    std::uint8_t size[4];
    satlink_put_le32(size, static_cast<std::uint32_t>(image.size()));
    expect_ok(BootRequest(SATLINK_CANBOOT_OP_START, size), "START");

    std::uint8_t seq = 0;
    for (std::size_t offset = 0; offset < image.size(); offset += SATLINK_CANBOOT_CHUNK)
    {
        const std::size_t len = std::min<std::size_t>(SATLINK_CANBOOT_CHUNK, image.size() - offset);
        std::uint8_t payload[1 + SATLINK_CANBOOT_CHUNK] = {seq};
        std::copy_n(image.begin() + static_cast<std::ptrdiff_t>(offset), len, payload + 1);
        expect_ok(BootRequest(SATLINK_CANBOOT_OP_DATA, std::span(payload, len + 1)), "DATA");
        ++seq;
        if (progress)
        {
            progress(offset + len, image.size());
        }
    }

    std::uint8_t crc[4];
    satlink_put_le32(crc, satlink_crc32(image.data(), image.size()));
    expect_ok(BootRequest(SATLINK_CANBOOT_OP_END, crc), "END");
}

void HkcClient::Boot()
{
    const auto r = BootRequest(SATLINK_CANBOOT_OP_BOOT, {});
    if (r.data[1] != SATLINK_CANBOOT_ST_OK)
    {
        throw HkcError(std::string("BOOT: ") + CanbootStatusName(r.data[1]), r.data[1]);
    }
}

void HkcClient::Abort()
{
    (void)BootRequest(SATLINK_CANBOOT_OP_ABORT, {});
}

satlink_hk_ack_t HkcClient::Command(std::uint8_t opcode, std::span<const std::uint8_t> args)
{
    satlink_can_frame_t request{};
    if (satlink_hk_encode_command(opcode, args.data(), static_cast<std::uint8_t>(args.size()),
                                  &request) != SATLINK_OK)
    {
        throw std::invalid_argument("too many command arguments");
    }
    satlink_hk_ack_t ack{};
    Transact(request, [&](const satlink_can_frame_t &r) {
        return (satlink_hk_decode_ack(&r, &ack) == SATLINK_OK) && (ack.opcode == opcode);
    });
    return ack;
}

void HkcClient::SyncTime(std::chrono::system_clock::time_point now)
{
    const auto since_epoch = now.time_since_epoch();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(since_epoch);
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch - seconds);
    satlink_hk_time_t time{static_cast<std::uint32_t>(seconds.count()),
                           static_cast<std::uint16_t>(millis.count())};
    satlink_can_frame_t frame{};
    satlink_hk_encode_time(&time, &frame);
    const int rc = bus_.Send(frame);
    if (rc != 0)
    {
        throw std::system_error(-rc, std::generic_category(), "CAN send");
    }
}

std::optional<std::string> DescribeFrame(const satlink_can_frame_t &frame)
{
    std::ostringstream text;
    satlink_hk_env_t env{};
    satlink_hk_supply_t supply{};
    satlink_hk_status_t status{};
    satlink_hk_ack_t ack{};
    satlink_hk_time_t time{};
    if (satlink_hk_decode_env(&frame, &env) == SATLINK_OK)
    {
        text << "env    temp=" << Hundredths(env.die_temp_centi_c) << "C"
             << " vccint=" << Millivolts(env.vccint_mv) << " vccaux=" << Millivolts(env.vccaux_mv)
             << " vbram=" << Millivolts(env.vbram_mv);
    }
    else if (satlink_hk_decode_supply(&frame, &supply) == SATLINK_OK)
    {
        text << "supply vccpint=" << Millivolts(supply.vccpint_mv)
             << " vccpaux=" << Millivolts(supply.vccpaux_mv)
             << " vcco_ddr=" << Millivolts(supply.vcco_ddr_mv);
    }
    else if (satlink_hk_decode_status(&frame, &status) == SATLINK_OK)
    {
        static const char *const k_causes[] = {"power-on", "watchdog", "command"};
        const char *cause = (status.reset_cause < 3) ? k_causes[status.reset_cause] : "?";
        static const char *const k_hex = "0123456789ABCDEF";
        const std::string flags =
            std::string("0x") + k_hex[status.error_flags >> 4U] + k_hex[status.error_flags & 0x0FU];
        text << "status uptime=" << status.uptime_s << "s reset=" << cause
             << " switches=" << static_cast<int>(status.switches)
             << " cmds=" << static_cast<int>(status.cmd_count) << " errors=" << flags;
    }
    else if (satlink_hk_decode_ack(&frame, &ack) == SATLINK_OK)
    {
        text << "ack    opcode=" << static_cast<int>(ack.opcode)
             << " status=" << static_cast<int>(ack.status);
    }
    else if (satlink_hk_decode_time(&frame, &time) == SATLINK_OK)
    {
        text << "time   unix=" << time.unix_s << "." << time.millis;
    }
    else if (frame.id == SATLINK_CANBOOT_ID_RESPONSE && frame.dlc >= 2)
    {
        text << "boot   response opcode=" << static_cast<int>(frame.data[0] & 0x7FU)
             << " status=" << CanbootStatusName(frame.data[1]);
    }
    else
    {
        return std::nullopt;
    }
    return text.str();
}

} // namespace satlink::hkc
