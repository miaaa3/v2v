#include "graphics/Camera2D.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace v2v::graphics {

// Below this pitch (radians) the camera is treated as perfectly flat, so the
// legacy 2D code paths run unchanged — a guaranteed-safe fallback.
constexpr float kTiltEpsilon = 1.0e-4f;

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

void Camera2D::setTilted(bool on)
{
    // Toggling only moves the *target*; tick() eases m_tilt toward it, which
    // is what makes entering / leaving 3D mode look cinematic.
    m_targetTilt = on ? glm::radians(m_tiltAngleDeg) : 0.0f;
}

void Camera2D::setTiltAngle(float degrees)
{
    m_tiltAngleDeg = std::clamp(degrees, 0.0f, 75.0f);
    if (tilted()) m_targetTilt = glm::radians(m_tiltAngleDeg);
}

void Camera2D::tick(float dt)
{
    if (m_smoothHalfLife <= 0.0f || dt <= 0.0f) {
        m_position = m_targetPos;
        m_zoom     = std::clamp(m_targetZoom, kMinZoom, kMaxZoom);
        m_tilt     = m_targetTilt;
        return;
    }
    // Exponential ease: alpha = 1 - 0.5^(dt / halfLife). Frame-rate independent.
    const float alpha = 1.0f - std::pow(0.5f, dt / m_smoothHalfLife);
    m_position += (m_targetPos  - m_position) * alpha;
    m_zoom     += (m_targetZoom - m_zoom)     * alpha;
    m_tilt     += (m_targetTilt - m_tilt)     * alpha;
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
    // Flat mode: exactly the legacy 2D path (guaranteed-safe fallback).
    if (m_tilt < kTiltEpsilon)
        return projectionMatrix() * viewMatrix();

    // Tilted ("isometric / 2.5D") mode. We keep an *orthographic* projection
    // (no perspective ⇒ no foreshortening, depth-stable, and cheap) and simply
    // rotate the ground plane around the screen-horizontal (X) axis. The focus
    // point stays centred. Every renderer that consumes viewProjection() —
    // tiles, graph, vehicles, links — tilts automatically, no edits needed.
    const float halfW = static_cast<float>(m_viewportWidth)  * 0.5f / m_zoom;
    const float halfH = static_cast<float>(m_viewportHeight) * 0.5f / m_zoom;
    // Generous symmetric depth range so the tilted plane is never clipped.
    const float depth = (halfW + halfH) * 4.0f + 1.0f;
    const glm::mat4 proj  = glm::ortho(-halfW, halfW, -halfH, halfH, -depth, depth);
    const glm::mat4 rot   = glm::rotate(glm::mat4(1.0f), m_tilt,
                                        glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::mat4 trans = glm::translate(glm::mat4(1.0f),
                                           glm::vec3(-m_position, 0.0f));
    return proj * rot * trans;
}

glm::vec2 Camera2D::screenToWorld(glm::vec2 screen) const
{
    if (m_tilt < kTiltEpsilon) {
        // Flat path — unchanged from the legacy 2D camera.
        // Screen origin is top-left, Y down. Convert to centered, Y-up.
        const glm::vec2 centered = {
            screen.x - static_cast<float>(m_viewportWidth)  * 0.5f,
            static_cast<float>(m_viewportHeight) * 0.5f - screen.y,
        };
        return m_position + centered / m_zoom;
    }
    // Tilted path: unproject the cursor to a world-space ray and intersect it
    // with the ground plane z = 0. Keeps vehicle picking accurate in 3D mode.
    const glm::mat4 invVP = glm::inverse(viewProjection());
    const float nx = 2.0f * screen.x / static_cast<float>(m_viewportWidth)  - 1.0f;
    const float ny = 1.0f - 2.0f * screen.y / static_cast<float>(m_viewportHeight);
    const glm::vec4 a = invVP * glm::vec4(nx, ny, -1.0f, 1.0f);
    const glm::vec4 b = invVP * glm::vec4(nx, ny,  1.0f, 1.0f);
    const glm::vec3 p0 = glm::vec3(a) / a.w;
    const glm::vec3 p1 = glm::vec3(b) / b.w;
    const float dz = p1.z - p0.z;
    const float t  = (std::abs(dz) > 1.0e-6f) ? (-p0.z / dz) : 0.0f;
    const glm::vec3 hit = p0 + (p1 - p0) * t;
    return { hit.x, hit.y };
}

glm::vec2 Camera2D::worldToScreen(glm::vec2 world) const
{
    if (m_tilt < kTiltEpsilon) {
        const glm::vec2 centered = (world - m_position) * m_zoom;
        return {
            centered.x + static_cast<float>(m_viewportWidth)  * 0.5f,
            static_cast<float>(m_viewportHeight) * 0.5f - centered.y,
        };
    }
    // Tilted path: project through the full view-projection.
    const glm::vec4 clip = viewProjection() * glm::vec4(world, 0.0f, 1.0f);
    const glm::vec3 ndc  = glm::vec3(clip) / clip.w;
    return {
        (ndc.x * 0.5f + 0.5f) * static_cast<float>(m_viewportWidth),
        (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(m_viewportHeight),
    };
}

void Camera2D::pan(glm::vec2 deltaScreen)
{
    // Drag right by `dx` pixels ⇒ world should appear to move right ⇒ camera moves left.
    // Drag down  by `dy` pixels ⇒ world should appear to move down  ⇒ camera moves up
    // (remember screen Y is inverted vs world Y).
    glm::vec2 deltaWorld = { deltaScreen.x, -deltaScreen.y };
    // In tilted mode the vertical screen axis is foreshortened by cos(tilt);
    // divide it back out so a drag tracks the cursor along the focus line.
    if (m_tilt >= kTiltEpsilon) {
        const float c = std::cos(m_tilt);
        if (c > 1.0e-3f) deltaWorld.y /= c;
    }
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
