#include "v2v/BroadcastRenderer.h"
#include "graphics/Camera2D.h"
#include "utils/Log.h"
#include "v2v/Broadcast.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <cmath>
#include <vector>

namespace v2v::vnet {

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

constexpr int kRingSegments = 64;

struct V {
    float x, y;
    float r, g, b, a;
};

} // namespace

BroadcastRenderer::~BroadcastRenderer() { destroy(); }

void BroadcastRenderer::destroy()
{
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    m_vboCapVerts  = 0;
    m_lastSegments = 0;
    m_initialized  = false;
}

bool BroadcastRenderer::init(const std::string&)
{
    if (m_initialized) return true;
    if (!m_shader.loadFromSource(kVertSrc, kFragSrc)) {
        V2V_LOG_ERROR("BroadcastRenderer: shader compile/link failed");
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

void BroadcastRenderer::render(const graphics::Camera2D& camera,
                               const BroadcastSystem&    sys,
                               double                    now)
{
    if (!m_initialized) return;
    const auto& list = sys.active();
    if (list.empty()) { m_lastSegments = 0; return; }

    static thread_local std::vector<V> verts;
    verts.clear();
    verts.reserve(list.size() * kRingSegments * 2);

    for (const auto& b : list) {
        const float age    = static_cast<float>(now - b.startTime);
        const float radius = age * BroadcastSystem::kSignalSpeed;
        if (radius <= 0.0f || radius > b.maxRadius) continue;

        const float lifeFrac = radius / b.maxRadius;          // 0..1
        const float alpha    = (1.0f - lifeFrac) * b.color.a;
        const glm::vec4 col  = { b.color.r, b.color.g, b.color.b, alpha };

        const float dAng = 6.28318530718f / static_cast<float>(kRingSegments);
        float a0 = 0.0f;
        for (int k = 0; k < kRingSegments; ++k) {
            const float a1 = a0 + dAng;
            const float x0 = b.origin.x + radius * std::cos(a0);
            const float y0 = b.origin.y + radius * std::sin(a0);
            const float x1 = b.origin.x + radius * std::cos(a1);
            const float y1 = b.origin.y + radius * std::sin(a1);
            verts.push_back(V{ x0, y0, col.r, col.g, col.b, col.a });
            verts.push_back(V{ x1, y1, col.r, col.g, col.b, col.a });
            a0 = a1;
        }
    }

    m_lastSegments = verts.size() / 2;
    if (verts.empty()) return;

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    const GLsizeiptr bytes = static_cast<GLsizeiptr>(verts.size() * sizeof(V));
    if (verts.size() > m_vboCapVerts) {
        glBufferData(GL_ARRAY_BUFFER, bytes, verts.data(), GL_DYNAMIC_DRAW);
        m_vboCapVerts = verts.size();
    } else {
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(m_vboCapVerts * sizeof(V)),
                     nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, verts.data());
    }

    m_shader.bind();
    m_shader.setMat4("uViewProjection", camera.viewProjection());

    glBindVertexArray(m_vao);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(verts.size()));
    glBindVertexArray(0);
}

} // namespace v2v::vnet
