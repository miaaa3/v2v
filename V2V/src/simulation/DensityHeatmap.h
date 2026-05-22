#pragma once

#include "simulation/VehiclePool.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <vector>

namespace v2v::graphics { class Camera2D; class Renderer; }

namespace v2v::simulation {

/**
 * Uniform grid of per-cell vehicle counts, drawn as a translucent colored
 * overlay (blue → red) below the vehicle layer.
 *
 * Each `update(dt, pool)` accumulates counts (with light temporal smoothing
 * so the heatmap doesn't strobe at high time-scale). `render` reads the
 * smoothed counts and draws one quad per non-empty cell, sized to the cell.
 *
 * Cell size and grid extent are set once by `rebuild(min, max, cellSize)` —
 * usually called once with the graph world bounds. Default cell size: 150 m.
 */
class DensityHeatmap {
public:
    void  rebuild(glm::vec2 worldMin, glm::vec2 worldMax, float cellSize);

    void  setEnabled(bool e)         { m_enabled = e; }
    bool  isEnabled() const          { return m_enabled; }

    /// Visual gain (multiplies counts before colormap). Default 1.0.
    void  setIntensity(float g)      { if (g > 0.05f) m_intensity = g; }
    float intensity() const          { return m_intensity; }

    /// Per-frame accumulation. `dt` is real seconds (used for smoothing).
    void  update(float dt, const VehiclePool& pool);

    /// Draw with the supplied primitives renderer. `camera` is used both for
    /// the view-projection transform and for view-frustum culling.
    void  render(graphics::Renderer& renderer,
                 const graphics::Camera2D& camera) const;

    int   cols() const { return m_cols; }
    int   rows() const { return m_rows; }
    float cellSize() const { return m_cellSize; }
    float maxCount() const { return m_maxCount; }

private:
    bool                 m_enabled    = false;
    float                m_intensity  = 1.0f;
    float                m_cellSize   = 150.0f;
    glm::vec2            m_origin     {0.0f, 0.0f};
    int                  m_cols       = 0;
    int                  m_rows       = 0;
    float                m_maxCount   = 0.0f;
    std::vector<float>   m_counts;   // smoothed, size = cols*rows
    std::vector<float>   m_scratch;  // fresh counts per frame

    inline int idx(int cx, int cy) const { return cy * m_cols + cx; }
};

} // namespace v2v::simulation
