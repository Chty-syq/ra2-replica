#pragma once
#include <glad/glad.h>
#include <utility>

#include "shp.hpp"

class Texture {
    GLuint texture_ = 0;
public:
    const int width, height;

    Texture(int width, int height, GLenum format, const Bytes& pixels): width(width), height(height) {
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // 索引纹理每像素只有一个字节，142 字节的行不能按默认的 4 字节对齐读取。
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, format == GL_RED ? GL_R8 : GL_RGBA8, width, height, 0, format, GL_UNSIGNED_BYTE, pixels.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }

    ~Texture() { glDeleteTextures(1, &texture_); }
    Texture(Texture&& other) noexcept: texture_(std::exchange(other.texture_, 0)), width(other.width), height(other.height) {}   // 放进 vector 时转移纹理编号，原对象不再负责删除它。

    void bind(int unit = 0) const {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, texture_);
    }
};
