#pragma once

#include "graphics/Shader.h"

#include <cstdint>
#include <string>

namespace v2v::graphics   { class Camera2D; }
namespace v2v::simulation { class VehiclePool; }

namespace v2v::vnet {

class V2VNetwork;

/**
 * Draws all current V2V links as GL_LINES segments in world space.
 *
 * For each `Link`, two vertices are emitted (endpoints in world meters), each
 * with the color produced by `RadioModel::colorFor(strength)`. One draw call
 * per frame; geometry is rebuilt CPU-side and uploaded to a dynamic VBO.
 *
 * `glLineWidth > 1` is not honored on macOS Core, so all lines are 1 px wide.
 * For thicker visuals, swap GL_LINES for the same thick-triangle technique
 * used by GraphRenderer.
 */
class LinkRenderer {
public:
    LinkRenderer() = default;
    ~LinkRenderer();

    LinkRenderer(const LinkRenderer&)            = delete;
    LinkRenderer& operator=(const LinkRenderer&) = delete;

    bool init(const std::string& shaderDir = {});

    /// Reads `net.links()`, looks vehicle positions up in `pool`, draws.
    void render(const graphics::Camera2D&        camera,
                const V2VNetwork&                net,
                const simulation::VehiclePool&   pool);

    std::size_t lastDrawLinkCount() const { return m_lastLinkCount; }

private:
    bool          m_initialized = false;
    graphics::Shader m_shader;
    std::uint32_t m_vao = 0;
    std::uint32_t m_vbo = 0;
    std::size_t   m_vboCapacityVerts = 0;
    std::size_t   m_lastLinkCount    = 0;

    void destroy();
};

} // namespace v2v::vnet
