#pragma once

#include "graphics/Shader.h"

#include <cstdint>
#include <string>

namespace v2v::graphics { class Camera2D; }

namespace v2v::utils {

class SpatialGrid;

/**
 * Renders one filled, semi-transparent quad per non-empty SpatialGrid cell,
 * colored by its density (green sparse → yellow medium → red congested).
 *
 * The geometry is rebuilt every frame from `grid.forEachCell()`; for the
 * 95×71 grid used in La Rochelle (a few thousand non-empty cells at peak
 * density), that's a small fraction of a millisecond.
 */
class HeatmapRenderer {
public:
    HeatmapRenderer() = default;
    ~HeatmapRenderer();

    HeatmapRenderer(const HeatmapRenderer&)            = delete;
    HeatmapRenderer& operator=(const HeatmapRenderer&) = delete;

    bool init(const std::string& shaderDir = {});

    /// Draw the heatmap. `normMax` is the bucket size that maps to "fully red";
    /// pass `grid.maxBucketSize()` for an auto-stretched look, or a fixed
    /// value (e.g. 40) for a stable visual reference between frames.
    void render(const graphics::Camera2D& camera,
                const SpatialGrid&        grid,
                std::size_t               normMax);

    std::size_t lastDrawnCells() const { return m_lastCells; }

private:
    bool             m_initialized = false;
    graphics::Shader m_shader;
    std::uint32_t    m_vao         = 0;
    std::uint32_t    m_vbo         = 0;
    std::size_t      m_vboCap      = 0;
    std::size_t      m_lastCells   = 0;

    void destroy();
};

} // namespace v2v::utils
