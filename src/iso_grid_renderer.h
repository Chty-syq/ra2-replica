#pragma once

#include "iso_world.h"

// GPU 版无界等距网格。
//
// 旧实现会在 CPU 侧为视口内每个地块分别画菱形填充和 4 条边线，
// 1280x720 下很容易接近两万次 draw call/帧。这个渲染器改为只画
// 一个覆盖屏幕的全屏三角形，再在 fragment shader 中按屏幕坐标
// 反算逻辑格坐标，直接生成棋盘地块和网格线。
class IsoGridRenderer {
public:
  IsoGridRenderer() = default;
  IsoGridRenderer(const IsoGridRenderer&) = delete;
  IsoGridRenderer& operator=(const IsoGridRenderer&) = delete;
  ~IsoGridRenderer();

  void initialize(int viewportWidth, int viewportHeight);
  void destroy();
  void setViewport(int viewportWidth, int viewportHeight);
  void draw(Vec2 origin, float tileWidth, float tileHeight);

private:
  void ensureInitialized() const;

  bool initialized_ = false;
  int viewportWidth_ = 0;
  int viewportHeight_ = 0;
  unsigned int program_ = 0;
  unsigned int vao_ = 0;
  int viewportUniform_ = -1;
  int originUniform_ = -1;
  int tileSizeUniform_ = -1;
};
