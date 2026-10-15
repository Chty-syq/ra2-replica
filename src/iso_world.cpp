#include "iso_world.h"
#include "foundation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {
[[nodiscard]] bool defersOverUnitLayersUntilAfterUnits(const BuildingAsset& asset) {
  return asset.art.deferOverUnit;
}

[[nodiscard]] bool isUnderUnitLayer(const BuildingAsset::LayerRole role) {
  return role == BuildingAsset::LayerRole::UnderUnit ||
         role == BuildingAsset::LayerRole::ProductionUnderUnit;
}

[[nodiscard]] bool isProductionLayer(const BuildingAsset::LayerRole role) {
  return role == BuildingAsset::LayerRole::ProductionUnderUnit ||
         role == BuildingAsset::LayerRole::ProductionOverUnit;
}


// 用占地格中心来表达“这个对象在地图逻辑平面里的位置”。
// 对建筑来说，这个中心点来自所有占地格中心的平均值；
// 对 1x1 载具来说，直接就是它当前所在的逻辑坐标。
Vec2 logicalCenterForPlacement(const BuildingPlacement& placement) {
  const auto coords = foundationTiles(placement);
  if (coords.empty()) {
    return Vec2{
      static_cast<float>(placement.topLeft.x),
      static_cast<float>(placement.topLeft.y)
    };
  }

  float sumX = 0.0f;
  float sumY = 0.0f;
  for (const auto coord : coords) {
    sumX += static_cast<float>(coord.x);
    sumY += static_cast<float>(coord.y);
  }

  const float invCount = 1.0f / static_cast<float>(coords.size());
  return Vec2{sumX * invCount, sumY * invCount};
}

// 估算建筑在屏幕空间里的“接地宽度”。
// demo 会用这个宽度来控制阴影大小，让大建筑看起来比小建筑更有分量。
float foundationContactSpan(const BuildingPlacement& placement, const float tileWidth) {
  const auto coords = foundationTiles(placement);
  if (coords.empty()) {
    return tileWidth * 0.5f;
  }

  float minScreenX = std::numeric_limits<float>::infinity();
  float maxScreenX = -std::numeric_limits<float>::infinity();
  for (const auto coord : coords) {
    const float screenX = static_cast<float>(coord.x - coord.y) * tileWidth * 0.5f;
    minScreenX = std::min(minScreenX, screenX - tileWidth * 0.5f);
    maxScreenX = std::max(maxScreenX, screenX + tileWidth * 0.5f);
  }

  return std::max(tileWidth * 0.5f, maxScreenX - minScreenX);
}

[[nodiscard]] std::array<RenderVertex, 6> makeQuadVertices(const Vec2 topLeft,
                                                           const Vec2 topRight,
                                                           const Vec2 bottomRight,
                                                           const Vec2 bottomLeft,
                                                           const float depth01,
                                                           const float r,
                                                           const float g,
                                                           const float b,
                                                           const float a) {
  return {{
    {topLeft.x, topLeft.y, depth01, 0.0f, 0.0f, r, g, b, a},
    {topRight.x, topRight.y, depth01, 1.0f, 0.0f, r, g, b, a},
    {bottomRight.x, bottomRight.y, depth01, 1.0f, 1.0f, r, g, b, a},
    {topLeft.x, topLeft.y, depth01, 0.0f, 0.0f, r, g, b, a},
    {bottomRight.x, bottomRight.y, depth01, 1.0f, 1.0f, r, g, b, a},
    {bottomLeft.x, bottomLeft.y, depth01, 0.0f, 1.0f, r, g, b, a}
  }};
}

