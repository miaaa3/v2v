#pragma once

#include "graphics/Shader.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace v2v::graphics {

class Camera2D;

/**
 * Minimal 2D renderer.
 *
 * Owns the OpenGL state we need for now: GLAD loader, a unit-quad VAO/VBO,
 * and a basic shader (transform + flat color). Steps 3+ will add a tile
 * renderer, line/instanced renderers, etc. on top of this same context.
 *
 * Lifecycle:
 *   Renderer r;
 *   r.init();              // after a GL context is current
 *   for each frame:
 *     r.beginFrame(camera);
 *     r.clear(...);
 *     r.drawQuad(...);
 *     r.endFrame();
 */
class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    /// One-time setup. Returns false on failure (loader / shader compile).
    /// `shaderDir` is the directory containing basic.vert / basic.frag.
    /// If empty, falls back to embedded shader sources.
    bool init(const std::string& shaderDir = {});

    /// Begin a frame: binds shader and uploads the camera matrix.
    void beginFrame(const Camera2D& camera);

    /// End a frame (currently a no-op, exposed for symmetry / future batching).
    void endFrame();

    /// Set the viewport and clear color/depth.
    void setViewport(int x, int y, int width, int height);
    void clear(float r, float g, float b, float a);

    /// Filled, axis-aligned quad in world space, centered on `center`.
    void drawQuad(glm::vec2 center, glm::vec2 size, glm::vec4 color);

    bool isReady() const { return m_initialized; }

private:
    bool m_initialized = false;

    Shader        m_basicShader;
    std::uint32_t m_quadVao = 0;
    std::uint32_t m_quadVbo = 0;

    // Cached per-frame state (set in beginFrame, used in draw calls).
    glm::mat4 m_viewProjection{1.0f};

    void createQuadGeometry();
    void destroy();
};

} // namespace v2v::graphics
