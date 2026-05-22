#include "graphics/Shader.h"
#include "utils/Log.h"

#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

namespace v2v::graphics {

namespace {

std::uint32_t compileStage(GLenum stage, std::string_view src, const char* label)
{
    GLuint sh = glCreateShader(stage);
    const GLchar* ptr = src.data();
    const GLint   len = static_cast<GLint>(src.size());
    glShaderSource(sh, 1, &ptr, &len);
    glCompileShader(sh);

    GLint ok = GL_FALSE;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(static_cast<size_t>(logLen > 0 ? logLen : 1));
        glGetShaderInfoLog(sh, logLen, nullptr, log.data());
        V2V_LOG_ERROR("Shader compile error (%s):\n%s", label, log.data());
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

bool linkProgram(GLuint program)
{
    glLinkProgram(program);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(static_cast<size_t>(logLen > 0 ? logLen : 1));
        glGetProgramInfoLog(program, logLen, nullptr, log.data());
        V2V_LOG_ERROR("Shader link error:\n%s", log.data());
        return false;
    }
    return true;
}

std::string slurp(const std::string& path)
{
    std::ifstream in(path);
    if (!in) {
        V2V_LOG_ERROR("Cannot open shader file: %s", path.c_str());
        return {};
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

Shader::~Shader() { destroy(); }

Shader::Shader(Shader&& other) noexcept
    : m_program(other.m_program), m_uniformCache(std::move(other.m_uniformCache))
{
    other.m_program = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept
{
    if (this != &other) {
        destroy();
        m_program      = other.m_program;
        m_uniformCache = std::move(other.m_uniformCache);
        other.m_program = 0;
    }
    return *this;
}

void Shader::destroy()
{
    if (m_program) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
    m_uniformCache.clear();
}

bool Shader::loadFromSource(std::string_view vertexSrc, std::string_view fragmentSrc)
{
    destroy();

    GLuint vs = compileStage(GL_VERTEX_SHADER,   vertexSrc,   "vertex");
    if (!vs) return false;
    GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragmentSrc, "fragment");
    if (!fs) { glDeleteShader(vs); return false; }

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    bool ok = linkProgram(program);

    glDetachShader(program, vs);
    glDetachShader(program, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    if (!ok) {
        glDeleteProgram(program);
        return false;
    }
    m_program = program;
    return true;
}

bool Shader::loadFromFiles(const std::string& vertPath, const std::string& fragPath)
{
    const std::string v = slurp(vertPath);
    const std::string f = slurp(fragPath);
    if (v.empty() || f.empty()) return false;
    return loadFromSource(v, f);
}

void Shader::bind()   const { if (m_program) glUseProgram(m_program); }
void Shader::unbind()       { glUseProgram(0); }

int Shader::uniformLocation(std::string_view name)
{
    auto key = std::string(name);
    auto it  = m_uniformCache.find(key);
    if (it != m_uniformCache.end()) return it->second;

    int loc = glGetUniformLocation(m_program, key.c_str());
    m_uniformCache.emplace(std::move(key), loc);
    return loc;
}

void Shader::setInt(std::string_view name, int value)
{
    int loc = uniformLocation(name);
    if (loc >= 0) glUniform1i(loc, value);
}

void Shader::setFloat(std::string_view name, float value)
{
    int loc = uniformLocation(name);
    if (loc >= 0) glUniform1f(loc, value);
}

void Shader::setVec2(std::string_view name, const glm::vec2& v)
{
    int loc = uniformLocation(name);
    if (loc >= 0) glUniform2fv(loc, 1, glm::value_ptr(v));
}

void Shader::setVec3(std::string_view name, const glm::vec3& v)
{
    int loc = uniformLocation(name);
    if (loc >= 0) glUniform3fv(loc, 1, glm::value_ptr(v));
}

void Shader::setVec4(std::string_view name, const glm::vec4& v)
{
    int loc = uniformLocation(name);
    if (loc >= 0) glUniform4fv(loc, 1, glm::value_ptr(v));
}

void Shader::setMat4(std::string_view name, const glm::mat4& m)
{
    int loc = uniformLocation(name);
    if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(m));
}

} // namespace v2v::graphics
