#include "simulation/VehicleRenderer.h"
#include "graphics/Camera2D.h"
#include "simulation/Vehicle.h"
#include "simulation/VehiclePool.h"
#include "simulation/VehicleType.h"
#include "utils/Log.h"

#include <glad/gl.h>

#include <algorithm>
#include <vector>

namespace v2v::simulation {

namespace {

// Embedded fallback — kept byte-for-byte in sync with shaders/vehicle.{vert,frag}.
constexpr const char* kVertSrc = R"(#version 330 core
layout(location = 0) in vec2  aLocal;
layout(location = 1) in vec2  aInstancePos;
layout(location = 2) in float aInstanceHeading;
layout(location = 3) in vec4  aInstanceColor;
layout(location = 4) in vec2  aInstanceSize;
layout(location = 5) in vec4  aInstanceFx;
uniform mat4  uViewProjection;
uniform float uSizeScale;
out vec2 vLocal;
out vec4 vColor;
out vec4 vFx;
void main() {
    vec2  scaled = aLocal * aInstanceSize * uSizeScale;
    float cs = cos(aInstanceHeading);
    float sn = sin(aInstanceHeading);
    vec2  rotated = vec2(scaled.x * cs - scaled.y * sn,
                         scaled.x * sn + scaled.y * cs);
    vLocal = aLocal;
    vColor = aInstanceColor;
    vFx    = aInstanceFx;
    gl_Position = uViewProjection * vec4(aInstancePos + rotated, 0.0, 1.0);
}
)";

constexpr const char* kFragSrc = R"(#version 330 core
in vec2 vLocal;
in vec4 vColor;
in vec4 vFx;
out vec4 FragColor;
uniform float uTime;
uniform float uNight;
float spot(vec2 p, vec2 c, float r) {
    return 1.0 - smoothstep(0.0, r, length(p - c));
}
void main() {
    vec2  p   = vLocal;
    vec3  col = vColor.rgb;
    float a   = vColor.a;
    col *= 0.82 + 0.30 * smoothstep(-0.5, 0.5, p.x);
    float head = smoothstep(0.06, 0.5, p.x);
    head *= head;
    col += vec3(1.0, 0.93, 0.70) * head * (0.12 + 0.85 * uNight);
    float brake = clamp(vFx.x, 0.0, 1.0);
    float tail  = spot(p, vec2(-0.40,  0.30), 0.17)
                + spot(p, vec2(-0.40, -0.30), 0.17);
    col += vec3(1.0, 0.06, 0.04) * tail * (0.28 + 0.72 * brake);
    if (vFx.y > 0.5) {
        float ph    = fract(uTime * 2.2);
        float isRed = step(ph, 0.5);
        vec3  beac  = mix(vec3(0.15, 0.40, 1.0), vec3(1.0, 0.10, 0.10), isRed);
        col += beac * spot(p, vec2(0.0, 0.0), 0.34) * 1.5;
    }
    float glow = clamp(vFx.z, 0.0, 1.0);
    if (glow > 0.001) {
        float pulse = 0.55 + 0.45 * sin(uTime * 7.0);
        col += vec3(0.25, 0.78, 1.0) * glow * pulse;
        a    = max(a, 0.5 + 0.5 * glow);
    }
    FragColor = vec4(clamp(col, 0.0, 1.0), a);
}
)";

// Per-instance GPU record. Attributes 1..5; see init() for the layout.
struct Instance {
    glm::vec2 pos;          //   8 B
    float     heading;      //   4 B
    glm::vec4 color;        //  16 B
    glm::vec2 size;         //   8 B
    glm::vec4 fx;           //  16 B   = 52 B total
};
static_assert(sizeof(Instance) == 52,
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

    // Static unit triangle (arrow): tip at +X, base at -X.
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

    // Dynamic per-instance VBO with 5 attributes.
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

    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(Instance),
                          reinterpret_cast<void*>(offsetof(Instance, fx)));
    glVertexAttribDivisor(5, 1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_initialized = true;
    return true;
}

void VehicleRenderer::render(const graphics::Camera2D& camera,
                             const VehiclePool& pool,
                             float timeSeconds, float nightFactor)
{
    if (!m_initialized) return;
    const std::size_t n = pool.size();
    m_lastDrawCount = n;
    if (n == 0) return;

    // ---- Build the per-instance buffer (incl. Step-2 effect channel) ----
    static thread_local std::vector<Instance> buf;
    buf.clear();
    buf.reserve(n);

    const double now         = static_cast<double>(timeSeconds);
    constexpr double kGlowWindow = 1.5;   // seconds a V2V glow lingers

    for (const auto& v : pool) {
        const auto& tinfo = typeInfo(v.type);
        Instance inst;
        inst.pos     = v.position;
        inst.heading = v.heading;
        inst.color   = tinfo.color;
        inst.size    = { tinfo.lengthMeters, tinfo.widthMeters };

        // Brake intensity — the vehicle is decelerating (acceleration < 0).
        const float brake = std::clamp(-v.acceleration / 3.0f, 0.0f, 1.0f);

        // V2V glow — fades out over kGlowWindow once a broadcast touches the
        // vehicle (whether it received or relayed it).
        float glow = 0.0f;
        const double last = std::max(v.lastBroadcastReceivedAt, v.lastRelayedAt);
        if (last >= 0.0) {
            const double age = now - last;
            if (age >= 0.0 && age < kGlowWindow)
                glow = static_cast<float>(1.0 - age / kGlowWindow);
        }

        inst.fx = { brake, tinfo.emergency ? 1.0f : 0.0f, glow, 0.0f };
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

    // The body pass needs alpha blending (glow / light bloom).
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_shader.bind();
    m_shader.setMat4 ("uViewProjection", camera.viewProjection());
    m_shader.setFloat("uSizeScale",      m_globalScale);
    m_shader.setFloat("uTime",           timeSeconds);
    m_shader.setFloat("uNight",          std::clamp(nightFactor, 0.0f, 1.0f));

    glBindVertexArray(m_vao);

    // Vehicle bodies with head / brake / emergency / V2V effects.
    glDrawArraysInstanced(GL_TRIANGLES, 0, 3, static_cast<GLsizei>(n));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

} // namespace v2v::simulation
