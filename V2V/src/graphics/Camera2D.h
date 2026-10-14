#pragma once

#include <glm/glm.hpp>

#include <algorithm>

namespace v2v::graphics {

/**
 * Orthographic 2D camera with pan & zoom.
 *
 * Conventions:
 *   - World space is right-handed, Y-up.
 *   - Screen space is the GLFW cursor convention: origin top-left, Y-down.
 *   - `position` is the world-space point at the center of the viewport.
 *   - `zoom` is "screen pixels per world unit" (zoom > 1 ⇒ zoomed in).
 *
 * Typical use:
 *   Camera2D cam(w, h);
 *   ... on resize:     cam.setViewport(w, h);
 *   ... on drag:       cam.pan(deltaPixels);
 *   ... on wheel:      cam.zoomAt(factor, cursorScreenPos);
 *   ... per frame:     shader.setMat4("uViewProjection", cam.viewProjection());
 */
class Camera2D {
public:
    Camera2D(int viewportWidth, int viewportHeight);

    // Matrices --------------------------------------------------------------
    glm::mat4 projectionMatrix() const;
    glm::mat4 viewMatrix()       const;
    glm::mat4 viewProjection()   const;

    // Interaction -----------------------------------------------------------
    /// Pan by a screen-pixel delta (e.g. mouse drag). Moves the camera so
    /// world content appears to follow the cursor.
    void pan(glm::vec2 deltaScreen);

    /// Multiply zoom by `factor`, keeping the world point under
    /// `screenAnchor` fixed on screen. Typical wheel handler.
    void zoomAt(float factor, glm::vec2 screenAnchor);

    // Coordinate conversion -------------------------------------------------
    glm::vec2 screenToWorld(glm::vec2 screen) const;
    glm::vec2 worldToScreen(glm::vec2 world)  const;

    // Viewport --------------------------------------------------------------
    void setViewport(int width, int height);
    int  viewportWidth()  const { return m_viewportWidth;  }
    int  viewportHeight() const { return m_viewportHeight; }

    // Accessors -------------------------------------------------------------
    glm::vec2 position() const { return m_position; }
    float     zoom()     const { return m_zoom;     }
    void      setPosition(glm::vec2 p) { m_position = p; m_targetPos = p; }
    void      setZoom(float z);

    // Cinematic smoothing ---------------------------------------------------
    /// Set the position the camera should glide toward. Updated each frame
    /// inside `tick(dt)` with an exponential ease.
    void  setTargetPosition(glm::vec2 p) { m_targetPos  = p; }
    void  setTargetZoom    (float z)     { m_targetZoom = std::max(z, kMinZoom); }

    /// Half-life (seconds) of the exponential ease toward the targets.
    /// 0 disables smoothing (camera snaps).
    void  setSmoothing(float halfLifeSec) { m_smoothHalfLife = std::max(0.0f, halfLifeSec); }
    float smoothing() const               { return m_smoothHalfLife; }

    /// Move position/zoom toward their targets. Call once per frame.
    void  tick(float dt);

    // Pseudo-3D tilt --------------------------------------------------------
    /// Enable/disable the tilted ("isometric / 2.5D") projection. The camera
    /// eases between flat and tilted over `smoothing()` seconds, so toggling
    /// looks cinematic. Flat mode (tilt 0) is byte-identical to the legacy 2D
    /// camera and is always a safe fallback.
    void  setTilted(bool on);
    void  toggleTilt() { setTilted(!tilted()); }
    bool  tilted() const { return m_targetTilt > 1.0e-3f; }

    /// Current (eased) pitch in radians — 0 = top-down. For diagnostics.
    float tilt() const { return m_tilt; }

    /// Pitch applied when tilt mode is on. Clamped to [0, 75] degrees.
    void  setTiltAngle(float degrees);
    float tiltAngle() const { return m_tiltAngleDeg; }

    // Limits ----------------------------------------------------------------
    static constexpr float kMinZoom = 0.01f;
    static constexpr float kMaxZoom = 1000.0f;

private:
    glm::vec2 m_position{0.0f, 0.0f};
    float     m_zoom = 1.0f;
    int       m_viewportWidth;
    int       m_viewportHeight;

    // Smoothing state.
    glm::vec2 m_targetPos     {0.0f, 0.0f};
    float     m_targetZoom    = 1.0f;
    float     m_smoothHalfLife = 0.20f;  // 200 ms exponential ease

    // Pseudo-3D tilt state.
    float     m_tilt          = 0.0f;   // current pitch (radians), 0 = top-down
    float     m_targetTilt    = 0.0f;   // pitch the camera eases toward
    float     m_tiltAngleDeg  = 50.0f;  // pitch applied when tilt mode is on
};

} // namespace v2v::graphics
