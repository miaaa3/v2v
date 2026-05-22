#pragma once

#include "simulation/VehiclePool.h"
#include "utils/SpatialGrid.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace v2v::vnet {

/**
 * Set of active V2V links between vehicles, recomputed each frame.
 *
 * Algorithm (called from `update`):
 *   1. Ask the SpatialGrid for *candidate* pairs within `m_maxRange`. The
 *      grid filters by spatial proximity in O(n) on average — no O(n²) scan.
 *   2. For each candidate (a, b), look up the vehicles in the pool. The
 *      effective transmission range for a link is min(range_a, range_b)
 *      (a link only exists if *both* peers can hear each other).
 *   3. If the distance is within the effective range, compute strength via
 *      `RadioModel::strength` and keep the link.
 *
 * The output is a flat vector of `Link`s, accessible to LinkRenderer.
 */
struct Link {
    std::uint32_t a;          // VehicleId (=index in pool)
    std::uint32_t b;
    float         distance;   // meters
    float         strength;   // [0, 1]
};

class V2VNetwork {
public:
    void  update(const simulation::VehiclePool& pool,
                 const utils::SpatialGrid&       grid);

    void  setMaxRange(float meters) { if (meters > 0.0f) m_maxRange = meters; }
    float maxRange()   const        { return m_maxRange; }

    const std::vector<Link>& links() const { return m_links; }
    std::size_t              linkCount() const { return m_links.size(); }

    /// Mean strength of currently active links, or 0 if none.
    float averageStrength() const;

private:
    float                                       m_maxRange = 500.0f;
    std::vector<Link>                           m_links;
    std::vector<std::pair<std::uint32_t,
                          std::uint32_t>>       m_pairScratch;
};

} // namespace v2v::vnet
