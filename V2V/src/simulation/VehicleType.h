#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace v2v::simulation {

/// Distinct vehicle categories. The values matter — they're used to index a
/// constexpr table in `typeInfo()` and may be serialized as `uint8_t`.
enum class VehicleType : std::uint8_t {
    Car       = 0,
    Truck     = 1,
    Bus       = 2,
    Ambulance = 3,
    Police    = 4,

    Count
};

struct VehicleTypeInfo {
    const char* name;          ///< short label for legends
    glm::vec4   color;         ///< rendered color
    float       lengthMeters;  ///< physical length (nose to tail)
    float       widthMeters;   ///< physical width  (side to side)
    float       maxSpeedKmh;   ///< governing cap (vehicle won't exceed this)
    bool        emergency;     ///< gets priority behavior (step 11 / 12)
};

/// Per-type metadata. The fields drive both rendering and behavior.
const VehicleTypeInfo& typeInfo(VehicleType t) noexcept;

/// Human-readable name. Equivalent to `typeInfo(t).name`.
const char* typeName(VehicleType t) noexcept;

} // namespace v2v::simulation
