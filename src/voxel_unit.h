#pragma once

#include "iso_world.h"
#include "vpl_box_renderer.h"

// 把逻辑地块位置翻译成 VXL 渲染器需要的屏幕锚点与深度范围。
// 这里既支持整数格坐标，也支持移动中的连续浮点位置。
void drawDirectVoxelUnitInstance(VplBoxRenderer& renderer,
                                 const Vec2& tilePosition,
                                 Vec2 origin,
                                 float tileWidth,
                                 float tileHeight,
                                 float footprintK,
                                 int viewportWidth,
                                 int viewportHeight,
                                 const VplBoxRendererState& renderState);

inline void drawDirectVoxelUnitInstance(VplBoxRenderer& renderer,
                                        const TileCoord& cell,
                                        const Vec2 origin,
                                        const float tileWidth,
                                        const float tileHeight,
                                        const float footprintK,
                                        const int viewportWidth,
                                        const int viewportHeight,
                                        const VplBoxRendererState& renderState) {
  drawDirectVoxelUnitInstance(renderer,
                              Vec2{static_cast<float>(cell.x), static_cast<float>(cell.y)},
                              origin,
                              tileWidth,
                              tileHeight,
                              footprintK,
                              viewportWidth,
                              viewportHeight,
                              renderState);
}
