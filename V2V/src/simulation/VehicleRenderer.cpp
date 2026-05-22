#include "simulation/VehicleRenderer.h"
#include "graphics/Camera2D.h"
#include "simulation/Vehicle.h"
#include "simulation/VehiclePool.h"
#include "simulation/VehicleType.h"
#include "utils/Log.h"

#include <glad/gl.h>

#include <vector>

namespace v2v::simulation {

namespace {

constexpr const char* kVertSrc = R"(#version 330 core
layout(location = 0) in vec2  aLocal;
layout(location = 1) in vec2  aInstancePos;
layout(location = 2) in float aInstanceHeading;
layout(location = 3) in vec4  aInstanceColor;
layout(location = 4) in vec2  aInstanceSize;
uniform mat4  uViewProjection;
uniform float uSizeScale;
out vec4 vColor;
void main() {
    vec2 scaled = aLocal * aInstanceSize * uSizeScale;
    float cs = cos(aInstanceHeading);
    float sn = sin(aInstanceHeading);
    vec2 rotated = vec2(scaled.x * cs - scaled.y * sn,
                        scaled.x * sn + scaled.y * cs);
    vColor = aInstanceColor;
    gl_Position = uViewProjection * vec4(aInstancePos + rotated, 0.0, 1.0);
}
)";

constexpr const char* kFragSrc = R"(#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main() { FragColor = vColor; }
)";

struct Instance {
    glm::vec2 pos;          //   8 B
    float     heading;      //   4 B
    glm::vec4 color;        //  16 B
    glm::vec2 size;         //   8 B   = 36 B total
};
static_assert(sizeof(Instance) == 36,
              "Instance layout unexpectedly padded — vertex attribute offsets would be wrong.");

} // namespace

VehicleRenderer::~VehicleRenderer() { destroy(); }

void VehicleRenderer::destroy()
{
    if (m_instanceVbo) { glDeleteBuffers(1, &m_instanceVbo); m_instanceVbo = 0; }
    if (m_staticVbo)   { glDeleteBuffers(1, &m_staticVbo);   m_staticVbo   = 0; }
    if (m_vao)         { glDeleteVertexArrays(1, &m_vao);    m_vao         = 0; }
    m_instanceVboCap = 0;
    m_lastDrawCount  = 0;
    m_initialized    = false;
}

bool VehicleRenderer::init(const std::string& shaderDir)
{
    if (m_initialized) return true;

    bool ok = false;
    if (!shaderDir.empty()) {
        ok = m_shader.loadFromFiles(shaderDir + "/vehicle.vert",
                                    shaderDir + "/vehicle.frag");
        if (!ok) {
            V2V_LOG_WARN("VehicleRenderer: failed to load shaders from '%s' — using embedded fallback.",
                         shaderDir.c_str());
        }
    }
    if (!ok) ok = m_shader.loadFromSource(kVertSrc, kFragSrc);
    if (!ok) {
        V2V_LOG_ERROR("VehicleRenderer: shader compile/link failed");
        return false;
    }

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    // Static unit triangle.
    const float triangle[6] = {
         0.5f,  0.0f,
        -0.5f, -0.5f,
        -0.5f,  0.5f,
    };
    glGenBuffers(1, &m_staticVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_staticVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);

    // Dynamic per-instance VBO with 4 attributes.
    glGenBuffers(1, &m_instanceVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Instance),
                          reinterpret_cast<void*>(offsetof(Instance, pos)));
    glVertexAttribDivisor(1, 1);

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Instance),
                          reinterpret_cast<void*>(offsetof(Instance, heading)));
    glVertexAttribDivisor(2, 1);

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Instance),
                          reinterpret_cast<void*>(offsetof(Instance, color)));
    glVertexAttribDivisor(3, 1);

    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(Instance),
                          reinterpret_cast<void*>(offsetof(Instance, size)));
    glVertexAttribDivisor(4, 1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_initialized = true;
    return true;
}

void VehicleRenderer::render(const graphics::Camera2D& camera, const VehiclePool& pool)
{
    if (!m_initialized) return;
    const std::size_t n = pool.size();
    m_lastDrawCount = n;
    if (n == 0) return;

    static thread_local std::vector<Instance> buf;
    buf.clear();
    buf.reserve(n);
    for (const auto& v : pool) {
        const auto& tinfo = typeInfo(v.type);
        Instance inst;
        inst.pos     = v.position;
        inst.heading = v.heading;
        inst.color   = tinfo.color;
        inst.size    = { tinfo.lengthMeters, tinfo.widthMeters };
        buf.push_back(inst);
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    const GLsizeiptr bytes = static_cast<GLsizeiptr>(buf.size() * sizeof(Instance));
    if (buf.size() > m_instanceVboCap) {
        glBufferData(GL_ARRAY_BUFFER, bytes, buf.data(), GL_DYNAMIC_DRAW);
        m_instanceVboCap = buf.size();
    } else {
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(m_instanceVboCap * sizeof(Instance)),
                     nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, buf.data());
    }

    m_shader.bind();
    m_shader.setMat4 ("uViewProjection", camera.viewProjection());
    m_shader.setFloat("uSizeScale",      m_globalScale);

    glBindVertexArray(m_vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 3, static_cast<GLsizei>(n));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

} // namespace v2v::simulation