// 最小四边形绘制辅助函数，同时服务于建筑本体和投影阴影。
void drawTexturedQuad(Renderer2D& renderer,
                      const UiTexture& texture,
                      const Vec2 topLeft,
                      const Vec2 topRight,
                      const Vec2 bottomRight,
                      const Vec2 bottomLeft,
                      const float r = 1.0f,
                      const float g = 1.0f,
                      const float b = 1.0f,
                      const float a = 1.0f,
                      const float depth01 = 0.5f) {
  const RenderVertex vertices[] = {
    {topLeft.x, topLeft.y, depth01, 0.0f, 0.0f, r, g, b, a},
    {topRight.x, topRight.y, depth01, 1.0f, 0.0f, r, g, b, a},
    {bottomRight.x, bottomRight.y, depth01, 1.0f, 1.0f, r, g, b, a},
    {topLeft.x, topLeft.y, depth01, 0.0f, 0.0f, r, g, b, a},
    {bottomRight.x, bottomRight.y, depth01, 1.0f, 1.0f, r, g, b, a},
    {bottomLeft.x, bottomLeft.y, depth01, 0.0f, 1.0f, r, g, b, a}
  };
  renderer.draw(GL_TRIANGLES, texture, vertices, sizeof(vertices) / sizeof(vertices[0]));
}

// 地面格填充辅助函数，地图网格和占地预览都复用它。
void drawDiamondFill(Renderer2D& renderer,
                     const Vec2 center,
                     const float tileWidth,
                     const float tileHeight,
                     const float r,
                     const float g,
                     const float b,
                     const float a) {
  const float halfW = tileWidth * 0.5f;
  const float halfH = tileHeight * 0.5f;
  const RenderVertex vertices[] = {
    {center.x, center.y - halfH, 0.999f, 0.0f, 0.0f, r, g, b, a},
    {center.x + halfW, center.y, 0.999f, 1.0f, 0.0f, r, g, b, a},
    {center.x, center.y + halfH, 0.999f, 1.0f, 1.0f, r, g, b, a},
    {center.x, center.y - halfH, 0.999f, 0.0f, 0.0f, r, g, b, a},
    {center.x, center.y + halfH, 0.999f, 1.0f, 1.0f, r, g, b, a},
    {center.x - halfW, center.y, 0.999f, 0.0f, 1.0f, r, g, b, a}
  };
  renderer.draw(GL_TRIANGLES, renderer.whiteTexture(), vertices, sizeof(vertices) / sizeof(vertices[0]));
}

void drawDiamondOutline(Renderer2D& renderer,
                        const Vec2 center,
                        const float tileWidth,
                        const float tileHeight,
                        const float r,
                        const float g,
                        const float b,
                        const float a) {
  const float halfW = tileWidth * 0.5f;
  const float halfH = tileHeight * 0.5f;
  const RenderVertex vertices[] = {
    {center.x, center.y - halfH, 0.999f, 0.0f, 0.0f, r, g, b, a},
    {center.x + halfW, center.y, 0.999f, 0.0f, 0.0f, r, g, b, a},
    {center.x, center.y + halfH, 0.999f, 0.0f, 0.0f, r, g, b, a},
    {center.x - halfW, center.y, 0.999f, 0.0f, 0.0f, r, g, b, a}
  };
  renderer.draw(GL_LINE_LOOP, renderer.whiteTexture(), vertices, sizeof(vertices) / sizeof(vertices[0]));
}

// 对 renderer 线段模式的薄封装，让所有网格线生成逻辑都留在这个文件内。
void drawLine(Renderer2D& renderer,
              const Vec2 from,
              const Vec2 to,
              const float r,
              const float g,
              const float b,
              const float a) {
  const RenderVertex vertices[] = {
    {from.x, from.y, 0.999f, 0.0f, 0.0f, r, g, b, a},
    {to.x, to.y, 0.999f, 0.0f, 0.0f, r, g, b, a}
  };
  renderer.draw(GL_LINES, renderer.whiteTexture(), vertices, sizeof(vertices) / sizeof(vertices[0]));
}
}  // 匿名命名空间

