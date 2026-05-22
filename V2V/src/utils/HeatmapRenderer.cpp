#include "utils/HeatmapRenderer.h"
#include "graphics/Camera2D.h"
#include "utils/Log.h"
#include "utils/SpatialGrid.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace v2v::utils {

namespace {

constexpr const char* kVertSrc = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 uViewProjection;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = uViewProjection * vec4(aPos, 0.0, 1.0);
}
)";

constexpr const char* kFragSrc = R"(#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main() { FragColor = vColor; }
)";

struct V {
    float x, y;
    float r, g, b, a;
};

/// Map a normalized density `t` ∈ [0,1] to a green→yellow→red color with
/// transparency growing with density.
inline glm::vec4 densityColor(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    glm::vec3 rgb;
    if (t < 0.5f) {
        const float u = t * 2.0f;
        rgb = { u, 1.0f, 0.0f };          // green → yellow
    } else {
        const float u = (t - 0.5f) * 2.0f;
        rgb = { 1.0f, 1.0f - u, 0.0f };   // yellow → red
    }
    const float alpha = 0.10f + 0.45f * t;
    return { rgb.r, rgb.g, rgb.b, alpha };
}

} // namespace

HeatmapRenderer::~HeatmapRenderer() { destroy(); }

void HeatmapRenderer::destroy()
{
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    m_vboCap = 0;
    m_lastCells = 0;
    m_initialized = false;
}

bool HeatmapRenderer::init(const std::string&)
{
    if (m_initialized) return true;
    if (!m_shader.loadFromSource(kVertSrc, kFragSrc)) {
        V2V_LOG_ERROR("HeatmapRenderer: shader compile/link failed");
        return false;
    }
    glGenVertexArrays(1, &m_vao);
    glGenBuffers     (1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(V),
                          reinterpret_cast<void*>(offsetof(V, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(V),
                          reinterpret_cast<void*>(offsetof(V, r)));
    glBindVertexArray(0);
    m_initialized = true;
    return true;
}

void HeatmapRenderer::render(const graphics::Camera2D& camera,
                             const SpatialGrid&        grid,
                             std::size_t               normMax)
{
    if (!m_initialized) return;
    if (grid.cols() == 0 || grid.rows() == 0) { m_lastCells = 0; return; }

    static thread_local std::vector<V> verts;
    verts.clear();
    verts.reserve(128);

    const float half = grid.cellSize() * 0.5f;
    const float denom = std::max<float>(1.0f, static_cast<float>(normMax));

    grid.forEachCell([&](int /*cx*/, int /*cy*/, std::size_t count, glm::vec2 center)
    {
        const float t = static_cast<float>(count) / denom;
        const glm::vec4 c = densityColor(t);
        // Two triangles for a cell quad.
        const V v0{ center.x - half, center.y - half, c.r, c.g, c.b, c.a };
        const V v1{ center.x + half, center.y - half, c.r, c.g, c.b, c.a };
        const V v2{ center.x + half, center.y + half, c.r, c.g, c.b, c.a };
        const V v3{ center.x - half, center.y + half, c.r, c.g, c.b, c.a };
        verts.push_back(v0); verts.push_back(v1); verts.push_back(v2);
        verts.push_back(v0); verts.push_back(v2); verts.push_back(v3);
    });

    m_lastCells = verts.size() / 6;
    if (verts.empty()) return;

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    const GLsizeiptr bytes = static_cast<GLsizeiptr>(verts.size() * sizeof(V));
    if (verts.size() > m_vboCap) {
        glBufferData(GL_ARRAY_BUFFER, bytes, verts.data(), GL_DYNAMIC_DRAW);
        m_vboCap = verts.size();
    } else {
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(m_vboCap * sizeof(V)),
                     nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, verts.data());
    }

    m_shader.bind();
    m_shader.setMat4("uViewProjection", camera.viewProjection());

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
    glBindVertexArray(0);
}

} // namespace v2v::utils
