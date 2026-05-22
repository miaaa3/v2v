#pragma once

#include "graphics/Shader.h"
#include "map/TileCache.h"
#include "map/TileLoader.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace v2v::graphics { class Camera2D; }

namespace v2v::map {

/**
 * Draws OSM raster tiles as textured world-space quads.
 *
 * Per frame:
 *   1. Choose the OSM zoom level matching the camera's pixels/meter.
 *   2. Compute the rectangle of visible tile coordinates.
 *   3. For each visible tile, ask the cache (which triggers async loads).
 *   4. Render every Ready tile as a textured quad in EPSG:3857 meter space.
 *
 * Owns its own GL resources (shader + quad VAO with UVs).
 */
class TileRenderer {
public:
    TileRenderer() = default;
    ~TileRenderer();

    TileRenderer(const TileRenderer&)            = delete;
    TileRenderer& operator=(const TileRenderer&) = delete;

    /// Compile shaders and create GL geometry.
    bool init(const std::string& shaderDir = {});

    /// Pump tile loads and draw the visible tile set.
    void render(const graphics::Camera2D& camera,
                TileCache&                cache,
                TileLoader&               loader);

    /// Optional: clamp how many tiles can be requested per frame to be polite
    /// to the OSM tile server.
    void setMaxRequestsPerFrame(int n) { m_maxRequestsPerFrame = n; }

    std::size_t lastFrameVisibleTileCount() const { return m_lastVisibleCount; }
    int         lastFrameZoom()             const { return m_lastZoom;         }

private:
    bool          m_initialized = false;
    graphics::Shader m_shader;
    std::uint32_t m_vao = 0;
    std::uint32_t m_vbo = 0;

    int           m_maxRequestsPerFrame = 8;
    std::size_t   m_lastVisibleCount    = 0;
    int           m_lastZoom            = 0;

    void createGeometry();
    void destroy();
};

} // namespace v2v::map
