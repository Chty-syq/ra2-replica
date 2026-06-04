#include "iso_grid_renderer.h"

#include "gl_loader.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace {
GLuint compileShader(const GLenum type, const char* source) {
  const GLuint shader = glCreateShaderPtr(type);
  glShaderSourcePtr(shader, 1, &source, nullptr);
  glCompileShaderPtr(shader);

  GLint status = GL_FALSE;
  glGetShaderivPtr(shader, GL_COMPILE_STATUS, &status);
  if (status == GL_TRUE) {
    return shader;
  }

  GLint logLength = 0;
  glGetShaderivPtr(shader, GL_INFO_LOG_LENGTH, &logLength);
  std::string log(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
  glGetShaderInfoLogPtr(shader, logLength, nullptr, log.data());
  glDeleteShaderPtr(shader);
  throw std::runtime_error("Failed to compile iso grid shader: " + log);
}

GLuint createProgram() {
  constexpr const char* vertexSource = R"(#version 330 core
const vec2 fullscreenTriangle[3] = vec2[3](
  vec2(-1.0, -1.0),
  vec2( 3.0, -1.0),
  vec2(-1.0,  3.0)
);

void main() {
  gl_Position = vec4(fullscreenTriangle[gl_VertexID], 0.0, 1.0);
}
)";

  constexpr const char* fragmentSource = R"(#version 330 core
uniform vec2 uViewport;
uniform vec2 uOrigin;
uniform vec2 uTileSize;

layout(location = 0) out vec4 FragColor;

vec2 screenToLogical(vec2 screen) {
  vec2 halfTile = uTileSize * 0.5;
  float dx = (screen.x - uOrigin.x) / halfTile.x;
  float dy = (screen.y - uOrigin.y) / halfTile.y;
  return vec2((dy + dx) * 0.5, (dy - dx) * 0.5);
}

void main() {
  vec2 screen = vec2(gl_FragCoord.x, uViewport.y - gl_FragCoord.y);
  vec2 logical = screenToLogical(screen);

  vec2 tile = floor(logical + vec2(0.5));
  float checker = mod(tile.x + tile.y, 2.0);
  vec3 fillEven = vec3(0.46, 0.47, 0.50);
  vec3 fillOdd = vec3(0.41, 0.42, 0.45);
  vec3 baseColor = mix(fillEven, fillOdd, checker);

  vec2 derivative = max(fwidth(logical), vec2(0.0001));
  vec2 distanceToGrid = abs(fract(logical) - vec2(0.5));
  vec2 normalizedDistance = distanceToGrid / derivative;
  float lineCoverage = 1.0 - min(min(normalizedDistance.x, normalizedDistance.y), 1.0);

  vec3 lineColor = vec3(0.78, 0.80, 0.84);
  vec3 color = mix(baseColor, lineColor, lineCoverage * 0.48);
  FragColor = vec4(color, 1.0);
}
)";

  const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
  const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);

  const GLuint program = glCreateProgramPtr();
  glAttachShaderPtr(program, vertexShader);
  glAttachShaderPtr(program, fragmentShader);
  glLinkProgramPtr(program);

  glDeleteShaderPtr(vertexShader);
  glDeleteShaderPtr(fragmentShader);

  GLint status = GL_FALSE;
  glGetProgramivPtr(program, GL_LINK_STATUS, &status);
  if (status == GL_TRUE) {
    return program;
  }

  GLint logLength = 0;
  glGetProgramivPtr(program, GL_INFO_LOG_LENGTH, &logLength);
  std::string log(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
  glGetProgramInfoLogPtr(program, logLength, nullptr, log.data());
  glDeleteProgramPtr(program);
  throw std::runtime_error("Failed to link iso grid shader program: " + log);
}
}  // namespace

IsoGridRenderer::~IsoGridRenderer() {
  destroy();
}

void IsoGridRenderer::initialize(const int viewportWidth, const int viewportHeight) {
  destroy();
  ensureOpenGlLoaded();

  viewportWidth_ = viewportWidth;
  viewportHeight_ = viewportHeight;
  program_ = createProgram();
  viewportUniform_ = glGetUniformLocationPtr(program_, "uViewport");
  originUniform_ = glGetUniformLocationPtr(program_, "uOrigin");
  tileSizeUniform_ = glGetUniformLocationPtr(program_, "uTileSize");

  glGenVertexArraysPtr(1, &vao_);
  initialized_ = true;
}

void IsoGridRenderer::destroy() {
  if (vao_ != 0) {
    glDeleteVertexArraysPtr(1, &vao_);
    vao_ = 0;
  }
  if (program_ != 0) {
    glDeleteProgramPtr(program_);
    program_ = 0;
  }

  initialized_ = false;
  viewportWidth_ = 0;
  viewportHeight_ = 0;
  viewportUniform_ = -1;
  originUniform_ = -1;
  tileSizeUniform_ = -1;
}

void IsoGridRenderer::setViewport(const int viewportWidth, const int viewportHeight) {
  ensureInitialized();
  viewportWidth_ = viewportWidth;
  viewportHeight_ = viewportHeight;
}

void IsoGridRenderer::draw(const Vec2 origin, const float tileWidth, const float tileHeight) {
  ensureInitialized();
  if (viewportWidth_ <= 0 || viewportHeight_ <= 0 || tileWidth <= 0.0f || tileHeight <= 0.0f) {
    return;
  }

  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glUseProgramPtr(program_);
  glUniform2fPtr(viewportUniform_, static_cast<float>(viewportWidth_), static_cast<float>(viewportHeight_));
  glUniform2fPtr(originUniform_, origin.x, origin.y);
  glUniform2fPtr(tileSizeUniform_, tileWidth, tileHeight);

  glBindVertexArrayPtr(vao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArrayPtr(0);
}

void IsoGridRenderer::ensureInitialized() const {
  if (!initialized_) {
    throw std::runtime_error("IsoGridRenderer is not initialized");
  }
}
