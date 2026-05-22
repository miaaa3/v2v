#include "graphics/Renderer.h"
#include "graphics/Camera2D.h"
#include "utils/Log.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

namespace v2v::graphics {

namespace {

// Embedded fallback used when shader files aren't found on disk.
constexpr const char* kVertSrc = R"(#version 330 core
layout(location = 0) in vec2 aPos;
uniform mat4 uViewProjection;
uniform mat4 uModel;
void main() {
    gl_Position = uViewProjection * uModel * vec4(aPos, 0.0, 1.0);
}
)";

constexpr const char* kFragSrc = R"(#version 330 core
out vec4 FragColor;
uniform vec4 uColor;
void main() {
    FragColor = uColor;
}
)";

} // namespace

Renderer::~Renderer() { destroy(); }

bool Renderer::init(const std::string& shaderDir)
{
    if (m_initialized) return true;

    // Load OpenGL function pointers through GLFW's getter.
    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
        V2V_LOG_ERROR("Failed to initialize GLAD (no current GL context?)");
        return false;
    }

    V2V_LOG_INFO("OpenGL %s",  reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    V2V_LOG_INFO("GLSL    %s", reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)));
    V2V_LOG_INFO("Vendor  %s", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
    V2V_LOG_INFO("Device  %s", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    // Compile shader: try files first, fall back to embedded source.
    bool shaderOk = false;
    if (!shaderDir.empty()) {
        const std::string vp = shaderDir + "/basic.vert";
        const std::string fp = shaderDir + "/basic.frag";
        shaderOk = m_basicShader.loadFromFiles(vp, fp);
        if (!shaderOk) {
            V2V_LOG_WARN("Could not load shaders from '%s' — using embedded fallback.",
                         shaderDir.c_str());
        }
    }
    if (!shaderOk) {
        shaderOk = m_basicShader.loadFromSource(kVertSrc, kFragSrc);
    }
    if (!shaderOk) {
        V2V_LOG_ERROR("Failed to build basic shader");
        return false;
    }

    createQuadGeometry();

    // Sensible defaults
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);

    m_initialized = true;
    return true;
}

void Renderer::destroy()
{
    if (m_quadVbo) { glDeleteBuffers(1, &m_quadVbo); m_quadVbo = 0; }
    if (m_quadVao) { glDeleteVertexArrays(1, &m_quadVao); m_quadVao = 0; }
    m_initialized = false;
}

void Renderer::createQuadGeometry()
{
    // Unit quad centered at origin: scaled by uModel at draw time.
    const float verts[] = {
        -0.5f, -0.5f,
         0.5f, -0.5f,
         0.5f,  0.5f,
        -0.5f, -0.5f,
         0.5f,  0.5f,
        -0.5f,  0.5f,
    };

    glGenVertexArrays(1, &m_quadVao);
    glGenBuffers     (1, &m_quadVbo);

    glBindVertexArray(m_quadVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    glBindVertexArray(0);
}

void Renderer::beginFrame(const Camera2D& camera)
{
    m_viewProjection = camera.viewProjection();

    m_basicShader.bind();
    m_basicShader.setMat4("uViewProjection", m_viewProjection);
}

void Renderer::endFrame()
{
    // Currently nothing to flush (immediate drawing). Reserved for batched
    // rendering in later steps.
}

void Renderer::setViewport(int x, int y, int width, int height)
{
    glViewport(x, y, width, height);
}

void Renderer::clear(float r, float g, float b, float a)
{
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::drawQuad(glm::vec2 center, glm::vec2 size, glm::vec4 color)
{
    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f));
    model           = glm::scale    (model,           glm::vec3(size,   1.0f));

    m_basicShader.setMat4("uModel", model);
    m_basicShader.setVec4("uColor", color);

    glBindVertexArray(m_quadVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

} // namespace v2v::graphics
