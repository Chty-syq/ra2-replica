#include "renderer2d.h"
#include "gl_loader.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace {
constexpr float kDefaultBuildingMapBrightness = 0.82f;

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
  throw std::runtime_error("Failed to compile shader: " + log);
}

GLuint createProgram() {
  constexpr const char* vertexSource = R"(#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in float aDepth;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aColor;

uniform vec2 uViewport;

out vec2 vTexCoord;
out vec4 vColor;

void main() {
  vec2 ndc = vec2((aPosition.x / uViewport.x) * 2.0 - 1.0,
                  1.0 - (aPosition.y / uViewport.y) * 2.0);
  gl_Position = vec4(ndc, aDepth * 2.0 - 1.0, 1.0);
  vTexCoord = aTexCoord;
  vColor = aColor;
}
)";

  constexpr const char* fragmentSource = R"(#version 330 core
in vec2 vTexCoord;
in vec4 vColor;

uniform sampler2D uTexture;
uniform sampler2D uPaletteTexture;
uniform sampler2D uLogicalCoordTexture;
uniform int uTextureMode;
uniform int uEnableRemap;
uniform int uUseLogicalDepth;
uniform vec4 uRemapColor;
uniform vec2 uLogicalAnchor;
uniform vec2 uLogicalTileSize;
uniform int uFootprintCount;
uniform vec4 uFootprintOffsetsPacked[8];

layout(location = 0) out vec4 FragColor;

vec4 remapStageColor(vec4 baseColor, int paletteIndex) {
  float stage = 1.0 - float(paletteIndex - 16) / 15.0;
  return vec4(uRemapColor.rgb * stage, baseColor.a);
}

float logicalDepthFromScreenOffset(vec2 screenOffset) {
  vec2 halfTile = vec2(uLogicalTileSize.x * 0.5, uLogicalTileSize.y * 0.5);
  float dx = screenOffset.x / halfTile.x;
  float dy = screenOffset.y / halfTile.y;

  vec2 logicalDelta = vec2((dy + dx) * 0.5, (dy - dx) * 0.5);
  vec2 logicalCoord = uLogicalAnchor + logicalDelta;

  if (uFootprintCount > 0) {
    float bestDistance = 1e30;
    vec2 bestLogical = logicalCoord;
    for (int i = 0; i < uFootprintCount && i < 16; ++i) {
      vec4 packedOffsets = uFootprintOffsetsPacked[i / 2];
      vec2 footprintOffset;
      if ((i % 2) == 0) {
        footprintOffset = packedOffsets.xy;
      } else {
        footprintOffset = packedOffsets.zw;
      }

      vec2 candidate = uLogicalAnchor + footprintOffset;
      vec2 delta = logicalCoord - candidate;
      float distance2 = dot(delta, delta);
      if (distance2 < bestDistance) {
        bestDistance = distance2;
        bestLogical = candidate;
      }
    }
    logicalCoord = bestLogical;
  }

  float basis =
      (logicalCoord.x + logicalCoord.y) * uLogicalTileSize.y * 0.5 +
      (logicalCoord.x - logicalCoord.y) * uLogicalTileSize.x * 0.5 * 0.001;
  return clamp(0.5 - basis / 16384.0, 0.001, 0.999);
}

void main() {
  vec4 color;
  if (uTextureMode == 0) {
    color = texture(uTexture, vTexCoord);
  } else {
    int paletteIndex = int(round(texture(uTexture, vTexCoord).r * 255.0));
    if (paletteIndex <= 0) {
      discard;
    }

    color = texelFetch(uPaletteTexture, ivec2(paletteIndex, 0), 0);
    if (uEnableRemap != 0 && paletteIndex >= 16 && paletteIndex <= 31) {
      color = remapStageColor(color, paletteIndex);
    }
  }

  color *= vColor;
  if (color.a < 0.01) {
    discard;
  }

  if (uUseLogicalDepth != 0) {
    vec2 screenOffset = texture(uLogicalCoordTexture, vTexCoord).rg;
    gl_FragDepth = logicalDepthFromScreenOffset(screenOffset);
  }

  FragColor = color;
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
  throw std::runtime_error("Failed to link shader program: " + log);
}

