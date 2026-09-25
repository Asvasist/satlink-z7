/**
 * @file payload_manager.hpp
 * @brief The on-board payload manager: PUS services, telemetry downlink through the modem,
 *        IP over the RF link, LEO pass emulation.
 *
 * ```
 * ground (UDP) --TC--> PUS services ----------------------------------+
 *                        | ST[1] verification, ST[3] housekeeping,    |
 *                        | ST[5] events, ST[8] functions, ST[17] test |
 *                        v                                            |
 *                     TM packets --VC0--+                             |
 * TUN "sat" --IP--> APID 0x3F0 --VC1----+-> frame mux --TX_FRAME--> modem (Core 1)
 * TUN "gnd" --IP--> APID 0x3F1 --VC1----+                              | RF loopback
 *                                                                      v
 * ground (UDP) <--TM-- VC0 <--+-- frame demux <--RX_FRAME------- modem (Core 1)
 * TUN "gnd"   <--IP--  0x3F0 <+
 * TUN "sat"   <--IP--  0x3F1 <+
 * ```
 *
 * Telemetry reaches the ground only through the modem link. While the receiver has no lock
 * (LOS, deep fade) the packets are stored in the virtual channel queues and downlinked when the
 * link is back (store and forward); frames lost to errors while locked stay lost, as on a real
 * downlink. Telecommands arrive
 * over Ethernet: the board emulates the downlink only. `tm_direct` sends a copy of all TM
 * straight to the ground as well (for debugging without a link).
 *
 * The manager is single-threaded and non-blocking: the daemon calls Step() whenever one of its
 * file descriptors is readable and at least every 10 ms.
 *
 * @implements SRS-PLM-001
 * @implements SRS-SYS-003
 */
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "satlink/amp_client/modem_client.hpp"
#include "satlink/payload/interfaces.hpp"
#include "satlink/payload/leo_pass.hpp"
#include "satlink/payload/mission.hpp"
#include "satlink/pus/space_packet.hpp"
#include "satlink/pus/tm_frame.hpp"

namespace satlink::payload {

struct PayloadConfig
{
    bool tm_direct = false;
    std::uint32_t hk_period_ms = 1000;
    double noise_scale = 4096.0;         ///< channel emulator units per unit sigma
    std::uint32_t max_frames_queued = 4; ///< modem TX queue depth to aim for
    std::size_t downlink_queue = 1024;   ///< packets stored per virtual channel while no link
    std::uint8_t initial_modcod = 1;
    std::uint8_t loopback = 2; ///< SATLINK_LOOP_*: software until the PL exists
    bool acm = true;
    /// Restarts the Core 1 firmware (ST[8] kRestartModem); empty if not possible.
    std::function<int()> restart_modem;
};

struct PayloadStats
{
    std::uint32_t tc_received = 0;
    std::uint32_t tc_rejected = 0;
    std::uint32_t tm_generated = 0;
    std::uint32_t tm_delivered = 0; ///< TM that came back through the link
    std::uint32_t frames_sent = 0;
    std::uint32_t frames_received = 0;
    std::uint32_t frames_crc_error = 0;
    std::uint32_t ip_down = 0; ///< datagrams satellite -> ground delivered
    std::uint32_t ip_up = 0;   ///< datagrams ground -> satellite delivered
    std::uint32_t events = 0;
};

/// Current state of the pass emulation.
struct PassState
{
    bool active = false;
    double time_scale = 1.0;
    std::uint64_t start_ms = 0;
    PassSample sample;
    bool visible = false;
};

class PayloadManager
{
  public:
    PayloadManager(PayloadConfig config, amp::MessagePort &modem, GroundLink &ground, Clock &clock,
                   PacketTunnel *sat_tun = nullptr, PacketTunnel *gnd_tun = nullptr,
                   PlatformSource *platform = nullptr);

    /// Configures the firmware (time, modem, ACM). Call once before Step().
    void Start();

    /// Handle everything that is ready; never blocks.
    void Step();

    /// Starts a pass (also available as ST[8] kStartPass).
    void StartPass(const PassConfig &config, double time_scale);
    void StopPass();

    [[nodiscard]] const PayloadStats &Stats() const
    {
        return stats_;
    }
    [[nodiscard]] const pus::DemuxStats &LinkStats() const
    {
        return demux_.Stats();
    }
    [[nodiscard]] const std::optional<satlink_msg_status_t> &ModemStatus() const
    {
        return client_.LastStatus();
    }
    [[nodiscard]] const PassState &Pass() const
    {
        return pass_;
    }

    /// Encodes a housekeeping report (also used by the SCPI server).
    std::vector<std::uint8_t> HousekeepingData(HkStructure sid);

  private:
    void HandleTc(std::span<const std::uint8_t> datagram);
    bool Execute(const pus::Telecommand &tc, FailureCode &failure);
    bool ExecuteFunction(std::span<const std::uint8_t> data, FailureCode &failure);
    void Verification(const pus::Telecommand &tc, std::uint8_t subtype,
                      std::optional<FailureCode> failure);
    void EmitTm(std::uint8_t service, std::uint8_t subtype, std::vector<std::uint8_t> data);
    void EmitEvent(Event id, std::uint8_t severity, std::vector<std::uint8_t> aux = {});
    void OnRxFrame(const amp::RxFrame &frame);
    void OnPacket(std::uint8_t vc, std::span<const std::uint8_t> packet);
    void OnStatus(const satlink_msg_status_t &status);
    void OnLog(const amp::LogLine &line);
    void UpdatePass(std::uint64_t now);
    void UpdateHousekeeping(std::uint64_t now);
    void FeedModem(std::uint64_t now);
    void ReadTunnels();

    PayloadConfig config_;
    amp::ModemClient client_;
    GroundLink &ground_;
    Clock &clock_;
    PacketTunnel *sat_tun_;
    PacketTunnel *gnd_tun_;
    PlatformSource *platform_;

    pus::FrameMultiplexer mux_;
    pus::FrameDemultiplexer demux_;
    pus::SequenceCounter tm_seq_;
    pus::SequenceCounter ip_down_seq_;
    pus::SequenceCounter ip_up_seq_;
    std::uint16_t message_counter_ = 0;

    std::array<bool, 4> hk_enabled_{false, true, true, true};
    std::array<std::uint32_t, 4> hk_period_ms_{};
    std::array<std::uint64_t, 4> hk_next_ms_{};

    PassState pass_;
    std::unique_ptr<LeoPass> leo_;
    std::uint64_t pass_last_update_ms_ = 0;

    double queue_estimate_ = 0.0;
    std::uint64_t last_feed_ms_ = 0;
    bool was_locked_ = false;
    PayloadStats stats_;
};

} // namespace satlink::payload
