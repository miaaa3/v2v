#include "simulation/DensityHeatmap.h"

#include "graphics/Camera2D.h"
#include "graphics/Renderer.h"

#include <algorithm>
#include <cmath>

namespace v2v::simulation {

void DensityHeatmap::rebuild(glm::vec2 worldMin, glm::vec2 worldMax, float cellSize)
{
    if (cellSize < 5.0f) cellSize = 5.0f;
    m_cellSize = cellSize;
    m_origin   = worldMin;

    const float w = std::max(1.0f, worldMax.x - worldMin.x);
    const float h = std::max(1.0f, worldMax.y - worldMin.y);
    m_cols = std::max(1, static_cast<int>(std::ceil(w / cellSize)));
    m_rows = std::max(1, static_cast<int>(std::ceil(h / cellSize)));

    m_counts .assign(static_cast<std::size_t>(m_cols) * m_rows, 0.0f);
    m_scratch.assign(static_cast<std::size_t>(m_cols) * m_rows, 0.0f);
    m_maxCount = 0.0f;
}

void DensityHeatmap::update(float dt, const VehiclePool& pool)
{
    if (!m_enabled || m_counts.empty()) return;

    // Fresh counts.
    std::fill(m_scratch.begin(), m_scratch.end(), 0.0f);
    const float inv = 1.0f / m_cellSize;
    for (const auto& v : pool) {
        const int cx = static_cast<int>((v.position.x - m_origin.x) * inv);
        const int cy = static_cast<int>((v.position.y - m_origin.y) * inv);
        if (cx < 0 || cy < 0 || cx >= m_cols || cy >= m_rows) continue;
        m_scratch[idx(cx, cy)] += 1.0f;
    }

    // Temporal smoothing. Half-life ≈ 0.7s feels right: heatmap responds to
    // congestion forming but doesn't strobe at high time-scale.
    constexpr float kHalfLifeSec = 0.7f;
    const float alpha = (dt <= 0.0f) ? 1.0f
                      : (1.0f - std::pow(0.5f, dt / kHalfLifeSec));

    float maxC = 0.0f;
    for (std::size_t i = 0; i < m_counts.size(); ++i) {
        m_counts[i] += alpha * (m_scratch[i] - m_counts[i]);
        if (m_counts[i] > maxC) maxC = m_counts[i];
    }
    m_maxCount = maxC;
}

namespace {

/// Blue → cyan → yellow → red gradient with alpha rising with intensity.
glm::vec4 colormap(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    glm::vec3 c;
    if (t < 0.33f) {
        const float k = t / 0.33f;                              // blue → cyan
        c = glm::mix(glm::vec3(0.10f, 0.20f, 0.85f),
                     glm::vec3(0.10f, 0.85f, 0.95f), k);
    } else if (t < 0.66f) {
        const float k = (t - 0.33f) / 0.33f;                    // cyan → yellow
        c = glm::mix(glm::vec3(0.10f, 0.85f, 0.95f),
                     glm::vec3(0.95f, 0.90f, 0.20f), k);
    } else {
        const float k = (t - 0.66f) / 0.34f;                    // yellow → red
        c = glm::mix(glm::vec3(0.95f, 0.90f, 0.20f),
                     glm::vec3(0.95f, 0.20f, 0.20f), k);
    }
    // Alpha curve: invisible at the very low end, ~0.55 at saturation.
    const float a = std::clamp(0.10f + 0.55f * t, 0.0f, 0.65f);
    return { c.x, c.y, c.z, a };
}

} // namespace

void DensityHeatmap::render(graphics::Renderer& renderer,
                            const graphics::Camera2D& camera) const
{
    if (!m_enabled || m_counts.empty()) return;
    if (m_maxCount <= 0.0f) return;

    // Frustum culling: only iterate cells that intersect the visible area.
    const float ppm   = std::max(camera.zoom(), 1e-6f);
    const float halfW = static_cast<float>(camera.viewportWidth())  * 0.5f / ppm;
    const float halfH = static_cast<float>(camera.viewportHeight()) * 0.5f / ppm;
    const glm::vec2 cpos = camera.position();
    const float vL = cpos.x - halfW, vR = cpos.x + halfW;
    const float vB = cpos.y - halfH, vT = cpos.y + halfH;

    const float invCell = 1.0f / m_cellSize;
    int cx0 = static_cast<int>(std::floor((vL - m_origin.x) * invCell));
    int cx1 = static_cast<int>(std::floor((vR - m_origin.x) * invCell));
    int cy0 = static_cast<int>(std::floor((vB - m_origin.y) * invCell));
    int cy1 = static_cast<int>(std::floor((vT - m_origin.y) * invCell));
    cx0 = std::max(cx0, 0);
    cy0 = std::max(cy0, 0);
    cx1 = std::min(cx1, m_cols - 1);
    cy1 = std::min(cy1, m_rows - 1);
    if (cx0 > cx1 || cy0 > cy1) return;

    const float invMax = m_intensity / std::max(m_maxCount, 1.0f);
    const float half   = m_cellSize * 0.5f;

    renderer.beginFrame(camera);
    for (int y = cy0; y <= cy1; ++y) {
        for (int x = cx0; x <= cx1; ++x) {
            const float c = m_counts[idx(x, y)];
            if (c <= 0.05f) continue;                         // skip empty
            const float t = std::clamp(c * invMax, 0.0f, 1.0f);
            const glm::vec4 col = colormap(t);
            const glm::vec2 center{ m_origin.x + (x + 0.5f) * m_cellSize,
                                    m_origin.y + (y + 0.5f) * m_cellSize };
            renderer.drawQuad(center,
                              glm::vec2(m_cellSize, m_cellSize),
                              col);
            (void)half; // silence -Wunused-variable in some compilers
        }
    }
    renderer.endFrame();
}

} // namespace v2v::simulation