void uploadPaletteTexture(const GLuint texture, const Palette& palette, const PaletteSampleOptions& options) {
  std::array<std::uint8_t, 256 * 4> bytes{};
  for (int i = 0; i < 256; ++i) {
    const auto color = palette.sample(static_cast<std::uint8_t>(i), options);
    const std::size_t offset = static_cast<std::size_t>(i) * 4;
    bytes[offset + 0] = color.r;
    bytes[offset + 1] = color.g;
    bytes[offset + 2] = color.b;
    bytes[offset + 3] = color.a;
  }

  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, bytes.data());
}
}  // namespace

Renderer2D::~Renderer2D() {
  destroy();
}

void Renderer2D::initialize(const int viewportWidth, const int viewportHeight) {
  destroy();
  ensureOpenGlLoaded();

  viewportWidth_ = viewportWidth;
  viewportHeight_ = viewportHeight;
  program_ = createProgram();
  viewportUniform_ = glGetUniformLocationPtr(program_, "uViewport");
  textureUniform_ = glGetUniformLocationPtr(program_, "uTexture");
  paletteUniform_ = glGetUniformLocationPtr(program_, "uPaletteTexture");
  textureModeUniform_ = glGetUniformLocationPtr(program_, "uTextureMode");
  enableRemapUniform_ = glGetUniformLocationPtr(program_, "uEnableRemap");
  remapColorUniform_ = glGetUniformLocationPtr(program_, "uRemapColor");
  logicalCoordTextureUniform_ = glGetUniformLocationPtr(program_, "uLogicalCoordTexture");
  useLogicalDepthUniform_ = glGetUniformLocationPtr(program_, "uUseLogicalDepth");
  logicalAnchorUniform_ = glGetUniformLocationPtr(program_, "uLogicalAnchor");
  logicalTileSizeUniform_ = glGetUniformLocationPtr(program_, "uLogicalTileSize");
  footprintCountUniform_ = glGetUniformLocationPtr(program_, "uFootprintCount");
  footprintOffsetsUniform_ = glGetUniformLocationPtr(program_, "uFootprintOffsetsPacked");

  glGenVertexArraysPtr(1, &vao_);
  glBindVertexArrayPtr(vao_);

  glGenBuffersPtr(1, &vbo_);
  glBindBufferPtr(GL_ARRAY_BUFFER, vbo_);
  glBufferDataPtr(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(RenderVertex) * 6), nullptr, GL_DYNAMIC_DRAW);

  constexpr GLsizei stride = static_cast<GLsizei>(sizeof(RenderVertex));
  glEnableVertexAttribArrayPtr(0);
  glVertexAttribPointerPtr(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(RenderVertex, x)));
  glEnableVertexAttribArrayPtr(1);
  glVertexAttribPointerPtr(1, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(RenderVertex, z)));
  glEnableVertexAttribArrayPtr(2);
  glVertexAttribPointerPtr(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(RenderVertex, u)));
  glEnableVertexAttribArrayPtr(3);
  glVertexAttribPointerPtr(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(RenderVertex, r)));

  constexpr std::array<std::uint8_t, 4> whitePixel{255, 255, 255, 255};
  glGenTextures(1, &whiteTexture_);
  glBindTexture(GL_TEXTURE_2D, whiteTexture_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel.data());

  glGenTextures(1, &unitPaletteTexture_);
  glGenTextures(1, &terrainPaletteTexture_);

  glUseProgramPtr(program_);
  glUniform1iPtr(textureUniform_, 0);
  glUniform1iPtr(paletteUniform_, 1);
  glUniform1iPtr(logicalCoordTextureUniform_, 2);
  glUniform2fPtr(viewportUniform_, static_cast<float>(viewportWidth_), static_cast<float>(viewportHeight_));
  glUniform1iPtr(textureModeUniform_, 0);
  glUniform1iPtr(enableRemapUniform_, 0);
  glUniform1iPtr(useLogicalDepthUniform_, 0);
  glUniform4fPtr(remapColorUniform_, 1.0f, 1.0f, 1.0f, 1.0f);
  glUniform2fPtr(logicalAnchorUniform_, 0.0f, 0.0f);
  glUniform2fPtr(logicalTileSizeUniform_, 1.0f, 1.0f);
  glUniform1iPtr(footprintCountUniform_, 0);
  std::array<float, 8 * 4> zeroPackedOffsets{};
  glUniform4fvPtr(footprintOffsetsUniform_, 8, zeroPackedOffsets.data());

  glBindBufferPtr(GL_ARRAY_BUFFER, 0);
  glBindVertexArrayPtr(0);

  initialized_ = true;
}

