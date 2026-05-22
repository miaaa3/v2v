#include "graph/RoadGraph.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace v2v::graph {

const char* roadTypeName(RoadType t)
{
    switch (t) {
        case RoadType::Motorway:     return "motorway";
        case RoadType::Trunk:        return "trunk";
        case RoadType::Primary:      return "primary";
        case RoadType::Secondary:    return "secondary";
        case RoadType::Tertiary:     return "tertiary";
        case RoadType::Residential:  return "residential";
        case RoadType::Unclassified: return "unclassified";
        case RoadType::Service:      return "service";
        case RoadType::Other:        return "other";
    }
    return "other";
}

RoadType parseRoadType(std::string_view v)
{
    // OSM `highway=*` values; suffixes like `_link` map to the parent type.
    auto starts = [&](std::string_view prefix) {
        return v.size() >= prefix.size() && v.substr(0, prefix.size()) == prefix;
    };

    if (starts("motorway"))     return RoadType::Motorway;
    if (starts("trunk"))        return RoadType::Trunk;
    if (starts("primary"))      return RoadType::Primary;
    if (starts("secondary"))    return RoadType::Secondary;
    if (starts("tertiary"))     return RoadType::Tertiary;
    if (v == "residential" || v == "living_street") return RoadType::Residential;
    if (v == "unclassified") return RoadType::Unclassified;
    if (v == "service")      return RoadType::Service;
    return RoadType::Other;
}

float RoadGraph::defaultMaxSpeed(RoadType t)
{
    switch (t) {
        case RoadType::Motorway:     return 130.0f;
        case RoadType::Trunk:        return 110.0f;
        case RoadType::Primary:      return  80.0f;
        case RoadType::Secondary:    return  60.0f;
        case RoadType::Tertiary:     return  50.0f;
        case RoadType::Residential:  return  30.0f;
        case RoadType::Unclassified: return  40.0f;
        case RoadType::Service:      return  20.0f;
        case RoadType::Other:        return  20.0f;
    }
    return 30.0f;
}

NodeIndex RoadGraph::addNode(glm::dvec2 latLon, glm::dvec2 world)
{
    m_nodes.push_back({ latLon, world });
    m_adjacencyValid = false;
    return static_cast<NodeIndex>(m_nodes.size() - 1);
}

EdgeIndex RoadGraph::addEdge(NodeIndex from, NodeIndex to,
                             RoadType type, bool oneway, float maxspeedKmh)
{
    Edge e;
    e.from     = from;
    e.to       = to;
    e.type     = type;
    e.oneway   = oneway;
    e.maxspeed = (maxspeedKmh > 0.0f) ? maxspeedKmh : defaultMaxSpeed(type);

    const glm::dvec2 d = m_nodes[to].world - m_nodes[from].world;
    e.length   = static_cast<float>(std::sqrt(d.x * d.x + d.y * d.y));

    m_edges.push_back(e);
    m_adjacencyValid = false;
    return static_cast<EdgeIndex>(m_edges.size() - 1);
}

void RoadGraph::clear()
{
    m_nodes.clear();
    m_edges.clear();
    m_adjacency.clear();
    m_adjacencyValid = false;
}

void RoadGraph::buildAdjacency() const
{
    m_adjacency.assign(m_nodes.size(), {});
    for (EdgeIndex i = 0; i < m_edges.size(); ++i) {
        m_adjacency[m_edges[i].from].push_back(i);
    }
    m_adjacencyValid = true;
}

const std::vector<EdgeIndex>& RoadGraph::edgesFrom(NodeIndex n) const
{
    if (!m_adjacencyValid) buildAdjacency();
    return m_adjacency[n];
}

glm::dvec4 RoadGraph::worldBounds() const
{
    if (m_nodes.empty()) return {0.0, 0.0, 0.0, 0.0};
    double xmin =  std::numeric_limits<double>::infinity();
    double ymin =  std::numeric_limits<double>::infinity();
    double xmax = -std::numeric_limits<double>::infinity();
    double ymax = -std::numeric_limits<double>::infinity();
    for (const auto& n : m_nodes) {
        xmin = std::min(xmin, n.world.x);
        ymin = std::min(ymin, n.world.y);
        xmax = std::max(xmax, n.world.x);
        ymax = std::max(ymax, n.world.y);
    }
    return { xmin, ymin, xmax, ymax };
}

} // namespace v2v::graph
