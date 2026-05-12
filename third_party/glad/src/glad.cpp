#include "glad/glad.h"

extern "C" {
int GLAD_GL_VERSION_3_3 = 0;

PFNGLCREATESHADERPROC glad_glCreateShader = nullptr;
PFNGLSHADERSOURCEPROC glad_glShaderSource = nullptr;
PFNGLCOMPILESHADERPROC glad_glCompileShader = nullptr;
PFNGLGETSHADERIVPROC glad_glGetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC glad_glGetShaderInfoLog = nullptr;
PFNGLDELETESHADERPROC glad_glDeleteShader = nullptr;
PFNGLCREATEPROGRAMPROC glad_glCreateProgram = nullptr;
PFNGLATTACHSHADERPROC glad_glAttachShader = nullptr;
PFNGLLINKPROGRAMPROC glad_glLinkProgram = nullptr;
PFNGLGETPROGRAMIVPROC glad_glGetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC glad_glGetProgramInfoLog = nullptr;
PFNGLDELETEPROGRAMPROC glad_glDeleteProgram = nullptr;
PFNGLUSEPROGRAMPROC glad_glUseProgram = nullptr;
PFNGLGETUNIFORMLOCATIONPROC glad_glGetUniformLocation = nullptr;
PFNGLUNIFORM1IPROC glad_glUniform1i = nullptr;
PFNGLUNIFORM2FPROC glad_glUniform2f = nullptr;
PFNGLUNIFORM3FPROC glad_glUniform3f = nullptr;
PFNGLUNIFORM4FPROC glad_glUniform4f = nullptr;
PFNGLUNIFORM1FPROC glad_glUniform1f = nullptr;
PFNGLUNIFORM3FVPROC glad_glUniform3fv = nullptr;
PFNGLUNIFORM4FVPROC glad_glUniform4fv = nullptr;
PFNGLGENVERTEXARRAYSPROC glad_glGenVertexArrays = nullptr;
PFNGLBINDVERTEXARRAYPROC glad_glBindVertexArray = nullptr;
PFNGLDELETEVERTEXARRAYSPROC glad_glDeleteVertexArrays = nullptr;
PFNGLGENBUFFERSPROC glad_glGenBuffers = nullptr;
PFNGLBINDBUFFERPROC glad_glBindBuffer = nullptr;
PFNGLBUFFERDATAPROC glad_glBufferData = nullptr;
PFNGLDELETEBUFFERSPROC glad_glDeleteBuffers = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC glad_glEnableVertexAttribArray = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC glad_glVertexAttribPointer = nullptr;
PFNGLVERTEXATTRIBDIVISORPROC glad_glVertexAttribDivisor = nullptr;
PFNGLDRAWARRAYSINSTANCEDPROC glad_glDrawArraysInstanced = nullptr;
PFNGLACTIVETEXTUREPROC glad_glActiveTexture = nullptr;
PFNGLGENFRAMEBUFFERSPROC glad_glGenFramebuffers = nullptr;
PFNGLBINDFRAMEBUFFERPROC glad_glBindFramebuffer = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC glad_glDeleteFramebuffers = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC glad_glFramebufferTexture2D = nullptr;
PFNGLGENRENDERBUFFERSPROC glad_glGenRenderbuffers = nullptr;
PFNGLBINDRENDERBUFFERPROC glad_glBindRenderbuffer = nullptr;
PFNGLRENDERBUFFERSTORAGEPROC glad_glRenderbufferStorage = nullptr;
PFNGLFRAMEBUFFERRENDERBUFFERPROC glad_glFramebufferRenderbuffer = nullptr;
PFNGLDELETERENDERBUFFERSPROC glad_glDeleteRenderbuffers = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC glad_glCheckFramebufferStatus = nullptr;
}

namespace {
template <typename T>
bool loadProc(GLADloadproc load, const char* name, T& target) {
  target = reinterpret_cast<T>(load(name));
  return target != nullptr;
}
}

