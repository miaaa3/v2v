#pragma once

#include "simulation/VehiclePool.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace v2v::vnet {

/// One outstanding broadcast emitted from a source vehicle. The radius grows
/// linearly with time at `kSignalSpeed` until it reaches `maxRadius`, then the
/// broadcast is dropped.
struct Broadcast {
    std::uint32_t sourceId  = 0;
    glm::vec2     origin   {0.0f, 0.0f};
    double        startTime = 0.0;     // seconds since program start
    float         maxRadius = 350.0f;  // meters (= source's V2V range)
    glm::vec4     color    {0.4f, 0.95f, 1.0f, 1.0f};

    /// Hop generation: 0 = original broadcast, 1 = first relay, ..., up to
    /// `maxGeneration`. Used to visually distinguish each hop with a different
    /// color and to bound multi-hop propagation.
    int           generation    = 0;
    int           maxGeneration = 3;
};

/**
 * Manager of active V2V broadcasts.
 *
 * Each `tick(now, pool)` call:
 *   - removes expired broadcasts,
 *   - flags each vehicle that the expanding ring just passed by setting its
 *     `lastBroadcastReceivedAt`. The receiver glow is then handled by the
 *     vehicle renderer (or the main loop, since it's just a timestamp).
 *
 * Speed of the visible wavefront is purely visual; real V2V signals
 * propagate at $\sim$c, but at that speed nothing would be visible.
 */
class BroadcastSystem {
public:
    /// Visible wavefront speed in m/s. Set low enough that the user can see
    /// the ring expanding.
    static constexpr float kSignalSpeed = 180.0f;

    /// Trigger a broadcast from `sourceId` (must be a valid index in `pool`).
    /// Does nothing if the id is out of range. Increments
    /// `pool[sourceId].packetsSent` for the telemetry inspector.
    void emit(std::uint32_t sourceId, simulation::VehiclePool& pool,
              double now);

    /// Trigger a broadcast from an arbitrary world position (used by the
    /// accident system, virtual roadside units, etc.). `sourceId` may be
    /// 0xFFFFFFFF for "system" broadcasts that aren't owned by a vehicle.
    void emitAt(glm::vec2 origin, glm::vec4 color, float maxRadius,
                double now, std::uint32_t sourceId = 0xFFFFFFFF);

    /// Advance the simulation of all active broadcasts. Call once per frame.
    void tick(double now, simulation::VehiclePool& pool);

    /// Currently active broadcasts (for rendering).
    const std::vector<Broadcast>& active() const { return m_active; }
    std::size_t count() const { return m_active.size(); }

    /// Wipe all active broadcasts.
    void clear() { m_active.clear(); }

private:
    std::vector<Broadcast> m_active;
};

} // namespace v2v::vnet
