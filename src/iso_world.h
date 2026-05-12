#pragma once

#include "building_system.h"
#include "renderer2d.h"

#include <cstdint>
#include <vector>

// 等距世界里反复传递的轻量 2D 点结构。
struct Vec2 {
  float x = 0.0f;
  float y = 0.0f;
};

// 世界层统一消费的建筑绘制命令。
// 把深度和 tint 独立出来后，预览建筑也能复用同一条绘制路径。
struct BuildingRenderCommand {
  const BuildingAsset* asset = nullptr;
  BuildingInstance instance;
  float depth01 = 0.5f;
  float tintR = 1.0f;
  float tintG = 1.0f;
  float tintB = 1.0f;
  float tintA = 1.0f;
};

// 地图格坐标 -> 屏幕坐标。
[[nodiscard]] Vec2 isoToScreen(int tileX, int tileY, Vec2 origin, float tileWidth, float tileHeight);

// 屏幕坐标 -> 近似地图格坐标。
[[nodiscard]] TileCoord screenToIso(float screenX, float screenY, Vec2 origin, float tileWidth, float tileHeight);

// 以锚点格为基准，生成一个 BuildingPlacement。
[[nodiscard]] BuildingPlacement placementFromAnchor(const TileCoord& anchor, const FoundationSize& foundation);

// 返回当前 demo 采用的建筑占地格集合。
[[nodiscard]] std::vector<TileCoord> foundationTiles(const BuildingPlacement& placement);

// 用地图逻辑坐标里的“占地中心点”计算深度。
// 这里不直接依赖 sprite 包围盒，也不再依赖“最南侧锚点”这类特例，
// 这样建筑和载具都能走同一套排序语义。
[[nodiscard]] float depthFromLogicalCenter(Vec2 logicalCenter, float tileWidth, float tileHeight);

// 根据 footprint 类型和 SHP 画布尺寸，计算建筑精灵对齐到地图时使用的锚点。
[[nodiscard]] Vec2 spriteAnchorInFrame(const DecodedFrame& frame, const FoundationSize& foundation);

// 把建筑实例包装成渲染命令，并计算深度和 tint。
[[nodiscard]] BuildingRenderCommand makeBuildingRenderCommand(const BuildingAsset& asset,
                                                             const BuildingInstance& instance,
                                                             Vec2 origin,
                                                             float tileWidth,
                                                             float tileHeight,
                                                             float tintR = 1.0f,
                                                             float tintG = 1.0f,
                                                             float tintB = 1.0f,
                                                             float tintA = 1.0f);

// 按格子绘制占地预览，并对每个格子单独显示是否可放置。
void drawPlacementPreview(Renderer2D& renderer,
                          const BuildingPlacement& placement,
                          const MapGrid& map,
                          Vec2 origin,
                          float tileWidth,
                          float tileHeight);

// 在世界层绘制建筑实例：
// - 建造动画
// - 完成态工作层
// - 建筑阴影
// - sprite 与基础占地的锚点对齐
void drawBuildingInstance(Renderer2D& renderer,
                          const BuildingAsset& asset,
                          const BuildingInstance& instance,
                          Vec2 origin,
                          float tileWidth,
                          float tileHeight,
                          std::uint32_t nowTicks,
                          float depth01,
                          float tintR = 1.0f,
                          float tintG = 1.0f,
                          float tintB = 1.0f,
                          float tintA = 1.0f);

// 仅重绘建筑本体与动画层，不绘制地面阴影。
// 这个入口主要给“建筑重绘遮挡 pass”使用：先画单位，再把会挡住单位的建筑前景局部重画到前面。
void drawBuildingForeground(Renderer2D& renderer,
                            const BuildingAsset& asset,
                            const BuildingInstance& instance,
                            Vec2 origin,
                            float tileWidth,
                            float tileHeight,
                            std::uint32_t nowTicks,
                            float depth01,
                            float tintR = 1.0f,
                            float tintG = 1.0f,
                            float tintB = 1.0f,
                            float tintA = 1.0f);

// 绘制当前 demo 使用的无限灰色等距地面网格。
void drawInfiniteIsoGroundGrid(Renderer2D& renderer,
                               Vec2 origin,
                               float tileWidth,
                               float tileHeight,
                               int viewportWidth,
                               int viewportHeight);
