#include "iso_world.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {
[[nodiscard]] bool defersOverUnitLayersUntilAfterUnits(const BuildingAsset& asset) {
  return asset.id == "GAWEAP";
}

[[nodiscard]] bool isUnderUnitLayer(const BuildingAsset::LayerRole role) {
  return role == BuildingAsset::LayerRole::UnderUnit ||
         role == BuildingAsset::LayerRole::ProductionUnderUnit;
}

[[nodiscard]] bool isProductionLayer(const BuildingAsset::LayerRole role) {
  return role == BuildingAsset::LayerRole::ProductionUnderUnit ||
         role == BuildingAsset::LayerRole::ProductionOverUnit;
}

// 用于表达“相对锚点格”的辅助偏移函数。
TileCoord offsetTile(const TileCoord anchor, const int dx, const int dy) {
  return TileCoord{anchor.x + dx, anchor.y + dy};
}

// 复现参考项目里使用的 footprint 形状。
// 虽然 art.ini 保存的是矩形宽高，但在等距视角下，
// 若干大建筑的可见/可放置区域实际上会按“菱形簇”来处理。
std::vector<TileCoord> foundationTilesFromAxes(const TileCoord anchor, const int fxNum, const int fyNum) {
  std::vector<TileCoord> coords;
  auto push = [&](const int dx, const int dy) {
    coords.push_back(offsetTile(anchor, dx, dy));
  };

  if (fxNum == 1 && fyNum == 1) {
    push(0, 0);
  } else if (fxNum == 2 && fyNum == 2) {
    push(0, 0);
    push(0, 1);
    push(1, 1);
    push(1, 0);
  } else if (fxNum == 2 && fyNum == 3) {
    push(0, 0);
    push(-1, 0);
    push(-1, 1);
    push(0, 1);
    push(1, 1);
    push(1, 0);
  } else if (fxNum == 3 && fyNum == 3) {
    push(0, 0);
    push(-1, 1);
    push(0, 1);
    push(1, 1);
    push(1, 0);
    push(1, -1);
    push(0, -1);
    push(-1, -1);
    push(-1, 0);
  } else if (fxNum == 3 && fyNum == 4) {
    push(0, 0);
    push(-1, 1);
    push(0, 1);
    push(1, 1);
    push(1, 0);
    push(1, -1);
    push(0, -1);
    push(-1, -1);
    push(-1, 0);
    push(2, -1);
    push(2, 0);
    push(2, 1);
  } else if (fxNum == 4 && fyNum == 4) {
    push(0, 0);
    push(-1, 1);
    push(0, 1);
    push(1, 1);
    push(1, 0);
    push(1, -1);
    push(0, -1);
    push(-1, -1);
    push(-1, 0);
    push(-1, 2);
    push(0, 2);
    push(1, 2);
    push(2, -1);
    push(2, 0);
    push(2, 1);
    push(2, 2);
  } else if (fxNum == 3 && fyNum == 5) {
    push(0, 0);
    push(-1, 1);
    push(0, 1);
    push(1, 1);
    push(1, 0);
    push(1, -1);
    push(0, -1);
    push(-1, -1);
    push(-1, 0);
    push(-2, 1);
    push(-2, 0);
    push(-2, -1);
    push(2, -1);
    push(2, 0);
    push(2, 1);
  } else {
    for (int dy = 0; dy < fyNum; ++dy) {
      for (int dx = 0; dx < fxNum; ++dx) {
        push(dx, dy);
      }
    }
  }

  return coords;
}

