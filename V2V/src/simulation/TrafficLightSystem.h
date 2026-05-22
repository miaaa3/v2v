#pragma once

#include "graph/RoadGraph.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace v2v::simulation {

/**
 * Cyclic traffic-light controller for graph intersections.
 *
 * After `build()`:
 *   - Only nodes with $\geq 3$ incoming edges are tracked (true intersections
 *     in OSM-typical road networks).
 *   - Each intersection has an ordered list of incoming edges; one is "green"
 *     at a time, cycling every `phaseDuration` seconds.
 *
 * `isGreen(edge)` returns true when the given incoming edge is allowed to
 * pass through its destination node (or true if the destination isn't a
 * managed intersection, i.e. simple through-route).
 */
class TrafficLightSystem {
public:
    struct Intersection {
        graph::NodeIndex                   node;
        std::vector<graph::EdgeIndex>      incomingEdges;   // ordered
        glm::dvec2                         world;           // for rendering
        int                                phase     = 0;    // index of green edge
        float                              timer     = 0.0f; // seconds in current phase
    };

    void  build(const graph::RoadGraph& graph);
    void  update(float scaledDt);

    /// True if the incoming edge currently has a green light at its `to` node,
    /// or true for non-intersection nodes. Vehicles use this to decide whether
    /// to stop just before the end of the edge.
    bool  isGreen(graph::EdgeIndex incomingEdge) const;

    /// Reset all phase timers to 0. Used by the UI "synchronize" button.
    void  resync();

    void  setEnabled(bool e)          { m_enabled = e; }
    bool  isEnabled() const           { return m_enabled; }
    void  setPhaseDuration(float s)   { if (s > 0.5f) m_phaseDuration = s; }
    float phaseDuration() const       { return m_phaseDuration; }

    const std::vector<Intersection>& intersections() const { return m_intersections; }
    std::size_t                      count() const { return m_intersections.size(); }

private:
    bool                                                m_enabled       = true;
    float                                               m_phaseDuration = 7.0f;
    std::vector<Intersection>                           m_intersections;
    std::unordered_map<graph::EdgeIndex, std::uint32_t> m_edgeToIntersection; // edgeIdx -> index in m_intersections (so we can answer isGreen in O(1))
};

} // namespace v2v::simulation
