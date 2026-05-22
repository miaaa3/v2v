#include "utils/SpatialGrid.h"

#include <algorithm>
#include <cmath>

namespace v2v::utils {

int SpatialGrid::cellX(float x) const
{
    const int c = static_cast<int>(std::floor((x - m_origin.x) / m_cellSize));
    return std::clamp(c, 0, std::max(0, m_cols - 1));
}

int SpatialGrid::cellY(float y) const
{
    const int c = static_cast<int>(std::floor((y - m_origin.y) / m_cellSize));
    return std::clamp(c, 0, std::max(0, m_rows - 1));
}

void SpatialGrid::rebuild(glm::vec2 worldMin, glm::vec2 worldMax, float cellSize)
{
    if (cellSize <= 0.0f) cellSize = 1.0f;
    if (worldMax.x <= worldMin.x) worldMax.x = worldMin.x + cellSize;
    if (worldMax.y <= worldMin.y) worldMax.y = worldMin.y + cellSize;

    m_origin   = worldMin;
    m_cellSize = cellSize;
    m_cols     = std::max(1, static_cast<int>(std::ceil((worldMax.x - worldMin.x) / cellSize)));
    m_rows     = std::max(1, static_cast<int>(std::ceil((worldMax.y - worldMin.y) / cellSize)));

    m_cells.assign(static_cast<std::size_t>(m_cols) * m_rows, {});
}

void SpatialGrid::clear()
{
    for (auto& bucket : m_cells) bucket.clear();
}

void SpatialGrid::insert(std::uint32_t id, glm::vec2 pos)
{
    if (m_cells.empty()) return;
    const int cx = cellX(pos.x);
    const int cy = cellY(pos.y);
    m_cells[static_cast<std::size_t>(cy) * m_cols + cx].push_back({ id, pos });
}

void SpatialGrid::queryRadius(glm::vec2 center, float radius,
                              std::vector<std::uint32_t>& outIds) const
{
    if (m_cells.empty() || radius <= 0.0f) return;

    const float r2 = radius * radius;
    const int cMinX = std::clamp(cellX(center.x - radius), 0, m_cols - 1);
    const int cMaxX = std::clamp(cellX(center.x + radius), 0, m_cols - 1);
    const int cMinY = std::clamp(cellY(center.y - radius), 0, m_rows - 1);
    const int cMaxY = std::clamp(cellY(center.y + radius), 0, m_rows - 1);

    for (int cy = cMinY; cy <= cMaxY; ++cy) {
        for (int cx = cMinX; cx <= cMaxX; ++cx) {
            const auto& bucket = m_cells[static_cast<std::size_t>(cy) * m_cols + cx];
            for (const auto& e : bucket) {
                const float dx = e.pos.x - center.x;
                const float dy = e.pos.y - center.y;
                if (dx * dx + dy * dy <= r2) outIds.push_back(e.id);
            }
        }
    }
}

void SpatialGrid::queryAllPairs(
    float radius,
    std::vector<std::pair<std::uint32_t, std::uint32_t>>& outPairs) const
{
    if (m_cells.empty() || radius <= 0.0f) return;
    const float r2 = radius * radius;

    // We iterate cells linearly. For each cell, we test:
    //   - pairs within the same cell (i<j)
    //   - pairs against the 4 "forward" neighbors (right, down, down-left,
    //     down-right) — this visits every neighbor pair exactly once.
    for (int cy = 0; cy < m_rows; ++cy) {
        for (int cx = 0; cx < m_cols; ++cx) {
            const auto& A = m_cells[static_cast<std::size_t>(cy) * m_cols + cx];
            if (A.empty()) continue;

            // Same-cell pairs
            for (std::size_t i = 0; i < A.size(); ++i) {
                for (std::size_t j = i + 1; j < A.size(); ++j) {
                    const float dx = A[i].pos.x - A[j].pos.x;
                    const float dy = A[i].pos.y - A[j].pos.y;
                    if (dx * dx + dy * dy <= r2) {
                        auto a = A[i].id, b = A[j].id;
                        if (a > b) std::swap(a, b);
                        outPairs.emplace_back(a, b);
                    }
                }
            }

            // Forward neighbors: (cx+1, cy), (cx-1, cy+1), (cx, cy+1), (cx+1, cy+1)
            const int neighborsDX[4] = { +1, -1,  0, +1 };
            const int neighborsDY[4] = {  0, +1, +1, +1 };
            for (int k = 0; k < 4; ++k) {
                const int nx = cx + neighborsDX[k];
                const int ny = cy + neighborsDY[k];
                if (nx < 0 || ny < 0 || nx >= m_cols || ny >= m_rows) continue;
                const auto& B = m_cells[static_cast<std::size_t>(ny) * m_cols + nx];
                if (B.empty()) continue;
                for (const auto& a : A) {
                    for (const auto& b : B) {
                        const float dx = a.pos.x - b.pos.x;
                        const float dy = a.pos.y - b.pos.y;
                        if (dx * dx + dy * dy <= r2) {
                            auto i = a.id, j = b.id;
                            if (i > j) std::swap(i, j);
                            outPairs.emplace_back(i, j);
                        }
                    }
                }
            }
        }
    }
}

std::size_t SpatialGrid::entryCount() const
{
    std::size_t n = 0;
    for (const auto& b : m_cells) n += b.size();
    return n;
}

std::size_t SpatialGrid::maxBucketSize() const
{
    std::size_t m = 0;
    for (const auto& b : m_cells) m = std::max(m, b.size());
    return m;
}

} // namespace v2v::utils
