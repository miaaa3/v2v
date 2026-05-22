#pragma once

#include "graph/RoadGraph.h"
#include "simulation/Vehicle.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace v2v::simulation { class SimulationEngine; }
namespace v2v::vnet      { class BroadcastSystem; }

namespace v2v::simulation {

/**
 * Accident generator + lifecycle manager.
 *
 * Each `tick(now, sim)` call:
 *   1. Clears wrecks whose `wreckedUntil <= now` (already handled by the
 *      engine — we just garbage-collect our records here).
 *   2. With probability proportional to `m_ratePerSec` and the number of
 *      "tight-following" pairs (frontGap < tightGapMeters and speed > 5 m/s),
 *      triggers a fresh accident. The pair is frozen via `wreckedUntil = now
 *      + duration`. If a `BroadcastSystem` pointer is supplied, an emergency
 *      V2V broadcast is auto-emitted from one of the involved vehicles.
 *
 * Accidents are also queryable for rendering (red flashing markers).
 */
class AccidentSystem {
public:
    struct Accident {
        glm::vec2     position{0.0f, 0.0f};
        std::uint32_t a   = 0;   // primary vehicle id (rear-end follower)
        std::uint32_t b   = 0;   // vehicle ahead (may equal a if a == INVALID)
        double        startTime = 0.0;
        double        endTime   = 0.0;
        bool          emergency = false; // true if any involved is emergency-class
    };

    void setEnabled(bool e)              { m_enabled = e; }
    bool isEnabled() const               { return m_enabled; }

    /// Expected accidents per real second, before per-step Poisson dice.
    /// 0 disables triggering (existing wrecks still clear out).
    void  setRatePerSec(float r)         { m_ratePerSec = (r >= 0.0f) ? r : 0.0f; }
    float ratePerSec() const             { return m_ratePerSec; }

    /// Seconds a wreck stays on the road before vehicles are released.
    void  setDuration(float s)           { if (s > 1.0f) m_duration = s; }
    float duration() const               { return m_duration; }

    /// Force an accident on the next tick (UI "Trigger now" button).
    void  forceTrigger()                 { m_forcePending = true; }

    /// Per-frame update. `dt` is real (unscaled) seconds.
    /// If `bsys` is non-null, an emergency broadcast is auto-emitted from
    /// the involved vehicle. Returns the number of accidents triggered this
    /// tick (0 most of the time).
    int   tick(double now, float dt, SimulationEngine& sim,
               vnet::BroadcastSystem* bsys);

    /// Active wrecks (rendered as flashing red markers).
    const std::vector<Accident>& active() const { return m_active; }
    std::size_t total() const { return m_totalEver; }

    /// Clear records (does not un-wreck vehicles already on the road; the
    /// engine releases them by timestamp). Used by "Clear" UI.
    void clear() { m_active.clear(); }

private:
    bool                  m_enabled      = false;  // user opts-in via UI
    float                 m_ratePerSec   = 0.05f;   // ~1 every 20 s when enabled
    float                 m_duration     = 8.0f;    // wreck blocks edge 8 s
    bool                  m_forcePending = false;
    std::size_t           m_totalEver    = 0;
    std::mt19937          m_rng{0xACC1DE57u};
    std::vector<Accident> m_active;

    /// Pick a (follower, leader) pair that is currently tight-following.
    /// Returns false if no eligible pair exists.
    bool pickPair(const SimulationEngine& sim,
                  std::uint32_t& follower, std::uint32_t& leader);
};

} // namespace v2v::simulation
