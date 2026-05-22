#pragma once

#include <functional>
#include <string>

struct GLFWwindow;

namespace v2v::graphics {

/**
 * Thin RAII wrapper around a GLFW window + OpenGL context.
 *
 * Initializes GLFW the first time a Window is constructed and terminates
 * GLFW when the last Window is destroyed. Owns its GLFWwindow*; non-copyable.
 *
 * Input is forwarded through std::function callbacks so the main loop can
 * stay decoupled from GLFW's C API.
 */
class Window {
public:
    using ScrollCallback           = std::function<void(double xOffset, double yOffset)>;
    using CursorPosCallback        = std::function<void(double xPos,    double yPos)>;
    using MouseButtonCallback      = std::function<void(int button, int action, int mods)>;
    using KeyCallback              = std::function<void(int key, int scancode, int action, int mods)>;
    using FramebufferSizeCallback  = std::function<void(int width, int height)>;

    Window(int width, int height, const std::string& title);
    ~Window();

    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&)                 = delete;
    Window& operator=(Window&&)      = delete;

    // Frame lifecycle
    bool   shouldClose() const;
    void   pollEvents();
    void   swapBuffers();
    void   requestClose();

    // Time since GLFW init (seconds)
    double time() const;

    // Geometry
    int    width()  const { return m_width;  }
    int    height() const { return m_height; }
    float  aspectRatio() const {
        return (m_height > 0) ? static_cast<float>(m_width) / static_cast<float>(m_height) : 1.0f;
    }

    // Raw handle (for ImGui backend wiring, etc.)
    GLFWwindow* handle() const { return m_window; }

    // Input callbacks
    void setScrollCallback(ScrollCallback cb);
    void setCursorPosCallback(CursorPosCallback cb);
    void setMouseButtonCallback(MouseButtonCallback cb);
    void setKeyCallback(KeyCallback cb);
    void setFramebufferSizeCallback(FramebufferSizeCallback cb);

private:
    GLFWwindow* m_window = nullptr;
    int         m_width  = 0;
    int         m_height = 0;
    std::string m_title;

    ScrollCallback          m_scrollCb;
    CursorPosCallback       m_cursorCb;
    MouseButtonCallback     m_mouseBtnCb;
    KeyCallback             m_keyCb;
    FramebufferSizeCallback m_fbSizeCb;

    // Static trampolines registered with GLFW; they fetch the Window* from
    // glfwGetWindowUserPointer and forward to the std::function members.
    static void s_scroll      (GLFWwindow*, double, double);
    static void s_cursorPos   (GLFWwindow*, double, double);
    static void s_mouseButton (GLFWwindow*, int, int, int);
    static void s_key         (GLFWwindow*, int, int, int, int);
    static void s_framebuffer (GLFWwindow*, int, int);
};

} // namespace v2v::graphics
