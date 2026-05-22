#include "graphics/Window.h"
#include "utils/Log.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <atomic>
#include <stdexcept>

namespace v2v::graphics {

namespace {

// Reference-count GLFW init across multiple Window instances.
std::atomic<int> g_glfwRefs{0};

void glfwErrorCallback(int code, const char* description)
{
    V2V_LOG_ERROR("GLFW error %d: %s", code, description ? description : "(null)");
}

} // namespace

Window::Window(int width, int height, const std::string& title)
    : m_width(width), m_height(height), m_title(title)
{
    if (g_glfwRefs.fetch_add(1) == 0) {
        glfwSetErrorCallback(&glfwErrorCallback);
        if (!glfwInit()) {
            g_glfwRefs.fetch_sub(1);
            throw std::runtime_error("Failed to initialize GLFW");
        }
    }

    // Request OpenGL 3.3 Core (forward-compatible on macOS).
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,        GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES,               4); // 4x MSAA
    glfwWindowHint(GLFW_RESIZABLE,             GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE,               GLFW_TRUE);

    m_window = glfwCreateWindow(m_width, m_height, m_title.c_str(), nullptr, nullptr);
    if (!m_window) {
        if (g_glfwRefs.fetch_sub(1) == 1) glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1); // vsync

    glfwSetWindowUserPointer(m_window, this);
    glfwSetScrollCallback         (m_window, &Window::s_scroll);
    glfwSetCursorPosCallback      (m_window, &Window::s_cursorPos);
    glfwSetMouseButtonCallback    (m_window, &Window::s_mouseButton);
    glfwSetKeyCallback            (m_window, &Window::s_key);
    glfwSetFramebufferSizeCallback(m_window, &Window::s_framebuffer);

    // Keep cached size in sync with the actual framebuffer (which can differ
    // from window size on high-DPI displays).
    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(m_window, &fbW, &fbH);
    m_width  = fbW;
    m_height = fbH;
}

Window::~Window()
{
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    if (g_glfwRefs.fetch_sub(1) == 1) {
        glfwTerminate();
    }
}

bool   Window::shouldClose() const { return glfwWindowShouldClose(m_window) != 0; }
void   Window::pollEvents()        { glfwPollEvents(); }
void   Window::swapBuffers()       { glfwSwapBuffers(m_window); }
void   Window::requestClose()      { glfwSetWindowShouldClose(m_window, GLFW_TRUE); }
double Window::time() const        { return glfwGetTime(); }

void Window::setScrollCallback         (ScrollCallback cb)          { m_scrollCb   = std::move(cb); }
void Window::setCursorPosCallback      (CursorPosCallback cb)       { m_cursorCb   = std::move(cb); }
void Window::setMouseButtonCallback    (MouseButtonCallback cb)     { m_mouseBtnCb = std::move(cb); }
void Window::setKeyCallback            (KeyCallback cb)             { m_keyCb      = std::move(cb); }
void Window::setFramebufferSizeCallback(FramebufferSizeCallback cb) { m_fbSizeCb   = std::move(cb); }

// --- static trampolines ----------------------------------------------------

void Window::s_scroll(GLFWwindow* win, double xOff, double yOff)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(win));
    if (self && self->m_scrollCb) self->m_scrollCb(xOff, yOff);
}

void Window::s_cursorPos(GLFWwindow* win, double x, double y)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(win));
    if (self && self->m_cursorCb) self->m_cursorCb(x, y);
}

void Window::s_mouseButton(GLFWwindow* win, int button, int action, int mods)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(win));
    if (self && self->m_mouseBtnCb) self->m_mouseBtnCb(button, action, mods);
}

void Window::s_key(GLFWwindow* win, int key, int scancode, int action, int mods)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(win));
    if (self && self->m_keyCb) self->m_keyCb(key, scancode, action, mods);
}

void Window::s_framebuffer(GLFWwindow* win, int w, int h)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(win));
    if (!self) return;
    self->m_width  = w;
    self->m_height = h;
    if (self->m_fbSizeCb) self->m_fbSizeCb(w, h);
}

} // namespace v2v::graphics