extern "C" int gladLoadGLLoader(GLADloadproc load) {
  if (load == nullptr) {
    return 0;
  }

  bool ok = true;
  ok = loadProc(load, "glCreateShader", glad_glCreateShader) && ok;
  ok = loadProc(load, "glShaderSource", glad_glShaderSource) && ok;
  ok = loadProc(load, "glCompileShader", glad_glCompileShader) && ok;
  ok = loadProc(load, "glGetShaderiv", glad_glGetShaderiv) && ok;
  ok = loadProc(load, "glGetShaderInfoLog", glad_glGetShaderInfoLog) && ok;
  ok = loadProc(load, "glDeleteShader", glad_glDeleteShader) && ok;
  ok = loadProc(load, "glCreateProgram", glad_glCreateProgram) && ok;
  ok = loadProc(load, "glAttachShader", glad_glAttachShader) && ok;
  ok = loadProc(load, "glLinkProgram", glad_glLinkProgram) && ok;
  ok = loadProc(load, "glGetProgramiv", glad_glGetProgramiv) && ok;
  ok = loadProc(load, "glGetProgramInfoLog", glad_glGetProgramInfoLog) && ok;
  ok = loadProc(load, "glDeleteProgram", glad_glDeleteProgram) && ok;
  ok = loadProc(load, "glUseProgram", glad_glUseProgram) && ok;
  ok = loadProc(load, "glGetUniformLocation", glad_glGetUniformLocation) && ok;
  ok = loadProc(load, "glUniform1i", glad_glUniform1i) && ok;
  ok = loadProc(load, "glUniform2f", glad_glUniform2f) && ok;
  ok = loadProc(load, "glUniform3f", glad_glUniform3f) && ok;
  ok = loadProc(load, "glUniform4f", glad_glUniform4f) && ok;
  ok = loadProc(load, "glUniform1f", glad_glUniform1f) && ok;
  ok = loadProc(load, "glUniform3fv", glad_glUniform3fv) && ok;
  ok = loadProc(load, "glUniform4fv", glad_glUniform4fv) && ok;
  ok = loadProc(load, "glGenVertexArrays", glad_glGenVertexArrays) && ok;
  ok = loadProc(load, "glBindVertexArray", glad_glBindVertexArray) && ok;
  ok = loadProc(load, "glDeleteVertexArrays", glad_glDeleteVertexArrays) && ok;
  ok = loadProc(load, "glGenBuffers", glad_glGenBuffers) && ok;
  ok = loadProc(load, "glBindBuffer", glad_glBindBuffer) && ok;
  ok = loadProc(load, "glBufferData", glad_glBufferData) && ok;
  ok = loadProc(load, "glDeleteBuffers", glad_glDeleteBuffers) && ok;
  ok = loadProc(load, "glEnableVertexAttribArray", glad_glEnableVertexAttribArray) && ok;
  ok = loadProc(load, "glVertexAttribPointer", glad_glVertexAttribPointer) && ok;
  ok = loadProc(load, "glVertexAttribDivisor", glad_glVertexAttribDivisor) && ok;
  ok = loadProc(load, "glDrawArraysInstanced", glad_glDrawArraysInstanced) && ok;
  ok = loadProc(load, "glActiveTexture", glad_glActiveTexture) && ok;
  ok = loadProc(load, "glGenFramebuffers", glad_glGenFramebuffers) && ok;
  ok = loadProc(load, "glBindFramebuffer", glad_glBindFramebuffer) && ok;
  ok = loadProc(load, "glDeleteFramebuffers", glad_glDeleteFramebuffers) && ok;
  ok = loadProc(load, "glFramebufferTexture2D", glad_glFramebufferTexture2D) && ok;
  ok = loadProc(load, "glGenRenderbuffers", glad_glGenRenderbuffers) && ok;
  ok = loadProc(load, "glBindRenderbuffer", glad_glBindRenderbuffer) && ok;
  ok = loadProc(load, "glRenderbufferStorage", glad_glRenderbufferStorage) && ok;
  ok = loadProc(load, "glFramebufferRenderbuffer", glad_glFramebufferRenderbuffer) && ok;
  ok = loadProc(load, "glDeleteRenderbuffers", glad_glDeleteRenderbuffers) && ok;
  ok = loadProc(load, "glCheckFramebufferStatus", glad_glCheckFramebufferStatus) && ok;

  GLAD_GL_VERSION_3_3 = ok ? 1 : 0;
  return GLAD_GL_VERSION_3_3;
}