Vec2 spriteAnchorInFrame(const DecodedFrame& frame) {
  return {static_cast<float>(frame.width / 2), static_cast<float>(frame.height / 2)};
}

Vec2 buildingDrawCenter(const BuildingPlacement& placement, const Vec2 origin,
                        const float tileWidth, const float tileHeight) {
  auto center = isoToScreen(placement.topLeft.x, placement.topLeft.y, origin, tileWidth, tileHeight);
  center.y -= tileHeight * 0.5f;
  return center;
}

Vec2 buildingSpriteTopLeft(const Vec2 drawCenter, const int canvasWidth, const int canvasHeight, const PixelOffset offset) {
  return {drawCenter.x - canvasWidth / 2 + offset.x, drawCenter.y - canvasHeight / 2 + offset.y};
}

LogicalDepthParams buildingLogicalDepth(const BuildingPlacement& placement, const float tileWidth, const float tileHeight) {
  // The drawing origin is the starting cell's upper vertex, i.e. (-0.5,-0.5)
  // relative to its center. Footprint offsets must use that same origin.
  LogicalDepthParams depth{static_cast<float>(placement.topLeft.x) - 0.5f,
                           static_cast<float>(placement.topLeft.y) - 0.5f, tileWidth, tileHeight};
  const auto cells = foundationTiles(placement);
  depth.footprintCount = std::min<int>(static_cast<int>(cells.size()), LogicalDepthParams::kMaxFootprintTiles);
  for (int i = 0; i < depth.footprintCount; ++i) {
    depth.footprintOffsets[i * 2] = static_cast<float>(cells[i].x - placement.topLeft.x) + 0.5f;
    depth.footprintOffsets[i * 2 + 1] = static_cast<float>(cells[i].y - placement.topLeft.y) + 0.5f;
  }
  return depth;
}

Vec2 isoToScreen(const int tileX, const int tileY, const Vec2 origin, const float tileWidth, const float tileHeight) {
  return Vec2{
    origin.x + (static_cast<float>(tileX - tileY) * tileWidth * 0.5f),
    origin.y + (static_cast<float>(tileX + tileY) * tileHeight * 0.5f)
  };
}

TileCoord screenToIso(const float screenX, const float screenY, const Vec2 origin, const float tileWidth, const float tileHeight) {
  const float dx = (screenX - origin.x) / (tileWidth * 0.5f);
  const float dy = (screenY - origin.y) / (tileHeight * 0.5f);
  const int tileX = static_cast<int>(std::lround((dy + dx) * 0.5f));
  const int tileY = static_cast<int>(std::lround((dy - dx) * 0.5f));
  return TileCoord{tileX, tileY};
}

BuildingPlacement placementFromAnchor(const TileCoord& anchor, const FoundationSize& foundation) {
  return BuildingPlacement{anchor, foundation.width, foundation.height};
}

BuildingRenderCommand makeBuildingRenderCommand(const BuildingAsset& asset,
                                                const BuildingInstance& instance,
                                                const Vec2 origin,
                                                const float tileWidth,
                                                const float tileHeight,
                                                const float tintR,
                                                const float tintG,
                                                const float tintB,
                                                const float tintA) {
  (void)origin;
  const auto logicalCenter = logicalCenterForPlacement(instance.placement);
  return BuildingRenderCommand{
    &asset,
    instance,
    depthFromLogicalCenter(logicalCenter, tileWidth, tileHeight),
    tintR,
    tintG,
    tintB,
    tintA
  };
}

float depthFromLogicalCenter(const Vec2 logicalCenter, const float tileWidth, const float tileHeight) {
  // 把“逻辑中心点”投影成一个仅用于排序的 basis。
  // 这里不关心屏幕原点，只保留：
  // - (x + y)：决定物体整体离镜头远近
  // - (x - y)：仅作为微弱的平局打破项，避免同一对角线完全同深度
  const float basis =
    (logicalCenter.x + logicalCenter.y) * tileHeight * 0.5f +
    (logicalCenter.x - logicalCenter.y) * tileWidth * 0.5f * 0.001f;
  return std::clamp(0.5f - basis / 16384.0f, 0.001f, 0.999f);
}

