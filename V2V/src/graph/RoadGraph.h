#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace v2v::graph {

using NodeIndex = std::uint32_t;
using EdgeIndex = std::uint32_t;

inline constexpr NodeIndex INVALID_NODE = std::numeric_limits<NodeIndex>::max();
inline constexpr EdgeIndex INVALID_EDGE = std::numeric_limits<EdgeIndex>::max();

/**
 * Road classification, mirroring the OSM `highway=*` tag values that matter
 * for routing. Order is significant: it doubles as a priority where higher
 * values mean larger roads.
 */
enum class RoadType : std::uint8_t {
    Other        = 0,   ///< fallback (cycleways, footpaths, …)
    Service      = 1,
    Unclassified = 2,
    Residential  = 3,
    Tertiary     = 4,
    Secondary    = 5,
    Primary      = 6,
    Trunk        = 7,
    Motorway     = 8,
};

const char* roadTypeName(RoadType t);
RoadType    parseRoadType(std::string_view osmHighwayValue);

struct Node {
    glm::dvec2 latLon{0.0, 0.0};   ///< degrees
    glm::dvec2 world {0.0, 0.0};   ///< EPSG:3857 meters
};

struct Edge {
    NodeIndex from     = INVALID_NODE;
    NodeIndex to       = INVALID_NODE;
    RoadType  type     = RoadType::Residential;
    bool      oneway   = false;
    float     maxspeed = 50.0f;     ///< km/h, defaulted by type if unknown
    float     length   = 0.0f;      ///< world meters (cached)
};

/**
 * Compact directed road graph.
 *
 * Nodes and edges are stored as flat vectors indexed by NodeIndex / EdgeIndex
 * for cache friendliness. An adjacency list (per-node outgoing edges) is built
 * lazily via `buildAdjacency()` and invalidated by any mutation.
 *
 * The graph stores directed edges: when parsing a two-way OSM segment, the
 * parser inserts both directions.
 */
class RoadGraph {
public:
    // ---- mutation -----------------------------------------------------------
    NodeIndex addNode(glm::dvec2 latLon, glm::dvec2 world);

    /// Insert one directed edge. Returns its index. Updates the world-meter
    /// length from current node positions.
    EdgeIndex addEdge(NodeIndex from, NodeIndex to,
                      RoadType type, bool oneway, float maxspeedKmh);

    void clear();

    /// Compute adjacency list (called automatically by `edgesFrom`).
    void buildAdjacency() const;

    // ---- accessors ----------------------------------------------------------
    const std::vector<Node>& nodes() const { return m_nodes; }
    const std::vector<Edge>& edges() const { return m_edges; }

    std::size_t nodeCount() const { return m_nodes.size(); }
    std::size_t edgeCount() const { return m_edges.size(); }

    const Node& node(NodeIndex i) const { return m_nodes[i]; }
    const Edge& edge(EdgeIndex i) const { return m_edges[i]; }

    /// Outgoing edges from `n`. Triggers adjacency build on first call.
    const std::vector<EdgeIndex>& edgesFrom(NodeIndex n) const;

    /// (xmin, ymin, xmax, ymax) in world meters. Empty graph returns zeros.
    glm::dvec4 worldBounds() const;

    /// Default speed limit (km/h) for a road type, used when OSM has no maxspeed.
    static float defaultMaxSpeed(RoadType t);

private:
    std::vector<Node>                            m_nodes;
    std::vector<Edge>                            m_edges;
    mutable std::vector<std::vector<EdgeIndex>>  m_adjacency;
    mutable bool                                 m_adjacencyValid = false;
};

} // namespace v2v::graph
