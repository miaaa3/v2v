#pragma once

namespace v2v::utils {

enum class WeatherMode {
    Clear  = 0,
    Rain   = 1,
    Fog    = 2,
};

/// Global weather state. A single value drives both visual particles
/// (RainRenderer) and physical impacts (V2V range, vehicle throttle).
struct Weather {
    WeatherMode mode      = WeatherMode::Clear;
    float       intensity = 0.0f;   // 0 = none, 1 = peak (rain heavy / fog dense)

    /// Multiplier applied to V2V transmission ranges. Rain mildly degrades,
    /// fog more aggressively (water droplets and scattering).
    float v2vRangeFactor() const {
        switch (mode) {
            case WeatherMode::Rain: return 1.0f - 0.35f * intensity;
            case WeatherMode::Fog:  return 1.0f - 0.55f * intensity;
            default:                return 1.0f;
        }
    }

    /// Multiplier applied to vehicle speeds. Wet pavement / poor visibility
    /// makes drivers slow down.
    float speedFactor() const {
        switch (mode) {
            case WeatherMode::Rain: return 1.0f - 0.20f * intensity;
            case WeatherMode::Fog:  return 1.0f - 0.30f * intensity;
            default:                return 1.0f;
        }
    }
};

} // namespace v2v::utils
