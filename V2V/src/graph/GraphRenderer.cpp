#include "graph/GraphRenderer.h"
#include "graph/RoadGraph.h"
#include "graphics/Camera2D.h"
#include "utils/Log.h"

#include <glad/gl.h>

#include <cmath>
#include <vector>

namespace v2v::graph {

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
void main() {
    FragColor = vColor;
}
)";

struct V {
    float x, y;
    float r, g, b, a;
};

} // namespace

float GraphRenderer::roadWidthMeters(RoadType t)
{
    switch (t) {
        case RoadType::Motorway:     return 16.0f;
        case RoadType::Trunk:        return 14.0f;
        case RoadType::Primary:      return 12.0f;
        case RoadType::Secondary:    return 10.0f;
        case RoadType::Tertiary:     return  8.0f;
        case RoadType::Residential:  return  6.0f;
        case RoadType::Unclassified: return  6.0f;
        case RoadType::Service:      return  4.0f;
        case RoadType::Other:        return  3.0f;
    }
    return 4.0f;
}

glm::vec4 GraphRenderer::roadColor(RoadType t)
{
    switch (t) {
        case RoadType::Motorway:     return { 0.95f, 0.40f, 0.30f, 0.95f };
        case RoadType::Trunk:        return { 0.95f, 0.55f, 0.30f, 0.95f };
        case RoadType::Primary:      return { 0.98f, 0.75f, 0.35f, 0.95f };
        case RoadType::Secondary:    return { 1.00f, 0.90f, 0.45f, 0.92f };
        case RoadType::Tertiary:     return { 1.00f, 0.98f, 0.65f, 0.88f };
        case RoadType::Residential:  return { 0.92f, 0.92f, 0.92f, 0.80f };
        case RoadType::Unclassified: return { 0.85f, 0.85f, 0.85f, 0.75f };
        case RoadType::Service:      return { 0.78f, 0.78f, 0.78f, 0.70f };
        case RoadType::Other:        return { 0.65f, 0.65f, 0.65f, 0.60f };
    }
    return { 0.7f, 0.7f, 0.7f, 0.7f };
}

GraphRenderer::~GraphRenderer() { destroy(); }

bool GraphRenderer::init(const std::string& shaderDir)
{
    if (m_initialized) return true;

    bool ok = false;
    if (!shaderDir.empty()) {
        ok = m_shader.loadFromFiles(shaderDir + "/graph.vert",
                                    shaderDir + "/graph.frag");
        if (!ok) {
            V2V_LOG_WARN("GraphRenderer: failed to load shaders from '%s' — using embedded fallback.",
                         shaderDir.c_str());
        }
    }
    if (!ok) ok = m_shader.loadFromSource(kVertSrc, kFragSrc);
    if (!ok) {
        V2V_LOG_ERROR("GraphRenderer: shader compile/link failed");
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

void GraphRenderer::destroy()
{
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    m_vertexCount = 0;
    m_initialized = false;
}

void GraphRenderer::buildGeometry(const RoadGraph& graph)
{
    if (!m_initialized) {
        V2V_LOG_ERROR("GraphRenderer::buildGeometry called before init()");
        return;
    }

    // Pre-size: 6 vertices per edge.
    std::vector<V> verts;
    verts.reserve(graph.edgeCount() * 6);

    for (const auto& e : graph.edges()) {
        const auto& a = graph.node(e.from).world;
        const auto& b = graph.node(e.to).world;

        const double dx = b.x - a.x;
        const double dy = b.y - a.y;
        const double len = std::sqrt(dx * dx + dy * dy);
        if (len <= 1e-9) continue;

        const double nx = -dy / len;   // perpendicular, unit length
        const double ny =  dx / len;

        const double halfW = 0.5 * static_cast<double>(roadWidthMeters(e.type));
        const glm::vec4 c  = roadColor(e.type);

        // Four corners (axis-aligned rectangle along the edge).
        const V v0{ static_cast<float>(a.x + nx * halfW),
                    static_cast<float>(a.y + ny * halfW), c.r, c.g, c.b, c.a };
        const V v1{ static_cast<float>(a.x - nx * halfW),
                    static_cast<float>(a.y - ny * halfW), c.r, c.g, c.b, c.a };
        const V v2{ static_cast<float>(b.x - nx * halfW),
                    static_cast<float>(b.y - ny * halfW), c.r, c.g, c.b, c.a };
        const V v3{ static_cast<float>(b.x + nx * halfW),
                    static_cast<float>(b.y + ny * halfW), c.r, c.g, c.b, c.a };

        // Two triangles: (v0, v1, v2) and (v0, v2, v3)
        verts.push_back(v0); verts.push_back(v1); verts.push_back(v2);
        verts.push_back(v0); verts.push_back(v2); verts.push_back(v3);
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(V)),
                 verts.data(),
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_vertexCount = verts.size();
    V2V_LOG_INFO("GraphRenderer: baked %zu edges into %zu vertices (~%.1f MB)",
                 graph.edgeCount(),
                 m_vertexCount,
                 (verts.size() * sizeof(V)) / (1024.0 * 1024.0));
}

void GraphRenderer::render(const graphics::Camera2D& camera)
{
    if (!m_initialized || m_vertexCount == 0) return;

    m_shader.bind();
    m_shader.setMat4("uViewProjection", camera.viewProjection());

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_vertexCount));
    glBindVertexArray(0);
}

} // namespace v2v::graph
