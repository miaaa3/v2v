#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace v2v::graphics {

/**
 * GLSL shader program (vertex + fragment).
 *
 * Owns a GL program object; non-copyable, movable.
 * Uniform locations are looked up and cached on first use.
 */
class Shader {
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&)            = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    // Build from inline GLSL source.
    bool loadFromSource(std::string_view vertexSrc, std::string_view fragmentSrc);

    // Build from .vert / .frag files on disk.
    bool loadFromFiles(const std::string& vertPath, const std::string& fragPath);

    void bind() const;
    static void unbind();

    bool isValid() const { return m_program != 0; }
    std::uint32_t id() const { return m_program; }

    // Uniform setters (no-op if uniform not found — useful when shaders evolve).
    void setInt  (std::string_view name, int value);
    void setFloat(std::string_view name, float value);
    void setVec2 (std::string_view name, const glm::vec2& v);
    void setVec3 (std::string_view name, const glm::vec3& v);
    void setVec4 (std::string_view name, const glm::vec4& v);
    void setMat4 (std::string_view name, const glm::mat4& m);

private:
    std::uint32_t m_program = 0;
    std::unordered_map<std::string, int> m_uniformCache;

    int  uniformLocation(std::string_view name);
    void destroy();
};

} // namespace v2v::graphics