void Renderer2D::destroy() {
  if (vbo_ != 0 && glDeleteBuffersPtr) {
    glDeleteBuffersPtr(1, &vbo_);
    vbo_ = 0;
  }
  if (vao_ != 0 && glDeleteVertexArraysPtr) {
    glDeleteVertexArraysPtr(1, &vao_);
    vao_ = 0;
  }
  if (program_ != 0 && glDeleteProgramPtr) {
    glDeleteProgramPtr(program_);
    program_ = 0;
  }
  if (whiteTexture_ != 0) {
    glDeleteTextures(1, &whiteTexture_);
    whiteTexture_ = 0;
  }
  if (unitPaletteTexture_ != 0) {
    glDeleteTextures(1, &unitPaletteTexture_);
    unitPaletteTexture_ = 0;
  }
  if (terrainPaletteTexture_ != 0) {
    glDeleteTextures(1, &terrainPaletteTexture_);
    terrainPaletteTexture_ = 0;
  }

  initialized_ = false;
}

void Renderer2D::setViewport(const int viewportWidth, const int viewportHeight) {
  ensureInitialized();
  viewportWidth_ = viewportWidth;
  viewportHeight_ = viewportHeight;
  glViewport(0, 0, viewportWidth_, viewportHeight_);
  glUseProgramPtr(program_);
  glUniform2fPtr(viewportUniform_, static_cast<float>(viewportWidth_), static_cast<float>(viewportHeight_));
}

