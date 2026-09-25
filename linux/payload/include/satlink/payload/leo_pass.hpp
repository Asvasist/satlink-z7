/**
 * @file leo_pass.hpp
 * @brief Geometry and link budget of a low-Earth-orbit pass over a ground station, used to
 *        drive the channel emulator so the adaptive link sees a realistic Es/N0 profile.
 *
 * Model: circular orbit of altitude h, spherical Earth, no Earth rotation (a pass lasts
 * minutes). The station lies at a cross-track central angle g0 from the ground track, chosen so
 * the pass culminates at the requested maximum elevation. With w the orbital angular rate and
 * t = 0 at culmination, the central angle is cos g = cos g0 cos(w t), and
 *
 *   elevation  e = atan2(cos g - R/r, sin g)
 *   range      d = sqrt(r^2 + R^2 - 2 r R cos g)
 *   Es/N0      = Es/N0(zenith) - 20 log10(d / h) - A / sin(e)
 *
 * with A the zenith atmospheric loss. Outside the elevation mask the link is closed.
 *
 * @implements SRS-ACM-002
 */
#pragma once

#include <cstdint>

namespace satlink::payload {

struct PassConfig
{
    double altitude_km = 550.0;
    double max_elevation_deg = 60.0;
    double min_elevation_deg = 5.0; ///< elevation mask: AOS / LOS
    double zenith_esn0_db = 20.0;   ///< Es/N0 with the satellite overhead
    double zenith_atmos_db = 0.3;   ///< clear-sky loss at 90 degrees
};

struct PassSample
{
    double time_s = 0.0; ///< relative to culmination
    double elevation_deg = 0.0;
    double range_km = 0.0;
    double range_rate_km_s = 0.0; ///< > 0 receding
    double esn0_db = 0.0;
    bool visible = false;
};

class LeoPass
{
  public:
    static constexpr double kEarthRadiusKm = 6371.0;
    static constexpr double kMuKm3S2 = 398600.4418;

    explicit LeoPass(const PassConfig &config);

    [[nodiscard]] PassSample At(double time_s) const;

    /// Time from AOS to LOS (0 if the pass never rises above the mask).
    [[nodiscard]] double DurationS() const
    {
        return 2.0 * half_duration_s_;
    }
    /// AOS time (negative: before culmination).
    [[nodiscard]] double AosS() const
    {
        return -half_duration_s_;
    }
    [[nodiscard]] double OrbitalPeriodS() const;
    [[nodiscard]] const PassConfig &Config() const
    {
        return config_;
    }

  private:
    [[nodiscard]] double ElevationAt(double time_s) const;
    [[nodiscard]] double RangeAt(double time_s) const;

    PassConfig config_;
    double r_km_;
    double omega_;  ///< rad/s
    double gamma0_; ///< rad
    double half_duration_s_ = 0.0;
};

/// Channel emulator setting for an Es/N0: software-loopback units, sigma * @p scale.
struct ChannelSetting
{
    std::uint16_t noise_level = 0;
    std::uint16_t gain_q15 = 0x7FFF;
};

/// Noise level giving @p esn0_db (unit-energy symbols); gain 0 when @p visible is false.
ChannelSetting ChannelFor(double esn0_db, bool visible, double noise_scale = 4096.0);

} // namespace satlink::payload