void drawPlacementPreview(Renderer2D& renderer,
                          const BuildingPlacement& placement,
                          const MapGrid& map,
                          const Vec2 origin,
                          const float tileWidth,
                          const float tileHeight) {
  // 每个格子都单独着色，这样即便 footprint 很大或不规则，
  // 也能很容易看出具体是哪一格发生了占用冲突。
  for (const auto coord : foundationTiles(placement)) {
    const bool buildable = map.isBuildable(coord);
    const float r = buildable ? 0.35f : 0.90f;
    const float g = buildable ? 0.85f : 0.25f;
    const float b = buildable ? 0.95f : 0.25f;
    const auto center = isoToScreen(coord.x, coord.y, origin, tileWidth, tileHeight);
    drawDiamondFill(renderer, center, tileWidth, tileHeight, r, g, b, 0.18f);
    drawDiamondOutline(renderer, center, tileWidth, tileHeight, r, g, b, 0.95f);
  }
}

void drawBuildingInstance(Renderer2D& renderer,
                          const BuildingAsset& asset,
                          const BuildingInstance& instance,
                          const Vec2 origin,
                          const float tileWidth,
                          const float tileHeight,
                          const std::uint32_t nowTicks,
                          const float depth01,
                          const float tintR,
                          const float tintG,
                          const float tintB,
                          const float tintA) {
  // 每一帧会先确定“基础锚点帧”：
  // - 建造中：使用 buildup 当前帧
  // - 完成态：使用第一层完成态帧
  // 真正绘制完成态时，再把 completeLayers 逐层叠上去。
  const UiTexture* texture = &asset.completeTexture;

  if ((instance.state == BuildingState::Constructing || instance.state == BuildingState::Packing) &&
      !asset.buildupTextures.empty()) {
    const auto frameIndex = buildingBuildupFrameIndex(instance, asset.buildupTextures.size(), nowTicks);
    texture = &asset.buildupTextures[frameIndex];
  }

  const auto foundationAnchor = buildingDrawCenter(instance.placement, origin, tileWidth, tileHeight);
  const bool buildup = instance.state == BuildingState::Constructing || instance.state == BuildingState::Packing;
  const auto offset = !buildup && !asset.completeLayers.empty() ? asset.completeLayers.front().offset : PixelOffset{};
  const auto spriteTopLeft = buildingSpriteTopLeft(foundationAnchor, texture->width, texture->height, offset);
  const Vec2 spriteTopRight{spriteTopLeft.x + texture->width, spriteTopLeft.y};
  const Vec2 spriteBottomRight{spriteTopLeft.x + texture->width, spriteTopLeft.y + texture->height};
  const Vec2 spriteBottomLeft{spriteTopLeft.x, spriteTopLeft.y + texture->height};

  // 阴影是刻意做得比较简化和风格化的。
  // 它并不追求物理正确，但在原型阶段能明显增强放置感和前后遮挡关系的可读性。
  // Flat=yes 的维修厂/地台类建筑已经包含贴地接触阴影，跳过额外投影。
  if (!asset.art.flat) {
    const float foundationScreenWidth = foundationContactSpan(instance.placement, tileWidth);
    // The ground shadow belongs to the footprint center, not the SHP draw origin.
    const auto ground = logicalCenterForPlacement(instance.placement);
    const Vec2 shadowCenter{origin.x + (ground.x - ground.y) * tileWidth * 0.5f,
                            origin.y + (ground.x + ground.y) * tileHeight * 0.5f};
    const Vec2 shadowTopLeft{shadowCenter.x - foundationScreenWidth * 0.48f, shadowCenter.y - tileHeight * 0.10f};
    const Vec2 shadowTopRight{shadowCenter.x + foundationScreenWidth * 0.32f, shadowCenter.y - tileHeight * 0.22f};
    const Vec2 shadowBottomRight{shadowCenter.x + foundationScreenWidth * 0.58f, shadowCenter.y + tileHeight * 0.12f};
    const Vec2 shadowBottomLeft{shadowCenter.x - foundationScreenWidth * 0.22f, shadowCenter.y + tileHeight * 0.20f};

  // 建筑本体现在已经是“按像素写深度”了，但阴影仍然是简化投影。
  // 如果让阴影也把统一深度写进 depth buffer，就可能在某些建筑像素上抢到
  // 比本体更靠前的深度，表现成建筑表面冒出奇怪的黑块。
  //
  // 这里让阴影参与深度测试，但不写深度：
  // - 已经画在前面的其它物体仍然可以挡住阴影
  // - 当前建筑本体一定能在后续把阴影压回去
    glDepthMask(GL_FALSE);
    drawTexturedQuad(renderer,
                     *texture,
                     shadowTopLeft,
                     shadowTopRight,
                     shadowBottomRight,
                     shadowBottomLeft,
                     0.0f,
                     0.0f,
                     0.0f,
                     0.34f * tintA,
                     std::min(0.999f, depth01 + 0.0005f));
    glDepthMask(GL_TRUE);
  }
  const auto logicalDepthWithFootprint = buildingLogicalDepth(instance.placement, tileWidth, tileHeight);

  auto drawCompleteLayer = [&](const UiTexture& layerTexture, const BuildingAsset::RenderLayer& layer) {
    const auto spriteTopLeft = buildingSpriteTopLeft(foundationAnchor, layerTexture.width, layerTexture.height, layer.offset);
    const Vec2 layerTopRight{spriteTopLeft.x + layerTexture.width, spriteTopLeft.y};
    const Vec2 layerBottomRight{spriteTopLeft.x + layerTexture.width, spriteTopLeft.y + layerTexture.height};
    const Vec2 layerBottomLeft{spriteTopLeft.x, spriteTopLeft.y + layerTexture.height};
    const auto layerVertices = makeQuadVertices(spriteTopLeft,
                                                layerTopRight,
                                                layerBottomRight,
                                                layerBottomLeft,
                                                depth01,
                                                tintR,
                                                tintG,
                                                tintB,
                                                tintA);
    const bool layerWritesDepth = !isUnderUnitLayer(layer.role);
    if (!layerWritesDepth) {
      glDepthMask(GL_FALSE);
    }
    renderer.draw(GL_TRIANGLES,
                  layerTexture,
                  layerVertices.data(),
                  layerVertices.size(),
                  logicalDepthWithFootprint);
    if (!layerWritesDepth) {
      glDepthMask(GL_TRUE);
    }
  };

  if (instance.state != BuildingState::Constructing && instance.state != BuildingState::Packing) {
    for (std::size_t layerIndex = 1; layerIndex < asset.completeLayers.size(); ++layerIndex) {
      const auto& layer = asset.completeLayers[layerIndex];
      if (isProductionLayer(layer.role) || !isUnderUnitLayer(layer.role)) {
        continue;
      }
      if (const auto* layerTexture = buildingLayerTexture(layer, instance, nowTicks)) {
        drawCompleteLayer(*layerTexture, layer);
      }
    }
  }

  const auto baseVertices = makeQuadVertices(spriteTopLeft,
                                             spriteTopRight,
                                             spriteBottomRight,
                                             spriteBottomLeft,
                                             depth01,
                                             tintR,
                                             tintG,
                                             tintB,
                                             tintA);
  const bool drawingCompleteBaseLayer =
    instance.state != BuildingState::Constructing && instance.state != BuildingState::Packing;
  const bool baseLayerWritesDepth =
    !drawingCompleteBaseLayer ||
    asset.completeLayers.empty() ||
    asset.completeLayers.front().role != BuildingAsset::LayerRole::UnderUnit;
  if (!baseLayerWritesDepth) {
    glDepthMask(GL_FALSE);
  }
  renderer.draw(GL_TRIANGLES,
                *texture,
                baseVertices.data(),
                baseVertices.size(),
                logicalDepthWithFootprint);
  if (!baseLayerWritesDepth) {
    glDepthMask(GL_TRUE);
  }

  // 预览态本质上是“完整建筑的半透明投影”，
  // 所以应该和 Complete 一样把后续附层一起画出来。
  // 只有建造中才只显示当前 build-up 帧，不叠完成态附层。
  if (instance.state == BuildingState::Constructing || instance.state == BuildingState::Packing) {
    return;
  }

  // 第 0 层已经作为基础层画过了，后面的层继续按顺序叠加。
  for (std::size_t layerIndex = 1; layerIndex < asset.completeLayers.size(); ++layerIndex) {
    const auto& layer = asset.completeLayers[layerIndex];
    if (isProductionLayer(layer.role)) {
      continue;
    }
    if (isUnderUnitLayer(layer.role)) {
      continue;
    }
    if (defersOverUnitLayersUntilAfterUnits(asset) && layer.role == BuildingAsset::LayerRole::OverUnit) {
      continue;
    }
    if (layer.textures.empty()) {
      continue;
    }

    const UiTexture* layerTexture = buildingLayerTexture(layer, instance, nowTicks);

    drawCompleteLayer(*layerTexture, layer);
  }
}

