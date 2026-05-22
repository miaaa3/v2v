#include "v2v/V2VNetwork.h"
#include "v2v/RadioModel.h"

#include <algorithm>
#include <cmath>

namespace v2v::vnet {

void V2VNetwork::update(const simulation::VehiclePool& pool,
                        const utils::SpatialGrid&       grid)
{
    m_links.clear();
    m_pairScratch.clear();

    if (pool.empty()) return;

    grid.queryAllPairs(m_maxRange, m_pairScratch);
    m_links.reserve(m_pairScratch.size());

    for (const auto& [a, b] : m_pairScratch) {
        if (a >= pool.size() || b >= pool.size()) continue;

        const auto& va = pool[a];
        const auto& vb = pool[b];

        const float dx = va.position.x - vb.position.x;
        const float dy = va.position.y - vb.position.y;
        const float d  = std::sqrt(dx * dx + dy * dy);

        const float effRange = std::min(va.transmissionRange, vb.transmissionRange);
        if (d > effRange) continue;

        const float s = RadioModel::strength(d, effRange);
        if (s <= 0.0f) continue;

        m_links.push_back(Link{ a, b, d, s });
    }
}

float V2VNetwork::averageStrength() const
{
    if (m_links.empty()) return 0.0f;
    double sum = 0.0;
    for (const auto& l : m_links) sum += l.strength;
    return static_cast<float>(sum / static_cast<double>(m_links.size()));
}

} // namespace v2v::vnet
