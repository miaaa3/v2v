#pragma once

#include "graph/RoadGraph.h"
#include "simulation/VehicleType.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace v2v::simulation {

using VehicleId = std::uint32_t;

/**
 * Per-vehicle state.
 *
 * Position is recomputed each step from the current edge and `edgeProgress`,
 * so the canonical state is (currentEdge, edgeProgress, heading); `position`
 * is a cached value that downstream renderers / V2V detection read.
 *
 * `path` is a list of upcoming edges to traverse; `pathPos` is the index of
 * the *next* edge to enter (`path[pathPos]`). When `pathPos == path.size()`,
 * the path is exhausted and a fresh A* request is queued (or random walk).
 */
struct Vehicle {
    glm::vec2 position { 0.0f, 0.0f };  // world meters (cached)
    float     heading  = 0.0f;          // radians, 0 = +X
    float     speed    = 0.0f;          // m/s (current)

    graph::EdgeIndex currentEdge  = graph::INVALID_EDGE;
    float            edgeProgress = 0.0f;   // 0..1 along the current edge

    std::vector<graph::EdgeIndex> path;
    std::size_t                   pathPos = 0;

    graph::NodeIndex destination = graph::INVALID_NODE;

    float       transmissionRange = 200.0f;  // meters, V2V comm radius
    VehicleId   id   = 0;
    VehicleType type = VehicleType::Car;

    /// Distance to the closest vehicle in front on the same edge, in meters
    /// (set by `SimulationEngine::computeFrontGaps`). INFINITY when there is
    /// nobody ahead. The `step()` loop uses this to throttle speed and avoid
    /// vehicles passing through each other.
    float       frontGap = 1e9f;

    /// Wall-clock time (seconds since simulation start) of the last incoming
    /// V2V broadcast that reached this vehicle. Used to draw a brief glow on
    /// recipients of a "Send signal" command. -1 = never received.
    double      lastBroadcastReceivedAt = -1.0;

    /// If positive, the vehicle is wrecked (frozen as an obstacle) until this
    /// wall-clock time. Wrecked vehicles do not move, force followers to queue
    /// behind them, and are rendered with a red X marker (see AccidentSystem).
    /// -1 = never wrecked.
    double      wreckedUntil = -1.0;

    /// If positive, this civilian vehicle is currently yielding to a nearby
    /// emergency vehicle (extra slowdown). Drawn with a yellow halo by the
    /// main render loop.
    double      yieldingUntil = 0.0;

    /// Wall-clock time of the last broadcast this vehicle *relayed*. Used as
    /// a cooldown so multi-hop messages don't bounce back and forth between
    /// the same neighbours.
    double      lastRelayedAt = -1.0;

    // ---------- Telemetry (for the rich Selected-Vehicle inspector) -------
    /// Cumulative meters since spawn. Updated every step.
    float       distanceTraveledM = 0.0f;

    /// `SimulationEngine::now()` at which this vehicle was created.
    double      spawnTime         = 0.0;

    /// Last-tick speed (m/s) — kept so the engine can derive `acceleration`.
    float       prevSpeed         = 0.0f;

    /// Current acceleration in m/s² (signed; negative under braking).
    float       acceleration      = 0.0f;

    /// V2V packet counters (incremented by BroadcastSystem).
    std::uint32_t packetsSent     = 0;
    std::uint32_t packetsReceived = 0;
};

} // namespace v2v::simulation
