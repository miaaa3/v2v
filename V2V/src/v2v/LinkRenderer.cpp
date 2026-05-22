#include "v2v/LinkRenderer.h"
#include "graphics/Camera2D.h"
#include "simulation/VehiclePool.h"
#include "utils/Log.h"
#include "v2v/RadioModel.h"
#include "v2v/V2VNetwork.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

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

struct V {
    float x, y;
    float r, g, b, a;
};

} // namespace

LinkRenderer::~LinkRenderer() { destroy(); }

void LinkRenderer::destroy()
{
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    m_vboCapacityVerts = 0;
    m_lastLinkCount    = 0;
    m_initialized      = false;
}

bool LinkRenderer::init(const std::string& shaderDir)
{
    if (m_initialized) return true;

    bool ok = false;
    if (!shaderDir.empty()) {
        ok = m_shader.loadFromFiles(shaderDir + "/link.vert",
                                    shaderDir + "/link.frag");
        if (!ok) {
            V2V_LOG_WARN("LinkRenderer: failed to load shaders from '%s' — using embedded fallback.",
                         shaderDir.c_str());
        }
    }
    if (!ok) ok = m_shader.loadFromSource(kVertSrc, kFragSrc);
    if (!ok) {
        V2V_LOG_ERROR("LinkRenderer: shader compile/link failed");
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

void LinkRenderer::render(const graphics::Camera2D&      camera,
                          const V2VNetwork&              net,
                          const simulation::VehiclePool& pool)
{
    if (!m_initialized) return;
    const auto& links = net.links();
    m_lastLinkCount = links.size();
    if (links.empty()) return;

    static thread_local std::vector<V> verts;
    verts.clear();
    verts.reserve(links.size() * 2);

    for (const auto& l : links) {
        if (l.a >= pool.size() || l.b >= pool.size()) continue;
        const auto& pa = pool[l.a].position;
        const auto& pb = pool[l.b].position;
        const glm::vec4 c = RadioModel::colorFor(l.strength);

        verts.push_back(V{ pa.x, pa.y, c.r, c.g, c.b, c.a });
        verts.push_back(V{ pb.x, pb.y, c.r, c.g, c.b, c.a });
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    const GLsizeiptr bytes = static_cast<GLsizeiptr>(verts.size() * sizeof(V));
    if (verts.size() > m_vboCapacityVerts) {
        glBufferData(GL_ARRAY_BUFFER, bytes, verts.data(), GL_DYNAMIC_DRAW);
        m_vboCapacityVerts = verts.size();
    } else {
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(m_vboCapacityVerts * sizeof(V)),
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
