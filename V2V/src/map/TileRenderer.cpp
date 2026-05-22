#include "map/TileRenderer.h"
#include "graphics/Camera2D.h"
#include "map/MapProjection.h"
#include "utils/Log.h"

#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace v2v::map {

namespace {

constexpr const char* kVertSrc = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uViewProjection;
uniform mat4 uModel;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = uViewProjection * uModel * vec4(aPos, 0.0, 1.0);
}
)";

constexpr const char* kFragSrc = R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTexture;
uniform vec4 uTint;
void main() {
    FragColor = texture(uTexture, vUV) * uTint;
}
)";

} // namespace

TileRenderer::~TileRenderer() { destroy(); }

bool TileRenderer::init(const std::string& shaderDir)
{
    if (m_initialized) return true;

    bool ok = false;
    if (!shaderDir.empty()) {
        ok = m_shader.loadFromFiles(shaderDir + "/tile.vert",
                                    shaderDir + "/tile.frag");
        if (!ok) {
            V2V_LOG_WARN("TileRenderer: failed to load shaders from '%s' — using embedded fallback.",
                         shaderDir.c_str());
        }
    }
    if (!ok) ok = m_shader.loadFromSource(kVertSrc, kFragSrc);
    if (!ok) {
        V2V_LOG_ERROR("TileRenderer: shader compilation failed");
        return false;
    }

    createGeometry();
    m_initialized = true;
    return true;
}

void TileRenderer::destroy()
{
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    m_initialized = false;
}

void TileRenderer::createGeometry()
{
    // Unit quad centered on (0,0); UV (0,0) at top-left so the OSM image
    // (which has its first pixel at the top-left) renders right-side up in
    // a Y-up world.
    //
    // Layout per vertex: x, y, u, v
    const float verts[] = {
        // pos          uv
        -0.5f, -0.5f,   0.0f, 1.0f,
         0.5f, -0.5f,   1.0f, 1.0f,
         0.5f,  0.5f,   1.0f, 0.0f,
        -0.5f, -0.5f,   0.0f, 1.0f,
         0.5f,  0.5f,   1.0f, 0.0f,
        -0.5f,  0.5f,   0.0f, 0.0f,
    };

    glGenVertexArrays(1, &m_vao);
    glGenBuffers     (1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));

    glBindVertexArray(0);
}

void TileRenderer::render(const graphics::Camera2D& camera,
                          TileCache&                cache,
                          TileLoader&               loader)
{
    if (!m_initialized) return;

    // 1. Upload any pending downloads.
    cache.pump(loader);

    // 2. Choose zoom level from camera's pixels-per-meter (Camera2D::zoom()).
    const double pxPerMeter = static_cast<double>(camera.zoom());
    const int    z          = MapProjection::chooseZoomLevel(pxPerMeter);
    m_lastZoom = z;

    const double tileSizeM  = MapProjection::tileSizeMeters(z);
    const int    tilesPerAx = MapProjection::tilesPerAxis(z);

    // 3. Compute visible world rect.
    const float halfW = static_cast<float>(camera.viewportWidth())  * 0.5f / camera.zoom();
    const float halfH = static_cast<float>(camera.viewportHeight()) * 0.5f / camera.zoom();
    const glm::vec2 cam = camera.position();

    const double worldLeft   = static_cast<double>(cam.x - halfW);
    const double worldRight  = static_cast<double>(cam.x + halfW);
    const double worldBottom = static_cast<double>(cam.y - halfH);
    const double worldTop    = static_cast<double>(cam.y + halfH);

    auto toTileX = [&](double wx) {
        return static_cast<int>(std::floor((wx + MapProjection::EARTH_HALF_M) / tileSizeM));
    };
    auto toTileY = [&](double wy) {
        return static_cast<int>(std::floor((MapProjection::EARTH_HALF_M - wy) / tileSizeM));
    };

    int xMin = std::clamp(toTileX(worldLeft),   0, tilesPerAx - 1);
    int xMax = std::clamp(toTileX(worldRight),  0, tilesPerAx - 1);
    int yMin = std::clamp(toTileY(worldTop),    0, tilesPerAx - 1);
    int yMax = std::clamp(toTileY(worldBottom), 0, tilesPerAx - 1);

    // 4. Bind shader / VAO and prepare common uniforms.
    m_shader.bind();
    m_shader.setMat4("uViewProjection", camera.viewProjection());
    m_shader.setVec4("uTint",           glm::vec4(1.0f));
    m_shader.setInt ("uTexture",        0);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(m_vao);

    // 5. Iterate, request loads, draw what's ready.
    std::size_t visibleCount = 0;
    int         requestsThisFrame = 0;

    // Iterate spirally-ish: center outward, so on-screen tiles closer to the
    // camera load first. Cheap version: just walk row-major; OSM is fast.
    for (int y = yMin; y <= yMax; ++y) {
        for (int x = xMin; x <= xMax; ++x) {
            ++visibleCount;
            TileKey k{ z, x, y };
            Tile* t = cache.getOrRequest(k, loader);
            if (!t) continue;

            if (t->status != TileStatus::Ready) {
                if (t->status == TileStatus::Pending) {
                    // Throttle outbound requests slightly.
                    if (requestsThisFrame >= m_maxRequestsPerFrame) continue;
                    ++requestsThisFrame;
                }
                continue;
            }

            const double tlX = -MapProjection::EARTH_HALF_M + x * tileSizeM;
            const double tlY =  MapProjection::EARTH_HALF_M - y * tileSizeM;
            const double cx  = tlX + tileSizeM * 0.5;
            const double cy  = tlY - tileSizeM * 0.5;

            glm::mat4 model = glm::translate(glm::mat4(1.0f),
                                             glm::vec3(static_cast<float>(cx),
                                                       static_cast<float>(cy), 0.0f));
            model = glm::scale(model,
                               glm::vec3(static_cast<float>(tileSizeM),
                                         static_cast<float>(tileSizeM), 1.0f));

            m_shader.setMat4("uModel", model);

            glBindTexture(GL_TEXTURE_2D, t->texture);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_lastVisibleCount = visibleCount;
}

} // namespace v2v::map
