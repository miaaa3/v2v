#pragma once

#include "graphics/Shader.h"

#include <cstdint>
#include <string>

namespace v2v::graphics { class Camera2D; }

namespace v2v::vnet {

class BroadcastSystem;

/**
 * Draws each active broadcast as an expanding ring (line loop of 64 segments).
 *
 * Geometry is regenerated CPU-side every frame and uploaded to a single
 * dynamic VBO. One `glDrawArrays(GL_LINES, ...)` covers all broadcasts.
 *
 * Alpha fades linearly from full to zero across the ring's lifetime so the
 * wavefront looks like a soft pulse rather than a hard outline.
 */
class BroadcastRenderer {
public:
    BroadcastRenderer() = default;
    ~BroadcastRenderer();

    BroadcastRenderer(const BroadcastRenderer&)            = delete;
    BroadcastRenderer& operator=(const BroadcastRenderer&) = delete;

    bool init(const std::string& shaderDir = {});

    void render(const graphics::Camera2D& camera,
                const BroadcastSystem&    sys,
                double                    now);

    std::size_t lastDrawSegments() const { return m_lastSegments; }

private:
    bool             m_initialized = false;
    graphics::Shader m_shader;
    std::uint32_t    m_vao            = 0;
    std::uint32_t    m_vbo            = 0;
    std::size_t      m_vboCapVerts    = 0;
    std::size_t      m_lastSegments   = 0;

    void destroy();
};

} // namespace v2v::vnet
