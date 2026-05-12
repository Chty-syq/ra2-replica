#pragma once

#include <glad/glad.h>

// 统一的 OpenGL 加载入口。
// 当前项目使用 SDL 创建上下文，再由 GLAD 解析现代 OpenGL 函数地址。
void ensureOpenGlLoaded();

// 为了降低迁移成本，先保留旧代码里残留的 *Ptr 命名。
// 这样我们可以先把“手动 proc”切到 GLAD，再逐步把调用点收敛成标准 glXxx(...)。
#define glCreateShaderPtr glCreateShader
#define glShaderSourcePtr glShaderSource
#define glCompileShaderPtr glCompileShader
#define glGetShaderivPtr glGetShaderiv
#define glGetShaderInfoLogPtr glGetShaderInfoLog
#define glDeleteShaderPtr glDeleteShader
#define glCreateProgramPtr glCreateProgram
#define glAttachShaderPtr glAttachShader
#define glLinkProgramPtr glLinkProgram
#define glGetProgramivPtr glGetProgramiv
#define glGetProgramInfoLogPtr glGetProgramInfoLog
#define glDeleteProgramPtr glDeleteProgram
#define glUseProgramPtr glUseProgram
#define glGetUniformLocationPtr glGetUniformLocation
#define glUniform1iPtr glUniform1i
#define glUniform2fPtr glUniform2f
#define glUniform3fPtr glUniform3f
#define glUniform4fPtr glUniform4f
#define glUniform1fPtr glUniform1f
#define glUniform3fvPtr glUniform3fv
#define glUniform4fvPtr glUniform4fv
#define glGenVertexArraysPtr glGenVertexArrays
#define glBindVertexArrayPtr glBindVertexArray
#define glDeleteVertexArraysPtr glDeleteVertexArrays
#define glGenBuffersPtr glGenBuffers
#define glBindBufferPtr glBindBuffer
#define glBufferDataPtr glBufferData
#define glDeleteBuffersPtr glDeleteBuffers
#define glEnableVertexAttribArrayPtr glEnableVertexAttribArray
#define glVertexAttribPointerPtr glVertexAttribPointer
#define glVertexAttribDivisorPtr glVertexAttribDivisor
#define glDrawArraysInstancedPtr glDrawArraysInstanced
#define glActiveTexturePtr glActiveTexture
#define glGenFramebuffersPtr glGenFramebuffers
#define glBindFramebufferPtr glBindFramebuffer
#define glDeleteFramebuffersPtr glDeleteFramebuffers
#define glFramebufferTexture2DPtr glFramebufferTexture2D
#define glGenRenderbuffersPtr glGenRenderbuffers
#define glBindRenderbufferPtr glBindRenderbuffer
#define glRenderbufferStoragePtr glRenderbufferStorage
#define glFramebufferRenderbufferPtr glFramebufferRenderbuffer
#define glDeleteRenderbuffersPtr glDeleteRenderbuffers
#define glCheckFramebufferStatusPtr glCheckFramebufferStatus
