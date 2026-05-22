#pragma once

#include "graph/RoadGraph.h"
#include "simulation/PathFinder.h"
#include "simulation/TrafficLightSystem.h"
#include "simulation/VehiclePool.h"

#include <cstdint>
#include <random>

namespace v2v::simulation {

/**
 * Owns the VehiclePool and advances all vehicles every `step(dt)`.
 *
 * Spawn:
 *   - `spawn(n)` places n vehicles on random edges with random destinations.
 *
 * Step (dt seconds, multiplied by `timeScale`):
 *   - For each vehicle, advance progress along its current edge.
 *   - When an edge completes, pick the next from the path; if the path is
 *     empty, do a random walk and queue an A* request to the destination
 *     (subject to per-frame budget).
 *   - Stuck vehicles (no outgoing edges from current node) respawn elsewhere.
 *
 * The engine has no GL state; it just mutates the pool. Rendering reads it.
 */
class SimulationEngine {
public:
    SimulationEngine(const graph::RoadGraph& graph, std::uint32_t rngSeed = 0xC0FFEEu);

    void spawn(std::size_t n);

    /// Remove the last `n` vehicles from the pool. Vehicle ids of surviving
    /// vehicles are unchanged (we erase from the tail).
    void despawn(std::size_t n);

    /// Advance simulation by `dt` real seconds (scaled by `timeScale`).
    void step(float dt);

    /// Tell the engine the current wall-clock time (seconds since program
    /// start). Used to honour `Vehicle::wreckedUntil` and `yieldingUntil`.
    /// Must be set every frame before `step()` when those features are used.
    void   setNow(double t)         { m_now = t; }
    double now() const              { return m_now; }

    void  setPaused(bool p)        { m_paused = p; }
    bool  isPaused() const         { return m_paused; }
    void  togglePaused()           { m_paused = !m_paused; }

    void  setTimeScale(float s)    { if (s > 0.0f) m_timeScale = s; }
    float timeScale() const        { return m_timeScale; }

    /// Cap A* searches performed per frame to control CPU spikes.
    void  setPathfindBudget(int n) { m_pathfindBudgetPerStep = n; }

    const VehiclePool&         pool()    const { return m_pool;    }
    VehiclePool&               pool()          { return m_pool;    }
    const graph::RoadGraph&    graph()   const { return m_graph;   }
    const TrafficLightSystem&  lights()  const { return m_lights;  }
    TrafficLightSystem&        lights()        { return m_lights;  }

    // Diagnostics
    std::size_t lastStepPathfinds()       const { return m_lastPathfinds; }
    std::size_t lastStepEdgeTransitions() const { return m_lastTransitions; }

private:
    const graph::RoadGraph& m_graph;
    VehiclePool             m_pool;
    PathFinder              m_finder;
    TrafficLightSystem      m_lights;

    std::mt19937            m_rng;
    bool                    m_paused                = false;
    float                   m_timeScale             = 1.0f;
    int                     m_pathfindBudgetPerStep = 32;
    double                  m_now                   = 0.0; // wall-clock seconds

    std::size_t             m_lastPathfinds   = 0;
    std::size_t             m_lastTransitions = 0;

    // Helpers
    Vehicle              makeRandomVehicle();
    graph::NodeIndex     pickRandomNode();
    graph::EdgeIndex     pickRandomOutgoing(graph::NodeIndex node,
                                            graph::EdgeIndex avoidEdge);
    void                 updatePosition(Vehicle& v);
    bool                 enterNextEdge(Vehicle& v, int& pathfindBudget);

    /// Per-edge sort + nearest-neighbour scan that fills `Vehicle::frontGap`.
    /// O(n log n) thanks to small per-edge buckets. Called once per step.
    void                 computeFrontGaps();
};

} // namespace v2v::simulation
