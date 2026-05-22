#include "utils/RainRenderer.h"
#include "graphics/Camera2D.h"
#include "utils/Log.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <random>
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

} // namespace

RainRenderer::~RainRenderer() { destroy(); }

void RainRenderer::destroy()
{
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    m_drops.clear();
    m_initialized = false;
}

bool RainRenderer::init(const std::string&)
{
    if (m_initialized) return true;

    if (!m_shader.loadFromSource(kVertSrc, kFragSrc)) {
        V2V_LOG_ERROR("RainRenderer: shader compile/link failed");
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

    setCapacity(1500);
    m_initialized = true;
    return true;
}

void RainRenderer::setCapacity(std::size_t n)
{
    m_drops.resize(n);
    std::mt19937 rng(0xCAFEBABEu);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    for (auto& d : m_drops) {
        d.pos    = { u01(rng) * 1000.0f - 500.0f,
                     u01(rng) * 1000.0f - 500.0f };
        // Strong downward velocity, small horizontal wind.
        d.vel    = { -3.0f + 6.0f * u01(rng), -180.0f - 60.0f * u01(rng) };
        d.length = 6.0f + 6.0f * u01(rng);
    }
}

void RainRenderer::update(const graphics::Camera2D& camera, float dt)
{
    if (m_drops.empty() || dt <= 0.0f) return;

    // Bounding rect in world space: a bit larger than the viewport so wrapping
    // never shows hard edges.
    const float ppm  = std::max(camera.zoom(), 1e-6f);
    const float halfW = static_cast<float>(camera.viewportWidth())  * 0.5f / ppm * 1.2f;
    const float halfH = static_cast<float>(camera.viewportHeight()) * 0.5f / ppm * 1.2f;
    const glm::vec2 c = camera.position();
    const float L = c.x - halfW, R = c.x + halfW;
    const float B = c.y - halfH, T = c.y + halfH;

    for (auto& d : m_drops) {
        d.pos += d.vel * dt;
        if (d.pos.y < B) {
            // Wrap to the top of the camera box, randomize x.
            d.pos.y = T;
            d.pos.x = L + (R - L) * (std::sin(d.pos.x * 12.9898f) * 0.5f + 0.5f);
        }
        // Horizontal wrap.
        if (d.pos.x < L) d.pos.x += (R - L);
        if (d.pos.x > R) d.pos.x -= (R - L);
        // Re-center if the camera moved far away (e.g. follow-jump).
        if (std::abs(d.pos.x - c.x) > halfW * 1.5f) d.pos.x = c.x + (d.pos.x > c.x ? -halfW : halfW);
        if (std::abs(d.pos.y - c.y) > halfH * 1.5f) d.pos.y = c.y + (d.pos.y > c.y ? -halfH : halfH);
    }
}

void RainRenderer::render(const graphics::Camera2D& camera, float intensity)
{
    if (!m_initialized || m_drops.empty()) { m_lastDrawn = 0; return; }
    intensity = std::clamp(intensity, 0.0f, 1.0f);
    if (intensity <= 0.005f)                  { m_lastDrawn = 0; return; }

    const std::size_t n = static_cast<std::size_t>(
        static_cast<float>(m_drops.size()) * intensity);
    if (n == 0)                               { m_lastDrawn = 0; return; }

    static thread_local std::vector<V> verts;
    verts.clear();
    verts.reserve(n * 2);

    const float alpha = 0.25f + 0.35f * intensity;
    const glm::vec4 col{0.65f, 0.80f, 1.00f, alpha};

    for (std::size_t i = 0; i < n; ++i) {
        const auto& d = m_drops[i];
        // Tail of the streak — along the velocity direction, scaled by `length`.
        const glm::vec2 dir = d.vel * (d.length / std::max(1.0f, glm::length(d.vel)));
        verts.push_back(V{ d.pos.x,         d.pos.y,         col.r, col.g, col.b, col.a });
        verts.push_back(V{ d.pos.x + dir.x, d.pos.y + dir.y, col.r, col.g, col.b, 0.0f });
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(V)),
                 verts.data(),
                 GL_DYNAMIC_DRAW);

    m_shader.bind();
    m_shader.setMat4("uViewProjection", camera.viewProjection());

    glBindVertexArray(m_vao);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(verts.size()));
    glBindVertexArray(0);

    m_lastDrawn = n;
}

} // namespace v2v::utils
