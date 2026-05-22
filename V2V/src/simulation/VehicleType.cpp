#include "simulation/VehicleType.h"

namespace v2v::simulation {

namespace {

// Order *must* match the VehicleType enum.
constexpr VehicleTypeInfo kTable[] = {
    // name         color (rgba)                              len  width  vMax  emerg
    {"Car",        {0.30f, 0.78f, 0.96f, 1.00f},              4.5f, 1.8f,  130.0f, false},
    {"Truck",      {0.55f, 0.55f, 0.62f, 1.00f},              9.5f, 2.5f,   90.0f, false},
    {"Bus",        {0.97f, 0.78f, 0.20f, 1.00f},             12.0f, 2.6f,   80.0f, false},
    {"Ambulance",  {0.96f, 0.96f, 0.96f, 1.00f},              5.8f, 2.2f,  140.0f, true },
    {"Police",     {0.25f, 0.45f, 0.98f, 1.00f},              4.8f, 1.9f,  150.0f, true },
};
static_assert(sizeof(kTable) / sizeof(kTable[0]) == static_cast<std::size_t>(VehicleType::Count),
              "VehicleType table size mismatch — keep enum and table in sync.");

} // namespace

const VehicleTypeInfo& typeInfo(VehicleType t) noexcept
{
    const auto idx = static_cast<std::size_t>(t);
    if (idx >= static_cast<std::size_t>(VehicleType::Count)) return kTable[0];
    return kTable[idx];
}

const char* typeName(VehicleType t) noexcept
{
    return typeInfo(t).name;
}

} // namespace v2v::simulation