// 用占地格中心来表达“这个对象在地图逻辑平面里的位置”。
// 对建筑来说，这个中心点来自所有占地格中心的平均值；
// 对 1x1 载具来说，直接就是它当前所在的逻辑坐标。
Vec2 logicalCenterForPlacement(const BuildingPlacement& placement) {
  const auto coords = foundationTilesFromAxes(placement.topLeft, placement.height, placement.width);
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

Vec2 foundationTopCenter(const BuildingPlacement& placement,
                         const Vec2 origin,
                         const float tileWidth,
                         const float tileHeight) {
  return isoToScreen(placement.topLeft.x, placement.topLeft.y, origin, tileWidth, tileHeight);
}

// 原版/参考项目的对齐规则并不是单纯依赖 sprite 包围盒，
// 而是先按 footprint 类别分组，再给一套经验偏移。
// 这些偏移就是 SHP 图像空间和地图格空间之间的实际桥梁。
Vec2 spriteAnchorInFrameImpl(const DecodedFrame& frame, const FoundationSize& foundation) {
  float anchorX = frame.width * 0.5f;
  float anchorY = frame.height * 0.5f;

  // 参考项目按 footprint 形状做对齐，而不是按图片实际边界做自动推导。
  const int fxNum = foundation.height;
  const int fyNum = foundation.width;

  if (fxNum == 2 && fyNum == 2) {
    anchorY += 14.0f;
  } else if (fxNum == 1 && fyNum == 1) {
    anchorY += 14.0f;
  } else if (fxNum == 2 && fyNum == 3) {
    anchorX += 30.0f;
    anchorY += 29.0f;
  } else if (fxNum == 3 && fyNum == 3) {
    anchorY += 44.0f;
  } else if (fxNum == 4 && fyNum == 4) {
    anchorY += 44.0f;
  } else if (fxNum == 3 && fyNum == 4) {
    anchorY += 44.0f;
  } else if (fxNum == 3 && fyNum == 5) {
    anchorX += 30.0f;
    anchorY += 59.0f;
  }

  return Vec2{anchorX, anchorY};
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

Vec2 spriteAnchorInFrame(const DecodedFrame& frame, const FoundationSize& foundation) {
  return spriteAnchorInFrameImpl(frame, foundation);
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

std::vector<TileCoord> foundationTiles(const BuildingPlacement& placement) {
  return foundationTilesFromAxes(placement.topLeft, placement.height, placement.width);
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
  const DecodedFrame* frame = &asset.completeFrame;

  if ((instance.state == BuildingState::Constructing || instance.state == BuildingState::Packing) &&
      !asset.buildupTextures.empty()) {
    constexpr std::uint32_t kBuildupFrameDurationMs = 50;
    const std::size_t elapsedFrame = std::min<std::size_t>(
      (nowTicks - instance.stateStartTicks) / kBuildupFrameDurationMs,
      asset.buildupTextures.size() - 1);
    const std::size_t frameIndex = instance.state == BuildingState::Packing
                                     ? asset.buildupTextures.size() - 1 - elapsedFrame
                                     : elapsedFrame;
    texture = &asset.buildupTextures[frameIndex];
    frame = &asset.buildupFrames[frameIndex];
  }

  const auto foundationAnchor = foundationTopCenter(instance.placement, origin, tileWidth, tileHeight);
  const auto anchor = spriteAnchorInFrame(*frame, asset.foundation);
  const Vec2 spriteTopLeft{foundationAnchor.x - anchor.x, foundationAnchor.y - anchor.y};
  const Vec2 spriteTopRight{spriteTopLeft.x + texture->width, spriteTopLeft.y};
  const Vec2 spriteBottomRight{spriteTopLeft.x + texture->width, spriteTopLeft.y + texture->height};
  const Vec2 spriteBottomLeft{spriteTopLeft.x, spriteTopLeft.y + texture->height};

  // 阴影是刻意做得比较简化和风格化的。
  // 它并不追求物理正确，但在原型阶段能明显增强放置感和前后遮挡关系的可读性。
  // Flat=yes 的维修厂/地台类建筑已经包含贴地接触阴影，跳过额外投影。
  if (!asset.art.flat) {
    const float foundationScreenWidth = foundationContactSpan(instance.placement, tileWidth);
    const Vec2 shadowTopLeft{foundationAnchor.x - foundationScreenWidth * 0.48f, foundationAnchor.y - tileHeight * 0.10f};
    const Vec2 shadowTopRight{foundationAnchor.x + foundationScreenWidth * 0.32f, foundationAnchor.y - tileHeight * 0.22f};
    const Vec2 shadowBottomRight{foundationAnchor.x + foundationScreenWidth * 0.58f, foundationAnchor.y + tileHeight * 0.12f};
    const Vec2 shadowBottomLeft{foundationAnchor.x - foundationScreenWidth * 0.22f, foundationAnchor.y + tileHeight * 0.20f};

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
  const LogicalDepthParams logicalDepth{
    static_cast<float>(instance.placement.topLeft.x),
    static_cast<float>(instance.placement.topLeft.y),
    tileWidth,
    tileHeight
  };
  const auto footprintCoords = foundationTiles(instance.placement);
  LogicalDepthParams logicalDepthWithFootprint = logicalDepth;
  logicalDepthWithFootprint.footprintCount =
    std::min<int>(static_cast<int>(footprintCoords.size()), LogicalDepthParams::kMaxFootprintTiles);
  for (int i = 0; i < logicalDepthWithFootprint.footprintCount; ++i) {
    logicalDepthWithFootprint.footprintOffsets[static_cast<std::size_t>(i) * 2 + 0] =
      static_cast<float>(footprintCoords[static_cast<std::size_t>(i)].x - instance.placement.topLeft.x);
    logicalDepthWithFootprint.footprintOffsets[static_cast<std::size_t>(i) * 2 + 1] =
      static_cast<float>(footprintCoords[static_cast<std::size_t>(i)].y - instance.placement.topLeft.y);
  }

  auto currentLayerTexture = [&](const BuildingAsset::RenderLayer& layer) -> const UiTexture* {
    if (layer.textures.empty()) {
      return nullptr;
    }
    const UiTexture* layerTexture = &layer.textures.front();
    if (layer.textures.size() > 1 && layer.frameDurationMs > 0) {
      const auto frameDurationMs = std::max<std::uint32_t>(50, layer.frameDurationMs);
      const std::size_t frameIndex = static_cast<std::size_t>(
        ((nowTicks - instance.stateStartTicks) / frameDurationMs) % layer.textures.size());
      layerTexture = &layer.textures[frameIndex];
    }
    return layerTexture;
  };

  auto drawCompleteLayer = [&](const UiTexture& layerTexture, const BuildingAsset::LayerRole role) {
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
    const bool layerWritesDepth = !isUnderUnitLayer(role);
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
      if (const auto* layerTexture = currentLayerTexture(layer)) {
        drawCompleteLayer(*layerTexture, layer.role);
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

    const UiTexture* layerTexture = &layer.textures.front();
    if (layer.textures.size() > 1 && layer.frameDurationMs > 0) {
      const auto frameDurationMs = std::max<std::uint32_t>(50, layer.frameDurationMs);
      const std::size_t frameIndex = static_cast<std::size_t>(
        ((nowTicks - instance.stateStartTicks) / frameDurationMs) % layer.textures.size());
      layerTexture = &layer.textures[frameIndex];
    }

    const Vec2 layerTopRight{spriteTopLeft.x + layerTexture->width, spriteTopLeft.y};
    const Vec2 layerBottomRight{spriteTopLeft.x + layerTexture->width, spriteTopLeft.y + layerTexture->height};
    const Vec2 layerBottomLeft{spriteTopLeft.x, spriteTopLeft.y + layerTexture->height};
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
                  *layerTexture,
                  layerVertices.data(),
                  layerVertices.size(),
                  logicalDepthWithFootprint);
    if (!layerWritesDepth) {
      glDepthMask(GL_TRUE);
    }
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
