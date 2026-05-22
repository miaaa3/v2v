#pragma once

#include "graph/RoadGraph.h"

#include <cstdint>
#include <vector>

namespace v2v::simulation {

/**
 * A* pathfinder over a RoadGraph.
 *
 * The cost minimized is travel *time* (seconds): edge cost = length /
 * (maxspeed in m/s). The heuristic is euclidean distance divided by the
 * highest reasonable speed in the network (motorway), which keeps it
 * admissible (never over-estimates) so the result is a true shortest path.
 *
 * Internal buffers (gScore, came-from, visit generation) are kept across
 * calls and reused, so the only per-call allocation is the priority queue
 * scratch space.
 */
class PathFinder {
public:
    /// Find a path from `from` to `to`. On success, `outPath` is filled with
    /// the sequence of edges (starting at `from`, ending at `to`) and the
    /// function returns true. On failure, `outPath` is cleared.
    bool findPath(const graph::RoadGraph& g,
                  graph::NodeIndex from,
                  graph::NodeIndex to,
                  std::vector<graph::EdgeIndex>& outPath);

    /// Per-call statistics from the most recent search.
    struct Stats {
        std::uint32_t expandedNodes = 0;
        std::uint32_t pathEdges     = 0;
        bool          found         = false;
    };
    Stats lastStats() const { return m_stats; }

    /// Max speed used in the admissible heuristic (m/s). Default = motorway
    /// limit (130 km/h ≈ 36.1 m/s); raise if your graph contains higher.
    void setHeuristicMaxSpeed(float metersPerSecond) {
        if (metersPerSecond > 0.0f) m_hMaxSpeed = metersPerSecond;
    }

private:
    // Reusable scratch buffers, sized to graph.nodeCount() on first call.
    std::vector<float>            m_gScore;     // best g-score per node
    std::vector<graph::EdgeIndex> m_cameFrom;   // edge that produced best g-score
    std::vector<std::uint32_t>    m_visitGen;   // generation marker per node
    std::uint32_t                 m_curGen     = 0;
    float                         m_hMaxSpeed  = 130.0f / 3.6f;
    Stats                         m_stats{};

    void resetScratch(std::size_t nNodes);
};

} // namespace v2v::simulation
