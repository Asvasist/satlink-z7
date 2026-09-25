/**
 * @file leo_pass.cpp
 * @implements SRS-ACM-002
 */
#include "satlink/payload/leo_pass.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace satlink::payload {
namespace {
constexpr double kDeg = std::numbers::pi / 180.0;
} // namespace

LeoPass::LeoPass(const PassConfig &config)
    : config_(config), r_km_(kEarthRadiusKm + config.altitude_km),
      omega_(std::sqrt(kMuKm3S2 / (r_km_ * r_km_ * r_km_)))
{
    // Central angle at culmination for the requested maximum elevation.
    const double emax = std::clamp(config.max_elevation_deg, 0.1, 90.0) * kDeg;
    gamma0_ = std::acos(kEarthRadiusKm * std::cos(emax) / r_km_) - emax;
    // Central angle at the elevation mask, then the along-track time to reach it.
    const double emin = config.min_elevation_deg * kDeg;
    const double gamma_mask = std::acos(kEarthRadiusKm * std::cos(emin) / r_km_) - emin;
    if (config.max_elevation_deg > config.min_elevation_deg && gamma_mask > gamma0_)
    {
        const double c = std::cos(gamma_mask) / std::cos(gamma0_);
        half_duration_s_ = std::acos(std::clamp(c, -1.0, 1.0)) / omega_;
    }
}

double LeoPass::OrbitalPeriodS() const
{
    return 2.0 * std::numbers::pi / omega_;
}

double LeoPass::ElevationAt(double time_s) const
{
    const double g = std::acos(std::cos(gamma0_) * std::cos(omega_ * time_s));
    return std::atan2(std::cos(g) - (kEarthRadiusKm / r_km_), std::sin(g)) / kDeg;
}

double LeoPass::RangeAt(double time_s) const
{
    const double g = std::acos(std::cos(gamma0_) * std::cos(omega_ * time_s));
    return std::sqrt((r_km_ * r_km_) + (kEarthRadiusKm * kEarthRadiusKm) -
                     (2.0 * r_km_ * kEarthRadiusKm * std::cos(g)));
}

PassSample LeoPass::At(double time_s) const
{
    PassSample s;
    s.time_s = time_s;
    s.elevation_deg = ElevationAt(time_s);
    s.range_km = RangeAt(time_s);
    constexpr double kDt = 0.5;
    s.range_rate_km_s = (RangeAt(time_s + kDt) - RangeAt(time_s - kDt)) / (2.0 * kDt);
    s.visible = s.elevation_deg >= config_.min_elevation_deg;
    const double el = std::max(s.elevation_deg, 0.5) * kDeg;
    s.esn0_db = config_.zenith_esn0_db - (20.0 * std::log10(s.range_km / config_.altitude_km)) -
                (config_.zenith_atmos_db / std::sin(el));
    return s;
}

ChannelSetting ChannelFor(double esn0_db, bool visible, double noise_scale)
{
    ChannelSetting c;
    const double sigma = std::pow(10.0, -esn0_db / 20.0);
    c.noise_level =
        static_cast<std::uint16_t>(std::lround(std::clamp(sigma * noise_scale, 0.0, 65535.0)));
    c.gain_q15 = visible ? 0x7FFF : 0;
    if (!visible)
    {
        c.noise_level = static_cast<std::uint16_t>(noise_scale); // 0 dB of noise, no signal
    }
    return c;
}

} // namespace satlink::payload