void drawInfiniteIsoGroundGrid(Renderer2D& renderer,
                               const Vec2 origin,
                               const float tileWidth,
                               const float tileHeight,
                               const int viewportWidth,
                               const int viewportHeight) {
  // “无限地图”的实现方式其实是：每帧只生成当前视口可能看到的格子，
  // 再在边缘额外补一圈安全边距。
  const int tileRadiusX = static_cast<int>(viewportWidth / tileWidth) + 8;
  const int tileRadiusY = static_cast<int>(viewportHeight / tileHeight) + 8;
  const int minTileX = -tileRadiusX;
  const int maxTileX = tileRadiusX;
  const int minTileY = -tileRadiusY;
  const int maxTileY = tileRadiusY;

  for (int y = minTileY; y <= maxTileY; ++y) {
    for (int x = minTileX; x <= maxTileX; ++x) {
      const auto center = isoToScreen(x, y, origin, tileWidth, tileHeight);
      const bool checker = ((x + y) % 2) == 0;
      const float r = checker ? 0.46f : 0.41f;
      const float g = checker ? 0.47f : 0.42f;
      const float b = checker ? 0.50f : 0.45f;
      drawDiamondFill(renderer, center, tileWidth, tileHeight, r, g, b, 1.0f);
    }
  }

  for (int y = minTileY; y <= maxTileY; ++y) {
    for (int x = minTileX; x <= maxTileX; ++x) {
      const auto center = isoToScreen(x, y, origin, tileWidth, tileHeight);
      const float halfW = tileWidth * 0.5f;
      const float halfH = tileHeight * 0.5f;
      drawLine(renderer, Vec2{center.x, center.y - halfH}, Vec2{center.x + halfW, center.y}, 0.78f, 0.80f, 0.84f, 0.28f);
      drawLine(renderer, Vec2{center.x + halfW, center.y}, Vec2{center.x, center.y + halfH}, 0.78f, 0.80f, 0.84f, 0.28f);
      drawLine(renderer, Vec2{center.x, center.y + halfH}, Vec2{center.x - halfW, center.y}, 0.78f, 0.80f, 0.84f, 0.28f);
      drawLine(renderer, Vec2{center.x - halfW, center.y}, Vec2{center.x, center.y - halfH}, 0.78f, 0.80f, 0.84f, 0.28f);
    }
  }
}
