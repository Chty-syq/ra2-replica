#pragma once

#include "palette.h"
#include "ui_texture.h"

#include <SDL_opengl.h>

#include <array>
#include <cstddef>

struct RenderVertex {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float u = 0.0f;
  float v = 0.0f;
  float r = 1.0f;
  float g = 1.0f;
  float b = 1.0f;
  float a = 1.0f;
};

struct LogicalDepthParams {
  static constexpr int kMaxFootprintTiles = 16;
  float logicalAnchorX = 0.0f;
  float logicalAnchorY = 0.0f;
  float tileWidth = 1.0f;
  float tileHeight = 1.0f;
  int footprintCount = 0;
  std::array<float, kMaxFootprintTiles * 2> footprintOffsets{};
};

// 项目里绝大多数 2D 内容都通过这层绘制：
// - 建筑贴图
// - 地面网格
// - 右侧侧栏
// - 调试叠加元素
//
// 它本质上是一个很薄的 OpenGL 2D 画笔：
// 上层准备好顶点、纹理和深度值，再交给它统一送进 GPU。
class Renderer2D {
public:
  Renderer2D() = default;
  Renderer2D(const Renderer2D&) = delete;
  Renderer2D& operator=(const Renderer2D&) = delete;
  ~Renderer2D();

  void initialize(int viewportWidth, int viewportHeight);
  void destroy();

  void setViewport(int viewportWidth, int viewportHeight);
  void beginFrame(float clearR, float clearG, float clearB, float clearA);
  void beginWorldPass();
  void beginUiPass();

  void draw(GLenum primitiveMode, GLuint texture, const RenderVertex* vertices, std::size_t vertexCount);
  void draw(GLenum primitiveMode, const UiTexture& texture, const RenderVertex* vertices, std::size_t vertexCount);
  void draw(GLenum primitiveMode,
            const UiTexture& texture,
            const RenderVertex* vertices,
            std::size_t vertexCount,
            const LogicalDepthParams& logicalDepth);

  void setIndexedPalettes(const Palette& unitPalette, const Palette& terrainPalette);
  void setRemapColor(const Rgba& color) noexcept;

  [[nodiscard]] GLuint whiteTexture() const noexcept { return whiteTexture_; }

private:
  void drawInternal(GLenum primitiveMode,
                    GLuint texture,
                    GLuint logicalCoordTexture,
                    UiTextureColorMode colorMode,
                    UiTexturePaletteKind paletteKind,
                    bool paletteRemap,
                    const LogicalDepthParams* logicalDepth,
                    const RenderVertex* vertices,
                    std::size_t vertexCount);
  void ensureInitialized() const;

  bool initialized_ = false;
  int viewportWidth_ = 0;
  int viewportHeight_ = 0;
  GLuint program_ = 0;
  GLuint vao_ = 0;
  GLuint vbo_ = 0;
  GLuint whiteTexture_ = 0;
  GLuint unitPaletteTexture_ = 0;
  GLuint terrainPaletteTexture_ = 0;
  GLint viewportUniform_ = -1;
  GLint textureUniform_ = -1;
  GLint paletteUniform_ = -1;
  GLint textureModeUniform_ = -1;
  GLint enableRemapUniform_ = -1;
  GLint remapColorUniform_ = -1;
  GLint logicalCoordTextureUniform_ = -1;
  GLint useLogicalDepthUniform_ = -1;
  GLint logicalAnchorUniform_ = -1;
  GLint logicalTileSizeUniform_ = -1;
  GLint footprintCountUniform_ = -1;
  GLint footprintOffsetsUniform_ = -1;
  Rgba remapColor_{255, 255, 255, 255};
};
