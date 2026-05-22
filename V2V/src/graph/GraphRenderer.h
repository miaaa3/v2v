#pragma once

#include "graphics/Shader.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace v2v::graphics { class Camera2D; }

namespace v2v::graph {

class RoadGraph;
enum class RoadType : std::uint8_t;

/**
 * Renders a RoadGraph as colored, thick triangle strips in world space.
 *
 * The geometry is baked once when `buildGeometry()` is called (typically right
 * after parsing). It is *not* updated dynamically — the road graph is static
 * for the lifetime of the simulation.
 *
 * Each edge becomes two triangles (six vertices, no indexing) with a
 * world-meter width that depends on RoadType. Per-vertex RGBA is also baked,
 * so the renderer issues a single draw call per frame.
 */
class GraphRenderer {
public:
    GraphRenderer() = default;
    ~GraphRenderer();

    GraphRenderer(const GraphRenderer&)            = delete;
    GraphRenderer& operator=(const GraphRenderer&) = delete;

    bool init(const std::string& shaderDir = {});

    /// Rebuild GPU geometry from `graph`. Safe to call multiple times.
    void buildGeometry(const RoadGraph& graph);

    /// Draw the baked geometry with the given camera.
    void render(const graphics::Camera2D& camera);

    std::size_t vertexCount() const { return m_vertexCount; }

    // Visual tuning per road type ------------------------------------------
    static float        roadWidthMeters(RoadType t);
    static glm::vec4    roadColor      (RoadType t);

private:
    bool          m_initialized = false;
    graphics::Shader m_shader;
    std::uint32_t m_vao = 0;
    std::uint32_t m_vbo = 0;
    std::size_t   m_vertexCount = 0;

    void destroy();
};

} // namespace v2v::graph
