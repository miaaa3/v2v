#pragma once

#include "graphics/Shader.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace v2v::graphics { class Camera2D; }

namespace v2v::utils {

/**
 * Procedural rain particle system rendered as GL_LINES segments in world space.
 *
 * Drops live in a virtual rectangle slightly larger than the camera viewport
 * and fall at a fixed velocity. When they exit the bottom or the sides they
 * wrap around at the top. The buffer is sized once via `setCapacity`.
 *
 * `intensity` (0..1) controls how many drops are actually rendered (linear
 * scale) so the renderer cost stays bounded.
 */
class RainRenderer {
public:
    RainRenderer() = default;
    ~RainRenderer();

    RainRenderer(const RainRenderer&)            = delete;
    RainRenderer& operator=(const RainRenderer&) = delete;

    bool init(const std::string& shaderDir = {});

    /// Allocate `n` particles. Re-initializes positions/velocities. Default
    /// 1500 drops, plenty visible at any reasonable framerate.
    void setCapacity(std::size_t n);

    /// Advance the particles. `dt` is real seconds.
    void update(const graphics::Camera2D& camera, float dt);

    /// Draw the active subset (intensity * capacity drops).
    void render(const graphics::Camera2D& camera, float intensity);

    std::size_t lastDrawnDrops() const { return m_lastDrawn; }

private:
    bool             m_initialized = false;
    graphics::Shader m_shader;
    std::uint32_t    m_vao = 0;
    std::uint32_t    m_vbo = 0;

    struct Drop {
        glm::vec2 pos;
        glm::vec2 vel;     // mostly -y, plus a small horizontal "wind"
        float     length;  // visual length in meters
    };
    std::vector<Drop> m_drops;
    std::size_t       m_lastDrawn = 0;

    void destroy();
};

} // namespace v2v::utils
