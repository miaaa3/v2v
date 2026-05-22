#include "v2v/Broadcast.h"

#include <algorithm>
#include <cmath>

namespace v2v::vnet {

void BroadcastSystem::emit(std::uint32_t sourceId,
                           simulation::VehiclePool& pool,
                           double now)
{
    if (sourceId >= pool.size()) return;
    auto& v = pool.vehicles()[sourceId];
    ++v.packetsSent;
    Broadcast b;
    b.sourceId  = sourceId;
    b.origin    = v.position;
    b.startTime = now;
    b.maxRadius = std::max(50.0f, v.transmissionRange);
    // Cyan for civilian, orange-red for emergency vehicles (they 'matter more').
    if (v.type == simulation::VehicleType::Ambulance ||
        v.type == simulation::VehicleType::Police)
    {
        b.color = { 1.0f, 0.45f, 0.20f, 1.0f };
    } else {
        b.color = { 0.40f, 0.95f, 1.0f, 1.0f };
    }
    m_active.push_back(b);
}

void BroadcastSystem::emitAt(glm::vec2 origin, glm::vec4 color,
                             float maxRadius, double now,
                             std::uint32_t sourceId)
{
    Broadcast b;
    b.sourceId  = sourceId;
    b.origin    = origin;
    b.startTime = now;
    b.maxRadius = std::max(50.0f, maxRadius);
    b.color     = color;
    m_active.push_back(b);
}

namespace {

// Per-generation color (capped at maxGeneration). Cyan → green → yellow →
// orange creates a visible "wave" of relays over the network.
glm::vec4 generationColor(int gen)
{
    switch (gen) {
        case 0:  return { 0.30f, 0.95f, 1.00f, 1.0f };   // cyan (originator)
        case 1:  return { 0.30f, 0.95f, 0.45f, 1.0f };   // green
        case 2:  return { 1.00f, 0.85f, 0.25f, 1.0f };   // yellow
        case 3:  return { 1.00f, 0.60f, 0.20f, 1.0f };   // orange
        default: return { 1.00f, 0.30f, 0.20f, 1.0f };   // red
    }
}

} // namespace

void BroadcastSystem::tick(double now, simulation::VehiclePool& pool)
{
    constexpr double kRelayCooldown = 1.5; // seconds — prevent chain loops
    constexpr float  kRelayRadiusFactor = 0.75f; // each hop loses range

    // Snapshot size at the start because we may push new broadcasts (relays)
    // and we don't want to process them in the same tick.
    const std::size_t snapshotEnd = m_active.size();

    for (std::size_t i = 0; i < snapshotEnd; /* manual */) {
        auto& b = m_active[i];
        const float age    = static_cast<float>(now - b.startTime);
        const float radius = age * kSignalSpeed;

        if (radius > b.maxRadius) {
            // Erase: swap-with-back to stay within the snapshot window cleanly.
            // (Order doesn't matter for visualization.)
            const bool isLastInSnapshot = (i + 1 == snapshotEnd);
            m_active[i] = std::move(m_active.back());
            m_active.pop_back();
            if (!isLastInSnapshot && i < m_active.size()) {
                // Adjust snapshotEnd because we removed an element from inside it.
                // But snapshotEnd was 'snapshotEnd'; we have one fewer entry there.
                // Keep going at the same index without incrementing.
            }
            // snapshotEnd decreases since one snapshot entry was removed.
            // We don't track it precisely — loop until i hits the new
            // snapshotEnd, which we recompute on the fly:
            if (i >= m_active.size()) break;
            continue;
        }

        // Wavefront crossing detection.
        const float r2     = radius * radius;
        const float prev   = std::max(0.0f, radius - kSignalSpeed * (1.0f / 30.0f));
        const float prev2  = prev * prev;

        for (auto& v : pool.vehicles()) {
            const float dx = v.position.x - b.origin.x;
            const float dy = v.position.y - b.origin.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 <= r2 && d2 > prev2) {
                v.lastBroadcastReceivedAt = now;
                ++v.packetsReceived;

                // Multi-hop relay: if this vehicle hasn't relayed recently
                // and we're under the hop budget, emit a new broadcast from
                // its position with generation+1.
                if (b.generation + 1 <= b.maxGeneration &&
                    now - v.lastRelayedAt > kRelayCooldown)
                {
                    v.lastRelayedAt = now;
                    ++v.packetsSent;
                    Broadcast relay;
                    relay.sourceId      = v.id;
                    relay.origin        = v.position;
                    relay.startTime     = now;
                    relay.maxRadius     = b.maxRadius * kRelayRadiusFactor;
                    relay.generation    = b.generation + 1;
                    relay.maxGeneration = b.maxGeneration;
                    relay.color         = generationColor(relay.generation);
                    m_active.push_back(relay);
                }
            }
        }
        ++i;
    }
}

} // namespace v2v::vnet
