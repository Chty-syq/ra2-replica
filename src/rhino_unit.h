#pragma once

#include "iso_world.h"
#include "map_grid.h"
#include "palette.h"
#include "renderer2d.h"
#include "ui_texture.h"

#include <cstdint>
#include <filesystem>
#include <vector>

class VplBoxRenderer;
struct VplBoxRendererState;

// 参考：
// - F:\projects\JavaRedAlert2\src\main\java\redAlert\other\VehicleBloodBar.java
// - F:\projects\JavaRedAlert2\src\main\java\redAlert\MouseEventDeal.java
//
// 这里不照搬 Swing 组件实现，而是把“选中后显示血条 / 右键下达移动命令”
// 这两个交互语义翻译成当前 SDL + OpenGL 主项目里的最小可用版本。

// Rhino 当前用到的单位 UI 资源。
// 这一轮只保留血条相关资源，不再绘制选中圈和 waypoint。
struct RhinoUnitUiAssets {
  UiTexture healthBorder;
  UiTexture healthPipGreen;
  UiTexture healthPipYellow;
  UiTexture healthPipRed;
};

// Rhino 的运行时状态。
// 这里同时承载：
// - 游戏层状态：选中、血量、移动目标
// - 表现层状态：连续位置、朝向、移动命令提示
struct RhinoUnitState {
  Vec2 tilePosition{7.0f, 10.0f};
  TileCoord occupiedCell{7, 10};
  float headingRadians = 0.0f;
  bool selected = false;
  int maxHp = 700;
  int hp = 700;
  std::vector<TileCoord> path;
  TileCoord moveTarget{7, 10};
  TileCoord steeringTarget{7, 10};
  int steeringDirectionIndex = 0;
  bool hasPendingMove = false;
  TileCoord pendingMoveTarget{7, 10};
  std::uint32_t waypointVisibleUntilTicks = 0;
};

[[nodiscard]] RhinoUnitUiAssets loadRhinoUnitUiAssets(const std::filesystem::path& overlayRoot,
                                                      const Palette& overlayPalette);
void destroyRhinoUnitUiAssets(RhinoUnitUiAssets& assets);

void initializeRhinoUnit(RhinoUnitState& unit, MapGrid& map, TileCoord startCell);

[[nodiscard]] bool hitTestRhinoUnit(const RhinoUnitState& unit,
                                    float mouseX,
                                    float mouseY,
                                    Vec2 origin,
                                    float tileWidth,
                                    float tileHeight);

void updateRhinoUnit(RhinoUnitState& unit, MapGrid& map, float deltaSeconds);

[[nodiscard]] bool issueRhinoMoveCommand(RhinoUnitState& unit,
                                         const MapGrid& map,
                                         TileCoord targetCell,
                                         std::uint32_t nowTicks);

[[nodiscard]] int rhinoDirectionIndex(const RhinoUnitState& unit);

void drawRhinoHealthBar(Renderer2D& renderer,
                        const RhinoUnitUiAssets& assets,
                        const RhinoUnitState& unit,
                        Vec2 origin,
                        float tileWidth,
                        float tileHeight);

[[nodiscard]] Vec2 rhinoGroundAnchor(const RhinoUnitState& unit,
                                     Vec2 origin,
                                     float tileWidth,
                                     float tileHeight);

void drawRhinoUnit(Renderer2D& renderer,
                   VplBoxRenderer& rhinoRenderer,
                   const RhinoUnitUiAssets& uiAssets,
                   const RhinoUnitState& unit,
                   Vec2 origin,
                   float tileWidth,
                   float tileHeight,
                   float footprintK,
                   int viewportWidth,
                   int viewportHeight,
                   std::uint32_t nowTicks,
                   const VplBoxRendererState& renderState);
