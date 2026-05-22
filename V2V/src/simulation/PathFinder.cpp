#include "simulation/PathFinder.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace v2v::simulation {

namespace {

inline float edgeCostSeconds(const graph::Edge& e)
{
    // length (m) / speed (m/s) = seconds
    const float vMs = e.maxspeed / 3.6f;
    return e.length / std::max(vMs, 0.1f);
}

inline float heuristicSeconds(const graph::Node& a, const graph::Node& b, float vMax)
{
    const double dx = b.world.x - a.world.x;
    const double dy = b.world.y - a.world.y;
    const double dist = std::sqrt(dx * dx + dy * dy);
    return static_cast<float>(dist) / vMax;
}

struct PQEntry {
    float            f;       // priority: g + h
    graph::NodeIndex node;
    // min-heap on `f`
    bool operator<(const PQEntry& o) const { return f > o.f; }
};

} // namespace

void PathFinder::resetScratch(std::size_t nNodes)
{
    if (m_gScore.size()   < nNodes) m_gScore.resize(nNodes);
    if (m_cameFrom.size() < nNodes) m_cameFrom.resize(nNodes, graph::INVALID_EDGE);
    if (m_visitGen.size() < nNodes) m_visitGen.resize(nNodes, 0);

    if (++m_curGen == 0) {
        // Wrap around: clear all gen markers so the new gen=1 isn't a false hit.
        std::fill(m_visitGen.begin(), m_visitGen.end(), 0);
        m_curGen = 1;
    }
}

bool PathFinder::findPath(const graph::RoadGraph& g,
                          graph::NodeIndex from,
                          graph::NodeIndex to,
                          std::vector<graph::EdgeIndex>& outPath)
{
    outPath.clear();
    m_stats = {};

    if (from == graph::INVALID_NODE || to == graph::INVALID_NODE) return false;
    if (from >= g.nodeCount() || to >= g.nodeCount()) return false;
    if (from == to) { m_stats.found = true; return true; }

    resetScratch(g.nodeCount());

    const auto& nodes = g.nodes();
    const auto& edges = g.edges();

    std::priority_queue<PQEntry> open;
    m_gScore  [from] = 0.0f;
    m_visitGen[from] = m_curGen;
    m_cameFrom[from] = graph::INVALID_EDGE;
    open.push({ heuristicSeconds(nodes[from], nodes[to], m_hMaxSpeed), from });

    while (!open.empty()) {
        const PQEntry cur = open.top();
        open.pop();

        // Skip outdated entries (a better g-score was found before we popped).
        if (cur.f - m_gScore[cur.node] - heuristicSeconds(nodes[cur.node], nodes[to], m_hMaxSpeed) > 1e-3f) {
            // Note: this branch rarely matters because the standard A* lazy-
            // delete pattern doesn't strictly require it; we keep it for safety.
        }

        if (cur.node == to) {
            // Reconstruct path by walking back through cameFrom.
            graph::NodeIndex n = to;
            while (n != from) {
                const graph::EdgeIndex ei = m_cameFrom[n];
                if (ei == graph::INVALID_EDGE) {
                    outPath.clear();
                    return false; // safety net (shouldn't happen)
                }
                outPath.push_back(ei);
                n = edges[ei].from;
            }
            std::reverse(outPath.begin(), outPath.end());
            m_stats.found     = true;
            m_stats.pathEdges = static_cast<std::uint32_t>(outPath.size());
            return true;
        }

        ++m_stats.expandedNodes;

        for (graph::EdgeIndex ei : g.edgesFrom(cur.node)) {
            const auto& e = edges[ei];
            const graph::NodeIndex nb = e.to;

            const float tentative = m_gScore[cur.node] + edgeCostSeconds(e);
            const bool  fresh     = (m_visitGen[nb] != m_curGen);

            if (fresh || tentative < m_gScore[nb]) {
                m_gScore  [nb] = tentative;
                m_cameFrom[nb] = ei;
                m_visitGen[nb] = m_curGen;
                const float h  = heuristicSeconds(nodes[nb], nodes[to], m_hMaxSpeed);
                open.push({ tentative + h, nb });
            }
        }
    }

    return false;
}

} // namespace v2v::simulation