void Renderer2D::beginFrame(const float clearR, const float clearG, const float clearB, const float clearA) {
  ensureInitialized();
  setViewport(viewportWidth_, viewportHeight_);
  glDepthMask(GL_TRUE);
  glClearColor(clearR, clearG, clearB, clearA);
  glClearDepth(1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer2D::beginWorldPass() {
  ensureInitialized();
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);
  glUseProgramPtr(program_);
  glUniform2fPtr(viewportUniform_, static_cast<float>(viewportWidth_), static_cast<float>(viewportHeight_));
}

void Renderer2D::beginUiPass() {
  ensureInitialized();
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glUseProgramPtr(program_);
  glUniform2fPtr(viewportUniform_, static_cast<float>(viewportWidth_), static_cast<float>(viewportHeight_));
}

void Renderer2D::draw(const GLenum primitiveMode,
                      const GLuint texture,
                      const RenderVertex* vertices,
                      const std::size_t vertexCount) {
  drawInternal(primitiveMode,
               texture,
               0,
               UiTextureColorMode::Rgba,
               UiTexturePaletteKind::None,
               false,
               nullptr,
               vertices,
               vertexCount);
}

void Renderer2D::draw(const GLenum primitiveMode,
                      const UiTexture& texture,
                      const RenderVertex* vertices,
                      const std::size_t vertexCount) {
  drawInternal(primitiveMode,
               texture.texture,
               texture.logicalCoordTexture,
               texture.colorMode,
               texture.paletteKind,
               texture.paletteRemap,
               nullptr,
               vertices,
               vertexCount);
}

void Renderer2D::draw(const GLenum primitiveMode,
                      const UiTexture& texture,
                      const RenderVertex* vertices,
                      const std::size_t vertexCount,
                      const LogicalDepthParams& logicalDepth) {
  drawInternal(primitiveMode,
               texture.texture,
               texture.logicalCoordTexture,
               texture.colorMode,
               texture.paletteKind,
               texture.paletteRemap,
               &logicalDepth,
               vertices,
               vertexCount);
}

void Renderer2D::setIndexedPalettes(const Palette& unitPalette, const Palette& terrainPalette) {
  ensureInitialized();
  uploadPaletteTexture(unitPaletteTexture_,
                       unitPalette,
                       PaletteSampleOptions{kDefaultBuildingMapBrightness, true});
  uploadPaletteTexture(terrainPaletteTexture_,
                       terrainPalette,
                       PaletteSampleOptions{kDefaultBuildingMapBrightness, false});
}

void Renderer2D::setRemapColor(const Rgba& color) noexcept {
  remapColor_ = color;
}

void Renderer2D::drawInternal(const GLenum primitiveMode,
                              const GLuint texture,
                              const GLuint logicalCoordTexture,
                              const UiTextureColorMode colorMode,
                              const UiTexturePaletteKind paletteKind,
                              const bool paletteRemap,
                              const LogicalDepthParams* logicalDepth,
                              const RenderVertex* vertices,
                              const std::size_t vertexCount) {
  ensureInitialized();
  glUseProgramPtr(program_);
  glBindVertexArrayPtr(vao_);
  glBindBufferPtr(GL_ARRAY_BUFFER, vbo_);
  glBufferDataPtr(GL_ARRAY_BUFFER,
                  static_cast<GLsizeiptr>(vertexCount * sizeof(RenderVertex)),
                  vertices,
                  GL_STREAM_DRAW);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);

  glUniform1iPtr(textureModeUniform_, colorMode == UiTextureColorMode::IndexedPalette ? 1 : 0);
  glUniform1iPtr(enableRemapUniform_, paletteRemap ? 1 : 0);
  glUniform4fPtr(remapColorUniform_,
                 remapColor_.r / 255.0f,
                 remapColor_.g / 255.0f,
                 remapColor_.b / 255.0f,
                 remapColor_.a / 255.0f);

  const bool useLogicalDepth = logicalDepth != nullptr && logicalCoordTexture != 0;
  glUniform1iPtr(useLogicalDepthUniform_, useLogicalDepth ? 1 : 0);
  if (useLogicalDepth) {
    glUniform2fPtr(logicalAnchorUniform_, logicalDepth->logicalAnchorX, logicalDepth->logicalAnchorY);
    glUniform2fPtr(logicalTileSizeUniform_, logicalDepth->tileWidth, logicalDepth->tileHeight);
    glUniform1iPtr(footprintCountUniform_, logicalDepth->footprintCount);

    std::array<float, 8 * 4> packedOffsets{};
    for (int i = 0; i < logicalDepth->footprintCount && i < LogicalDepthParams::kMaxFootprintTiles; ++i) {
      const std::size_t src = static_cast<std::size_t>(i) * 2;
      const std::size_t dst = static_cast<std::size_t>(i / 2) * 4 + (((i % 2) == 0) ? 0 : 2);
      packedOffsets[dst + 0] = logicalDepth->footprintOffsets[src + 0];
      packedOffsets[dst + 1] = logicalDepth->footprintOffsets[src + 1];
    }
    glUniform4fvPtr(footprintOffsetsUniform_, 8, packedOffsets.data());
  } else {
    glUniform1iPtr(footprintCountUniform_, 0);
    std::array<float, 8 * 4> zeroPackedOffsets{};
    glUniform4fvPtr(footprintOffsetsUniform_, 8, zeroPackedOffsets.data());
  }

  glActiveTexture(GL_TEXTURE1);
  switch (paletteKind) {
    case UiTexturePaletteKind::Unit:
      glBindTexture(GL_TEXTURE_2D, unitPaletteTexture_);
      break;
    case UiTexturePaletteKind::Terrain:
      glBindTexture(GL_TEXTURE_2D, terrainPaletteTexture_);
      break;
    case UiTexturePaletteKind::None:
      glBindTexture(GL_TEXTURE_2D, whiteTexture_);
      break;
  }

  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, useLogicalDepth ? logicalCoordTexture : 0);

  glDrawArrays(primitiveMode, 0, static_cast<GLsizei>(vertexCount));

  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindBufferPtr(GL_ARRAY_BUFFER, 0);
  glBindVertexArrayPtr(0);
}

void Renderer2D::ensureInitialized() const {
  if (!initialized_) {
    throw std::runtime_error("Renderer2D is not initialized");
  }
}
