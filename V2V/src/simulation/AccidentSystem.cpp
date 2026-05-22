#include "simulation/AccidentSystem.h"

#include "simulation/SimulationEngine.h"
#include "simulation/VehicleType.h"
#include "v2v/Broadcast.h"

#include <algorithm>
#include <cmath>

namespace v2v::simulation {

bool AccidentSystem::pickPair(const SimulationEngine& sim,
                              std::uint32_t& follower,
                              std::uint32_t& leader)
{
    const auto& pool = sim.pool();
    if (pool.size() < 2) return false;

    // Build a list of candidate followers in the pool. To stay O(n) without
    // an allocation per tick we sample a few random indices and accept the
    // first one whose frontGap is tight and whose speed is > 5 m/s.
    std::uniform_int_distribution<std::uint32_t> dist(
        0, static_cast<std::uint32_t>(pool.size() - 1));

    constexpr float kTightGapMeters = 9.0f;
    constexpr float kMinSpeed       = 5.0f;
    constexpr int   kMaxTries       = 64;

    for (int t = 0; t < kMaxTries; ++t) {
        const std::uint32_t i = dist(m_rng);
        const auto& v = pool[i];
        if (v.wreckedUntil > sim.now())              continue;
        if (v.currentEdge == graph::INVALID_EDGE)    continue;
        if (v.frontGap >= kTightGapMeters)           continue;
        if (v.speed   <= kMinSpeed)                  continue;

        // Find the vehicle directly in front on the same edge. Linear scan is
        // fine — at most a few vehicles share an edge in city traffic.
        const auto edge = v.currentEdge;
        std::uint32_t bestId = static_cast<std::uint32_t>(-1);
        float bestDelta = 1e9f;
        for (std::uint32_t j = 0; j < pool.size(); ++j) {
            if (j == i) continue;
            const auto& o = pool[j];
            if (o.currentEdge != edge) continue;
            if (o.wreckedUntil > sim.now())          continue;
            const float delta = o.edgeProgress - v.edgeProgress;
            if (delta > 0.0f && delta < bestDelta) {
                bestDelta = delta;
                bestId    = j;
            }
        }
        if (bestId == static_cast<std::uint32_t>(-1)) continue;

        follower = i;
        leader   = bestId;
        return true;
    }
    return false;
}

int AccidentSystem::tick(double now, float dt, SimulationEngine& sim,
                         vnet::BroadcastSystem* bsys)
{
    // 1) Garbage-collect ended wrecks.
    m_active.erase(
        std::remove_if(m_active.begin(), m_active.end(),
                       [now](const Accident& a) { return a.endTime <= now; }),
        m_active.end());

    if (!m_enabled) return 0;

    // 2) Decide whether to trigger now.
    bool trigger = m_forcePending;
    m_forcePending = false;

    if (!trigger && m_ratePerSec > 0.0f && dt > 0.0f) {
        // Bernoulli per frame with mean `ratePerSec * dt`.
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        if (u(m_rng) < m_ratePerSec * dt) trigger = true;
    }
    if (!trigger) return 0;

    std::uint32_t follower = 0, leader = 0;
    if (!pickPair(sim, follower, leader)) return 0;

    auto& pool = sim.pool();
    const double endT = now + static_cast<double>(m_duration);
    pool[follower].wreckedUntil = endT;
    pool[leader  ].wreckedUntil = endT;

    Accident a;
    a.position  = pool[follower].position;
    a.a         = follower;
    a.b         = leader;
    a.startTime = now;
    a.endTime   = endT;
    a.emergency = typeInfo(pool[follower].type).emergency ||
                  typeInfo(pool[leader  ].type).emergency;
    m_active.push_back(a);
    ++m_totalEver;

    // 3) Auto-broadcast a V2V emergency signal from the follower (the rear-
    //    ender is usually the one to alert traffic behind it).
    if (bsys) {
        bsys->emit(follower, pool, now);
    }
    return 1;
}

} // namespace v2v::simulation
