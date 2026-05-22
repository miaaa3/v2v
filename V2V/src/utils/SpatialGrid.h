#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace v2v::utils {

/**
 * Uniform 2D spatial hash grid for fast proximity queries.
 *
 * Designed to be rebuilt every frame (vehicle positions change). Cell size
 * should be ≳ the largest query radius so each query touches at most 3×3
 * cells. Memory cost is proportional to (worldArea / cellSize²).
 *
 * The grid stores `(id, pos)` entries. IDs are opaque uint32 handles supplied
 * by the caller (e.g. vehicle index).
 *
 * Typical use:
 *     SpatialGrid g;
 *     g.rebuild({xmin,ymin}, {xmax,ymax}, 500.0f);   // cellSize = 500m
 *     for (auto& v : pool) g.insert(v.id, v.position);
 *     g.queryRadius(p, 250.0f, out);
 */
class SpatialGrid {
public:
    struct Entry {
        std::uint32_t id;
        glm::vec2     pos;
    };

    void rebuild(glm::vec2 worldMin, glm::vec2 worldMax, float cellSize);
    void clear();

    void insert(std::uint32_t id, glm::vec2 pos);

    /// IDs whose recorded position is within `radius` of `center`. Output is
    /// appended (not cleared).
    void queryRadius(glm::vec2 center, float radius,
                     std::vector<std::uint32_t>& outIds) const;

    /// All unordered pairs (id_a < id_b) whose recorded positions are within
    /// `radius` of each other. Used by V2V link detection (step 7).
    void queryAllPairs(float radius,
                       std::vector<std::pair<std::uint32_t, std::uint32_t>>& outPairs) const;

    // Diagnostics ----------------------------------------------------------
    int  cols()      const { return m_cols; }
    int  rows()      const { return m_rows; }
    float cellSize() const { return m_cellSize; }
    glm::vec2 origin() const { return m_origin; }
    std::size_t entryCount() const;
    std::size_t maxBucketSize() const;

    /// Iterate all non-empty cells. Callback signature:
    ///   void(int cx, int cy, std::size_t count, glm::vec2 centerWorld)
    template <typename Fn>
    void forEachCell(Fn&& fn) const
    {
        for (int cy = 0; cy < m_rows; ++cy) {
            for (int cx = 0; cx < m_cols; ++cx) {
                const auto& b = m_cells[static_cast<std::size_t>(cy) * m_cols + cx];
                if (b.empty()) continue;
                const glm::vec2 center{
                    m_origin.x + (static_cast<float>(cx) + 0.5f) * m_cellSize,
                    m_origin.y + (static_cast<float>(cy) + 0.5f) * m_cellSize
                };
                fn(cx, cy, b.size(), center);
            }
        }
    }

private:
    glm::vec2 m_origin   {0.0f, 0.0f};
    float     m_cellSize = 1.0f;
    int       m_cols     = 0;
    int       m_rows     = 0;
    std::vector<std::vector<Entry>> m_cells;

    inline int cellX(float x) const;
    inline int cellY(float y) const;
};

} // namespace v2v::utils
