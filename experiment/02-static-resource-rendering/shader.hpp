#pragma once

#include <glad/glad.h>
#include <SDL.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>


class Shader {
    GLuint vertex_ = 0, fragment_ = 0, program_ = 0;

    void compile(GLuint& shader, GLenum kind, std::string path) {
        std::size_t size = 0;
        const std::unique_ptr<void, decltype(&SDL_free)> data(SDL_LoadFile(path.c_str(), &size), SDL_free);
        const std::string source = std::string(static_cast<const char*>(data.get()), size);
        const char* code = source.c_str();
        shader = glCreateShader(kind);
        glShaderSource(shader, 1, &code, nullptr);
        glCompileShader(shader);

        GLint ok = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char infoLog[512];
            glGetShaderInfoLog(shader, 512, nullptr, infoLog);
            throw std::runtime_error("Shader编译失败: " + std::string(infoLog));
        }
    }

public:
    Shader(const std::string &vPath, const std::string &fPath) {
        compile(vertex_, GL_VERTEX_SHADER, vPath);
        compile(fragment_, GL_FRAGMENT_SHADER, fPath);
        program_ = glCreateProgram();
        glAttachShader(program_, vertex_);
        glAttachShader(program_, fragment_);
        glLinkProgram(program_);

        GLint ok = GL_FALSE;
        glGetProgramiv(program_, GL_LINK_STATUS, &ok);
        if (!ok) {
            char infoLog[512];
            glGetProgramInfoLog(program_, 512, nullptr, infoLog);
            throw std::runtime_error("Shader链接失败: " + std::string(infoLog));
        }

    }

    ~Shader() {
        glDeleteProgram(program_);
        glDeleteShader(fragment_);
        glDeleteShader(vertex_);
    }

    void use() const { glUseProgram(program_); }
    void setInt(const char* name, int value) const { glUniform1i(glGetUniformLocation(program_, name), value); }
    void setFloat(const char* name, float value) const { glUniform1f(glGetUniformLocation(program_, name), value); }
    void setVec2(const char* name, const glm::vec2& value) const { glUniform2f(glGetUniformLocation(program_, name), value.x, value.y); }
    void setVec3(const char* name, const glm::vec3& value) const { glUniform3f(glGetUniformLocation(program_, name), value.x, value.y, value.z); }
    void setVec2(const char* name, float x, float y) const { setVec2(name, glm::vec2(x, y)); }
    void setVec3(const char* name, float x, float y, float z) const { setVec3(name, glm::vec3(x, y, z)); }
};
