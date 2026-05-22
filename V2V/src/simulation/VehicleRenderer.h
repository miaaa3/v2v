#pragma once

#include "graphics/Shader.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace v2v::graphics { class Camera2D; }
namespace v2v::simulation { class VehiclePool; }

namespace v2v::simulation {

/**
 * Instanced renderer for vehicles.
 *
 * GPU layout:
 *   - VBO 0 (static):  3 vertices of a unit triangle, attrib 0.
 *   - VBO 1 (dynamic): N instances of
 *         { vec2 pos, float heading, vec4 color, vec2 size }
 *     bound to attribs 1..4 with divisor=1.
 *
 * Vehicle-specific size (length, width in meters) is per-instance so each
 * vehicle type renders with its own footprint. A global `uSizeScale` uniform
 * lets `main` apply a camera-zoom-dependent multiplier so vehicles stay
 * visible at any zoom level.
 */
class VehicleRenderer {
public:
    VehicleRenderer() = default;
    ~VehicleRenderer();

    VehicleRenderer(const VehicleRenderer&)            = delete;
    VehicleRenderer& operator=(const VehicleRenderer&) = delete;

    bool init(const std::string& shaderDir = {});

    /// Upload current pool state and draw it.
    void render(const graphics::Camera2D& camera, const VehiclePool& pool);

    /// Global zoom-dependent scale factor (default 1.0).
    void setGlobalScale(float s) { m_globalScale = s; }

    std::size_t lastDrawInstanceCount() const { return m_lastDrawCount; }

private:
    bool             m_initialized      = false;
    graphics::Shader m_shader;

    std::uint32_t    m_vao              = 0;
    std::uint32_t    m_staticVbo        = 0;
    std::uint32_t    m_instanceVbo      = 0;
    std::size_t     m_instanceVboCap   = 0;
    std::size_t     m_lastDrawCount    = 0;

    float            m_globalScale      = 1.0f;

    void destroy();
};

} // namespace v2v::simulation
