#pragma once

namespace v2v::graphics { class Window; }

namespace v2v::ui {

/**
 * RAII wrapper around Dear ImGui + its GLFW / OpenGL3 backends.
 *
 * Usage:
 *     ImGuiLayer ui;
 *     ui.init(window);             // after GL context is current
 *     while (!window.shouldClose()) {
 *         ...                       // input, simulation, world rendering
 *         ui.beginFrame();
 *         Panels::draw(...);        // ImGui::Begin/End calls
 *         ui.endFrame();            // submits the draw lists to GL
 *         window.swapBuffers();
 *     }
 *
 * ImGuiLayer's destructor tears down both ImGui and its backends.
 */
class ImGuiLayer {
public:
    ImGuiLayer() = default;
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&)            = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    bool init(graphics::Window& window, const char* glslVersion = "#version 330");

    void beginFrame();
    void endFrame();

    bool wantCaptureMouse()    const;
    bool wantCaptureKeyboard() const;

    bool isInitialized() const { return m_initialized; }

private:
    bool m_initialized = false;
};

} // namespace v2v::ui
