#include "simulation/TrafficLightSystem.h"

#include <algorithm>
#include <unordered_map>

namespace v2v::simulation {

void TrafficLightSystem::build(const graph::RoadGraph& graph)
{
    m_intersections.clear();
    m_edgeToIntersection.clear();

    // 1. Bucket incoming edges by their destination node.
    std::unordered_map<graph::NodeIndex, std::vector<graph::EdgeIndex>> incoming;
    incoming.reserve(graph.nodeCount() / 4 + 1);
    const auto& edges = graph.edges();
    for (graph::EdgeIndex ei = 0; ei < edges.size(); ++ei) {
        incoming[edges[ei].to].push_back(ei);
    }

    // 2. Keep only nodes with degree >= 3 (true intersections).
    m_intersections.reserve(incoming.size() / 4 + 1);
    for (auto& [node, ins] : incoming) {
        if (ins.size() < 3) continue;
        Intersection it;
        it.node          = node;
        it.incomingEdges = std::move(ins);
        it.world         = graph.node(node).world;
        // Light phase: stagger initial timer to avoid synchronized blinking.
        it.timer  = static_cast<float>((node * 1009u) % 7000u) / 1000.0f;
        it.phase  = static_cast<int>(node % it.incomingEdges.size());
        const std::uint32_t idx = static_cast<std::uint32_t>(m_intersections.size());
        m_intersections.push_back(std::move(it));
        for (auto e : m_intersections.back().incomingEdges) {
            m_edgeToIntersection[e] = idx;
        }
    }
}

void TrafficLightSystem::update(float scaledDt)
{
    if (!m_enabled) return;
    if (scaledDt <= 0.0f) return;

    for (auto& it : m_intersections) {
        it.timer += scaledDt;
        if (it.timer >= m_phaseDuration) {
            it.timer = 0.0f;
            it.phase = (it.phase + 1) % static_cast<int>(it.incomingEdges.size());
        }
    }
}

bool TrafficLightSystem::isGreen(graph::EdgeIndex incomingEdge) const
{
    if (!m_enabled) return true;
    auto it = m_edgeToIntersection.find(incomingEdge);
    if (it == m_edgeToIntersection.end()) return true;
    const auto& X = m_intersections[it->second];
    // The green incoming is X.incomingEdges[X.phase].
    return X.incomingEdges[static_cast<std::size_t>(X.phase)] == incomingEdge;
}

void TrafficLightSystem::resync()
{
    for (auto& it : m_intersections) {
        it.timer = 0.0f;
        it.phase = 0;
    }
}

} // namespace v2v::simulation
