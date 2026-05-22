#include "graphics/Camera2D.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace v2v::graphics {

Camera2D::Camera2D(int viewportWidth, int viewportHeight)
    : m_viewportWidth (viewportWidth  > 0 ? viewportWidth  : 1)
    , m_viewportHeight(viewportHeight > 0 ? viewportHeight : 1)
{}

void Camera2D::setViewport(int width, int height)
{
    m_viewportWidth  = width  > 0 ? width  : 1;
    m_viewportHeight = height > 0 ? height : 1;
}

void Camera2D::setZoom(float z)
{
    m_zoom        = std::clamp(z, kMinZoom, kMaxZoom);
    m_targetZoom  = m_zoom;
}

void Camera2D::tick(float dt)
{
    if (m_smoothHalfLife <= 0.0f || dt <= 0.0f) {
        m_position = m_targetPos;
        m_zoom     = std::clamp(m_targetZoom, kMinZoom, kMaxZoom);
        return;
    }
    // Exponential ease: alpha = 1 - 0.5^(dt / halfLife). Frame-rate independent.
    const float alpha = 1.0f - std::pow(0.5f, dt / m_smoothHalfLife);
    m_position += (m_targetPos  - m_position) * alpha;
    m_zoom     += (m_targetZoom - m_zoom)     * alpha;
    m_zoom = std::clamp(m_zoom, kMinZoom, kMaxZoom);
}

glm::mat4 Camera2D::projectionMatrix() const
{
    // Half-extents of the visible world rectangle, in world units.
    const float halfW = static_cast<float>(m_viewportWidth)  * 0.5f / m_zoom;
    const float halfH = static_cast<float>(m_viewportHeight) * 0.5f / m_zoom;
    return glm::ortho(-halfW, halfW, -halfH, halfH, -1.0f, 1.0f);
}

glm::mat4 Camera2D::viewMatrix() const
{
    // The view matrix translates the world so that `m_position` lands at the origin.
    return glm::translate(glm::mat4(1.0f), glm::vec3(-m_position, 0.0f));
}

glm::mat4 Camera2D::viewProjection() const
{
    return projectionMatrix() * viewMatrix();
}

glm::vec2 Camera2D::screenToWorld(glm::vec2 screen) const
{
    // Screen origin is top-left, Y down. Convert to centered, Y-up.
    const glm::vec2 centered = {
        screen.x - static_cast<float>(m_viewportWidth)  * 0.5f,
        static_cast<float>(m_viewportHeight) * 0.5f - screen.y,
    };
    return m_position + centered / m_zoom;
}

glm::vec2 Camera2D::worldToScreen(glm::vec2 world) const
{
    const glm::vec2 centered = (world - m_position) * m_zoom;
    return {
        centered.x + static_cast<float>(m_viewportWidth)  * 0.5f,
        static_cast<float>(m_viewportHeight) * 0.5f - centered.y,
    };
}

void Camera2D::pan(glm::vec2 deltaScreen)
{
    // Drag right by `dx` pixels ⇒ world should appear to move right ⇒ camera moves left.
    // Drag down  by `dy` pixels ⇒ world should appear to move down  ⇒ camera moves up
    // (remember screen Y is inverted vs world Y).
    const glm::vec2 deltaWorld = { deltaScreen.x, -deltaScreen.y };
    m_position -= deltaWorld / m_zoom;
    m_targetPos = m_position;   // user input wins over follow smoothing
}

void Camera2D::zoomAt(float factor, glm::vec2 screenAnchor)
{
    if (factor <= 0.0f) return;

    const glm::vec2 worldBefore = screenToWorld(screenAnchor);
    setZoom(m_zoom * factor);
    const glm::vec2 worldAfter  = screenToWorld(screenAnchor);

    // Translate so the anchored world point is back under the cursor.
    m_position += worldBefore - worldAfter;
}

} // namespace v2v::graphics
