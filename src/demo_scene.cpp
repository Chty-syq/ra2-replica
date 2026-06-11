#include "demo_scene.h"
#include "rhino_unit.h"

#include <SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>

namespace {
struct ScreenRect {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;
};

struct BarColor {
  float r = 1.0f;
  float g = 1.0f;
  float b = 1.0f;
  float a = 1.0f;
};

struct BuildingHealthColors {
  BarColor black;
  BarColor deep;
  BarColor inner;
};

// 经验换算：把 art.ini 的逻辑 Height 转为选择骨架/血条使用的屏幕像素高度。
constexpr float kArtHeightToBonePixelsScale = 0.58f;

[[nodiscard]] std::uint64_t tileKey(const TileCoord coord) {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(coord.x)) << 32) |
         static_cast<std::uint32_t>(coord.y);
}

[[nodiscard]] Vec2 topFirstFoundationCenter(const BuildingPlacement& placement,
                                            const Vec2 origin,
                                            const float tileWidth,
                                            const float tileHeight) {
  Vec2 best = isoToScreen(placement.topLeft.x, placement.topLeft.y, origin, tileWidth, tileHeight);
  float bestY = best.y;
  for (const auto tile : foundationTiles(placement)) {
    const auto center = isoToScreen(tile.x, tile.y, origin, tileWidth, tileHeight);
    if (center.y < bestY) {
      best = center;
      bestY = center.y;
    }
  }
  return best;
}

[[nodiscard]] Vec2 leftFirstFoundationCenter(const BuildingPlacement& placement,
                                             const Vec2 origin,
                                             const float tileWidth,
                                             const float tileHeight) {
  Vec2 best = isoToScreen(placement.topLeft.x, placement.topLeft.y, origin, tileWidth, tileHeight);
  float bestX = best.x;
  for (const auto tile : foundationTiles(placement)) {
    const auto center = isoToScreen(tile.x, tile.y, origin, tileWidth, tileHeight);
    if (center.x < bestX) {
      best = center;
      bestX = center.x;
    }
  }
  return best;
}

[[nodiscard]] Vec2 rightFirstFoundationCenter(const BuildingPlacement& placement,
                                              const Vec2 origin,
                                              const float tileWidth,
                                              const float tileHeight) {
  Vec2 best = isoToScreen(placement.topLeft.x, placement.topLeft.y, origin, tileWidth, tileHeight);
  float bestX = best.x;
  for (const auto tile : foundationTiles(placement)) {
    const auto center = isoToScreen(tile.x, tile.y, origin, tileWidth, tileHeight);
    if (center.x > bestX) {
      best = center;
      bestX = center.x;
    }
  }
  return best;
}

[[nodiscard]] float referenceBoneAxisLength(const int axisTiles) {
  if (axisTiles <= 1) {
    return 8.0f;
  }
  if (axisTiles == 2) {
    return 16.0f;
  }
  return static_cast<float>(axisTiles - 1) * 10.0f;
}

[[nodiscard]] float buildingBoneHeightPixels(const BuildingAsset& asset, const float tileHeight) {
  if (!asset.art.height.has_value()) {
    return 70.0f;
  }

  // JavaRedAlert2 的 BuildingBone 使用各建筑手写 height；这里用 art.ini 的 Height
  // 转成屏幕像素，保持“白线框落在建筑顶部附近”的同一语义。
  return std::clamp(*asset.art.height * tileHeight * kArtHeightToBonePixelsScale, 18.0f, 190.0f);
}

[[nodiscard]] Vec2 buildingHealthBarOrigin(const BuildingAsset& asset,
                                           const BuildingInstance& building,
                                           const Vec2 mapOrigin,
                                           const float tileWidth,
                                           const float tileHeight,
                                           const int barWidth) {
  const auto topFirst = topFirstFoundationCenter(building.placement, mapOrigin, tileWidth, tileHeight);
  const float boneHeight = buildingBoneHeightPixels(asset, tileHeight);

  // 原版/参考项目的建筑血条贴在“顶部骨架线”上，而不是贴 sprite 的像素包围盒。
  // 因此这里必须和 BuildingBone 一样使用 TopFirst - 虚拟高度这套坐标。
  return Vec2{
    topFirst.x - static_cast<float>(barWidth) + 7.0f,
    topFirst.y - boneHeight
  };
}

[[nodiscard]] std::vector<TileCoord> sampledRhinoTiles(const RhinoUnitState& unit) {
  const int minX = static_cast<int>(std::floor(unit.tilePosition.x));
  const int maxX = static_cast<int>(std::ceil(unit.tilePosition.x));
  const int minY = static_cast<int>(std::floor(unit.tilePosition.y));
  const int maxY = static_cast<int>(std::ceil(unit.tilePosition.y));

  std::vector<TileCoord> tiles;
  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      tiles.push_back(TileCoord{x, y});
    }
  }
  if (tiles.empty()) {
    tiles.push_back(unit.occupiedCell);
  }
  return tiles;
}

[[nodiscard]] std::unordered_set<std::uint64_t> occluderCandidateTileSet(const RhinoUnitState& unit) {
  std::unordered_set<std::uint64_t> candidates;
  for (const auto tile : sampledRhinoTiles(unit)) {
    candidates.insert(tileKey(TileCoord{tile.x + 1, tile.y}));
    candidates.insert(tileKey(TileCoord{tile.x, tile.y + 1}));
    candidates.insert(tileKey(TileCoord{tile.x + 1, tile.y + 1}));
  }
  return candidates;
}

[[nodiscard]] bool buildingMayOccludeRhino(const BuildingInstance& building,
                                           const std::unordered_set<std::uint64_t>& candidateTiles) {
  for (const auto tile : foundationTiles(building.placement)) {
    if (candidateTiles.find(tileKey(tile)) != candidateTiles.end()) {
      return true;
    }
  }
  return false;
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

[[nodiscard]] std::array<RenderVertex, 6> makeQuadVerticesWithUv(const Vec2 topLeft,
                                                                 const Vec2 topRight,
                                                                 const Vec2 bottomRight,
                                                                 const Vec2 bottomLeft,
                                                                 const float u0,
                                                                 const float v0,
                                                                 const float u1,
                                                                 const float v1,
                                                                 const float depth01,
                                                                 const float r,
                                                                 const float g,
                                                                 const float b,
                                                                 const float a) {
  return {{
    {topLeft.x, topLeft.y, depth01, u0, v0, r, g, b, a},
    {topRight.x, topRight.y, depth01, u1, v0, r, g, b, a},
    {bottomRight.x, bottomRight.y, depth01, u1, v1, r, g, b, a},
    {topLeft.x, topLeft.y, depth01, u0, v0, r, g, b, a},
    {bottomRight.x, bottomRight.y, depth01, u1, v1, r, g, b, a},
    {bottomLeft.x, bottomLeft.y, depth01, u0, v1, r, g, b, a}
  }};
}

[[nodiscard]] bool isWarFactory(const std::string& buildingId) {
  return buildingId == "GAWEAP" || buildingId == "NAWEAP";
}

[[nodiscard]] bool isProductionLayer(const BuildingAsset::LayerRole role) {
  return role == BuildingAsset::LayerRole::ProductionUnderUnit ||
         role == BuildingAsset::LayerRole::ProductionOverUnit;
}

[[nodiscard]] bool isUnderUnitLayer(const BuildingAsset::LayerRole role) {
  return role == BuildingAsset::LayerRole::UnderUnit ||
         role == BuildingAsset::LayerRole::ProductionUnderUnit;
}

[[nodiscard]] bool rhinoInWarFactoryExitLane(const BuildingInstance& building,
                                             const RhinoUnitState& rhinoUnit) {
  const float localX = rhinoUnit.tilePosition.x - static_cast<float>(building.placement.topLeft.x);
  const float localY = rhinoUnit.tilePosition.y - static_cast<float>(building.placement.topLeft.y);
  return localX >= 0.25f && localX <= 4.25f && localY >= -1.0f && localY <= 1.5f;
}

void drawSolidRect(Renderer2D& renderer,
                   const float x,
                   const float y,
                   const float width,
                   const float height,
                   const BarColor color) {
  if (width <= 0.0f || height <= 0.0f) {
    return;
  }

  const RenderVertex vertices[] = {
    {x, y, 0.001f, 0.0f, 0.0f, color.r, color.g, color.b, color.a},
    {x + width, y, 0.001f, 1.0f, 0.0f, color.r, color.g, color.b, color.a},
    {x + width, y + height, 0.001f, 1.0f, 1.0f, color.r, color.g, color.b, color.a},
    {x, y, 0.001f, 0.0f, 0.0f, color.r, color.g, color.b, color.a},
    {x + width, y + height, 0.001f, 1.0f, 1.0f, color.r, color.g, color.b, color.a},
    {x, y + height, 0.001f, 0.0f, 1.0f, color.r, color.g, color.b, color.a}
  };
  renderer.draw(GL_TRIANGLES, renderer.whiteTexture(), vertices, sizeof(vertices) / sizeof(vertices[0]));
}

void drawPixelLine(Renderer2D& renderer,
                   const Vec2 origin,
                   const int x0,
                   const int y0,
                   const int x1,
                   const int y1,
                   const float scale,
                   const BarColor color) {
  const int minX = std::min(x0, x1);
  const int maxX = std::max(x0, x1);
  const int minY = std::min(y0, y1);
  const int maxY = std::max(y0, y1);
  drawSolidRect(renderer,
                origin.x + static_cast<float>(minX) * scale,
                origin.y + static_cast<float>(minY) * scale,
                static_cast<float>(maxX - minX + 1) * scale,
                static_cast<float>(maxY - minY + 1) * scale,
                color);
}

void drawSelectionLine(Renderer2D& renderer,
                       const Vec2 from,
                       const Vec2 to,
                       const BarColor color = BarColor{1.0f, 1.0f, 1.0f, 1.0f}) {
  const RenderVertex vertices[] = {
    {from.x, from.y, 0.001f, 0.0f, 0.0f, color.r, color.g, color.b, color.a},
    {to.x, to.y, 0.001f, 1.0f, 1.0f, color.r, color.g, color.b, color.a}
  };
  renderer.draw(GL_LINES, renderer.whiteTexture(), vertices, sizeof(vertices) / sizeof(vertices[0]));
}

void drawSelectedBuildingBone(Renderer2D& renderer,
                              const BuildingAsset& asset,
                              const BuildingInstance& building,
                              const Vec2 mapOrigin,
                              const float tileWidth,
                              const float tileHeight) {
  const auto leftFirst = leftFirstFoundationCenter(building.placement, mapOrigin, tileWidth, tileHeight);
  const auto topFirst = topFirstFoundationCenter(building.placement, mapOrigin, tileWidth, tileHeight);
  const auto rightFirst = rightFirstFoundationCenter(building.placement, mapOrigin, tileWidth, tileHeight);

  const float boneHeight = buildingBoneHeightPixels(asset, tileHeight);
  const float verticalHeight = std::clamp(boneHeight * 0.25f, 5.0f, 48.0f);
  const int fxNum = std::max(1, asset.foundation.height);
  const int fyNum = std::max(1, asset.foundation.width);
  const float fxLength = referenceBoneAxisLength(fxNum);
  const float fyLength = referenceBoneAxisLength(fyNum);

  const Vec2 leftPoint{
    leftFirst.x - tileWidth * 0.5f - 1.0f,
    leftFirst.y - boneHeight + tileHeight * 0.47f
  };
  const Vec2 topPoint{
    topFirst.x,
    topFirst.y - boneHeight
  };
  const Vec2 rightPoint{
    rightFirst.x + tileWidth * 0.5f - 3.0f,
    rightFirst.y - boneHeight + tileHeight * 0.40f
  };

  constexpr BarColor kWhite{1.0f, 1.0f, 1.0f, 0.92f};
  drawSelectionLine(renderer, leftPoint, Vec2{leftPoint.x, leftPoint.y + verticalHeight}, kWhite);
  drawSelectionLine(renderer, leftPoint, Vec2{leftPoint.x + fyLength, leftPoint.y + fyLength * 0.5f}, kWhite);
  drawSelectionLine(renderer, leftPoint, Vec2{leftPoint.x + fxLength, leftPoint.y - fxLength * 0.5f}, kWhite);

  drawSelectionLine(renderer, topPoint, Vec2{topPoint.x, topPoint.y + verticalHeight}, kWhite);
  drawSelectionLine(renderer, topPoint, Vec2{topPoint.x - fxLength, topPoint.y + fxLength * 0.5f}, kWhite);
  drawSelectionLine(renderer, topPoint, Vec2{topPoint.x + fyLength, topPoint.y + fyLength * 0.5f}, kWhite);

  drawSelectionLine(renderer, rightPoint, Vec2{rightPoint.x, rightPoint.y + verticalHeight}, kWhite);
  drawSelectionLine(renderer, rightPoint, Vec2{rightPoint.x - fxLength, rightPoint.y + fxLength * 0.5f}, kWhite);
  drawSelectionLine(renderer, rightPoint, Vec2{rightPoint.x - fyLength, rightPoint.y - fyLength * 0.5f}, kWhite);
}

void drawFilledBuildingHealthPip(Renderer2D& renderer,
                                 const Vec2 origin,
                                 const int x,
                                 const int y,
                                 const float scale,
                                 const BuildingHealthColors colors,
                                 const bool firstPip,
                                 const bool lastPip) {
  // 参考 JavaRedAlert2 的 BuildingBloodBar：每个建筑血块都是 6x7 的斜向像素块。
  drawPixelLine(renderer, origin, x, y, x + 1, y, scale, colors.deep);

  drawPixelLine(renderer, origin, x - 2, y + 1, x - 1, y + 1, scale, colors.deep);
  drawPixelLine(renderer, origin, x, y + 1, x + 1, y + 1, scale, colors.inner);
  drawPixelLine(renderer, origin, x + 2, y + 1, x + 3, y + 1, scale, colors.deep);

  drawPixelLine(renderer, origin, x - 4, y + 2, x - 3, y + 2, scale, colors.deep);
  drawPixelLine(renderer, origin, x - 2, y + 2, x + 3, y + 2, scale, colors.inner);
  drawPixelLine(renderer, origin, x + 4, y + 2, x + 5, y + 2, scale, colors.deep);

  drawPixelLine(renderer, origin, x - 2, y + 3, x - 1, y + 3, scale, colors.deep);
  drawPixelLine(renderer, origin, x, y + 3, x + 1, y + 3, scale, colors.inner);
  drawPixelLine(renderer, origin, x + 2, y + 3, x + 3, y + 3, scale, colors.deep);

  drawPixelLine(renderer, origin, x, y + 4, x + 1, y + 4, scale, colors.deep);
  drawPixelLine(renderer, origin, x - 4, y + 3, x - 4, y + 4, scale, colors.black);
  drawPixelLine(renderer, origin, x - 4, y + 4, x - 3, y + 4, scale, colors.black);
  drawPixelLine(renderer, origin, x - 3, y + 3, x - 3, y + 3, scale, colors.inner);
  drawPixelLine(renderer, origin, x - 2, y + 4, x - 1, y + 4, scale, colors.inner);
  drawPixelLine(renderer, origin, x, y + 5, x + 3, y + 5, scale, colors.black);
  drawPixelLine(renderer, origin, x, y + 6, x + 1, y + 6, scale, colors.black);
  drawPixelLine(renderer, origin, x + 2, y + 4, x + 3, y + 4, scale, colors.deep);

  if (firstPip) {
    drawPixelLine(renderer, origin, x + 5, y + 2, x + 5, y + 4, scale, colors.black);
    drawPixelLine(renderer, origin, x + 4, y + 4, x + 5, y + 4, scale, colors.black);
    drawPixelLine(renderer, origin, x + 4, y + 3, x + 4, y + 3, scale, colors.deep);
  }
  if (lastPip) {
    drawPixelLine(renderer, origin, x - 4, y + 3, x - 4, y + 4, scale, colors.black);
    drawPixelLine(renderer, origin, x - 4, y + 4, x - 3, y + 4, scale, colors.black);
    drawPixelLine(renderer, origin, x - 2, y + 5, x - 1, y + 5, scale, colors.black);
  }
}

void drawEmptyBuildingHealthPip(Renderer2D& renderer,
                                const Vec2 origin,
                                const int x,
                                const int y,
                                const float scale,
                                const bool lastPip) {
  constexpr BarColor kGray{180.0f / 255.0f, 180.0f / 255.0f, 180.0f / 255.0f, 1.0f};
  constexpr BarColor kBlack{0.0f, 0.0f, 0.0f, 1.0f};
  constexpr BarColor kWhite{1.0f, 1.0f, 1.0f, 1.0f};

  drawPixelLine(renderer, origin, x, y, x + 1, y, scale, kGray);
  drawPixelLine(renderer, origin, x - 2, y + 1, x - 1, y + 1, scale, kGray);
  drawPixelLine(renderer, origin, x + 2, y + 1, x + 3, y + 1, scale, kGray);
  drawPixelLine(renderer, origin, x - 4, y + 2, x - 3, y + 2, scale, kGray);
  drawPixelLine(renderer, origin, x + 4, y + 2, x + 5, y + 2, scale, kGray);
  drawPixelLine(renderer, origin, x - 2, y + 3, x - 1, y + 3, scale, kGray);
  drawPixelLine(renderer, origin, x + 2, y + 3, x + 3, y + 3, scale, kGray);
  drawPixelLine(renderer, origin, x, y + 4, x + 1, y + 4, scale, kGray);
  drawPixelLine(renderer, origin, x - 4, y + 3, x - 4, y + 4, scale, kBlack);
  drawPixelLine(renderer, origin, x - 4, y + 4, x - 3, y + 4, scale, kBlack);
  drawPixelLine(renderer, origin, x, y + 5, x + 3, y + 5, scale, kBlack);
  drawPixelLine(renderer, origin, x, y + 6, x + 1, y + 6, scale, kBlack);

  if (lastPip) {
    drawPixelLine(renderer, origin, x - 2, y + 5, x + 3, y + 5, scale, kBlack);
    drawPixelLine(renderer, origin, x, y + 6, x + 1, y + 6, scale, kBlack);
    drawPixelLine(renderer, origin, x + 1, y + 5, x + 1, y + 5, scale, kWhite);
  }
}

void drawBuildingForegroundOverlay(Renderer2D& renderer,
                                   const BuildingAsset& asset,
                                   const BuildingInstance& instance,
                                   const RhinoUnitState& rhinoUnit,
                                   const Vec2 origin,
                                   const float tileWidth,
                                   const float tileHeight,
                                   const std::uint32_t nowTicks,
                                   const float depth01) {
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

  const auto foundationAnchor = isoToScreen(instance.placement.topLeft.x,
                                            instance.placement.topLeft.y,
                                            origin,
                                            tileWidth,
                                            tileHeight);
  const auto anchor = spriteAnchorInFrame(*frame, asset.foundation);
  const Vec2 spriteTopLeft{foundationAnchor.x - anchor.x, foundationAnchor.y - anchor.y};
  const Vec2 spriteTopRight{spriteTopLeft.x + texture->width, spriteTopLeft.y};

  const bool warFactoryExitLane =
    instance.state == BuildingState::Complete && isWarFactory(asset.id) &&
    rhinoInWarFactoryExitLane(instance, rhinoUnit);
  const bool baseLayerUnderUnit =
    !asset.completeLayers.empty() &&
    asset.completeLayers.front().role == BuildingAsset::LayerRole::UnderUnit;
  if (!baseLayerUnderUnit && !warFactoryExitLane) {
    float baseForegroundHeight = static_cast<float>(texture->height);
    if (instance.state == BuildingState::Complete && isWarFactory(asset.id)) {
      baseForegroundHeight = std::max(0.0f, baseForegroundHeight - tileHeight * 1.25f);
    }

    const float baseForegroundV1 =
      texture->height > 0 ? std::clamp(baseForegroundHeight / static_cast<float>(texture->height), 0.0f, 1.0f)
                          : 0.0f;
    const Vec2 baseForegroundBottomRight{spriteTopLeft.x + texture->width, spriteTopLeft.y + baseForegroundHeight};
    const Vec2 baseForegroundBottomLeft{spriteTopLeft.x, spriteTopLeft.y + baseForegroundHeight};
    const auto baseVertices = makeQuadVerticesWithUv(spriteTopLeft,
                                                     spriteTopRight,
                                                     baseForegroundBottomRight,
                                                     baseForegroundBottomLeft,
                                                     0.0f,
                                                     0.0f,
                                                     1.0f,
                                                     baseForegroundV1,
                                                     depth01,
                                                     1.0f,
                                                     1.0f,
                                                     1.0f,
                                                     1.0f);
    renderer.draw(GL_TRIANGLES, *texture, baseVertices.data(), baseVertices.size());
  }

  if (instance.state != BuildingState::Complete) {
    return;
  }

  for (std::size_t layerIndex = 1; layerIndex < asset.completeLayers.size(); ++layerIndex) {
    const auto& layer = asset.completeLayers[layerIndex];
    if (isUnderUnitLayer(layer.role) || isProductionLayer(layer.role) || layer.textures.empty()) {
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
                                                1.0f,
                                                1.0f,
                                                1.0f,
                                                1.0f);
    renderer.draw(GL_TRIANGLES, *layerTexture, layerVertices.data(), layerVertices.size());
  }
}

[[nodiscard]] const UiTexture* currentLayerTexture(const BuildingAsset::RenderLayer& layer,
                                                   const BuildingInstance& instance,
                                                   const std::uint32_t nowTicks) {
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
}

void drawWarFactoryProductionLayerSubset(Renderer2D& renderer,
                                         const BuildingAsset& asset,
                                         const BuildingInstance& building,
                                         const Vec2 mapOrigin,
                                         const float tileWidth,
                                         const float tileHeight,
                                         const std::uint32_t nowTicks,
                                         const float depth01,
                                         const bool underUnitPass) {
  const auto foundationAnchor = isoToScreen(building.placement.topLeft.x,
                                            building.placement.topLeft.y,
                                            mapOrigin,
                                            tileWidth,
                                            tileHeight);
  const auto anchor = spriteAnchorInFrame(asset.completeFrame, asset.foundation);
  const Vec2 spriteTopLeft{foundationAnchor.x - anchor.x, foundationAnchor.y - anchor.y};

  for (const auto& layer : asset.completeLayers) {
    const bool drawLayer =
      underUnitPass
        ? (layer.role == BuildingAsset::LayerRole::ProductionUnderUnit ||
           layer.role == BuildingAsset::LayerRole::UnderUnit)
        : layer.role == BuildingAsset::LayerRole::ProductionOverUnit;
    if (!drawLayer) {
      continue;
    }

    const UiTexture* layerTexture = currentLayerTexture(layer, building, nowTicks);
    if (layerTexture == nullptr) {
      continue;
    }

    const Vec2 layerTopRight{spriteTopLeft.x + layerTexture->width, spriteTopLeft.y};
    const Vec2 layerBottomRight{spriteTopLeft.x + layerTexture->width, spriteTopLeft.y + layerTexture->height};
    const Vec2 layerBottomLeft{spriteTopLeft.x, spriteTopLeft.y + layerTexture->height};
    const auto layerVertices = makeQuadVertices(spriteTopLeft,
                                                layerTopRight,
                                                layerBottomRight,
                                                layerBottomLeft,
                                                depth01,
                                                1.0f,
                                                1.0f,
                                                1.0f,
                                                1.0f);
    if (underUnitPass) {
      glDepthMask(GL_FALSE);
    }
    renderer.draw(GL_TRIANGLES, *layerTexture, layerVertices.data(), layerVertices.size());
    if (underUnitPass) {
      glDepthMask(GL_TRUE);
    }
  }
}

[[nodiscard]] ScreenRect rhinoRedrawClipRect(const RhinoUnitState& unit,
                                             const Vec2 origin,
                                             const float tileWidth,
                                             const float tileHeight,
                                             const float footprintK) {
  const auto anchor = rhinoGroundAnchor(unit, origin, tileWidth, tileHeight);
  const float width = tileWidth * std::max(1.8f, footprintK * 2.3f);
  const float height = tileHeight * std::max(4.2f, footprintK * 5.2f);
  return ScreenRect{
    anchor.x - width * 0.5f,
    anchor.y - height * 0.90f,
    anchor.x + width * 0.5f,
    anchor.y + height * 0.10f
  };
}
}  // namespace

void updateSidebarStateForDemo(SidebarState& sidebarState,
                               const SidebarAssets& sidebarAssets,
                               const int viewportHeight,
                               const std::uint32_t nowTicks) {
  sidebarState.visibleRowsHint = computeSidebarVisibleRows(sidebarAssets, viewportHeight);
  sidebarState.tabVisible = {true, true, true, true};
  sidebarState.tabReady = {false, false, false, false};

  if (sidebarState.selectedTab != SidebarTab::Base &&
      !sidebarState.tabVisible[static_cast<std::size_t>(sidebarState.selectedTab)]) {
    sidebarState.selectedTab = SidebarTab::Base;
  }

  updateSidebarState(sidebarState, nowTicks);
}

void moveFirstBuilding(std::vector<BuildingInstance>& buildings,
                       MapGrid& map,
                       const TileCoord delta) {
  if ((delta.x == 0 && delta.y == 0) || buildings.empty()) {
    return;
  }

  auto& building = buildings.front();
  map.setOccupied(foundationTiles(building.placement), false);

  auto candidate = building.placement;
  candidate.topLeft.x += delta.x;
  candidate.topLeft.y += delta.y;
  if (map.canPlace(foundationTiles(candidate))) {
    building.placement = candidate;
  }

  map.setOccupied(foundationTiles(building.placement), true);
}

void tryPlaceSelectedBuilding(const float mouseX,
                              const float mouseY,
                              const std::uint32_t nowTicks,
                              const float viewportWidth,
                              const float sidebarWidth,
                              const Vec2 mapOrigin,
                              const float tileWidth,
                              const float tileHeight,
                              MapGrid& map,
                              std::vector<BuildingInstance>& buildings,
                              const BuildingAsset*& selectedBuildAsset,
                              const int selectedBuildMaxHp,
                              BuildingPlacement& previewPlacement) {
  if (selectedBuildAsset == nullptr || mouseX >= viewportWidth - sidebarWidth) {
    return;
  }

  const auto anchor = screenToIso(mouseX, mouseY, mapOrigin, tileWidth, tileHeight);
  auto candidate = placementFromAnchor(anchor, selectedBuildAsset->foundation);
  if (!map.canPlace(foundationTiles(candidate))) {
    return;
  }

  map.setOccupied(foundationTiles(candidate), true);
  const auto state = selectedBuildAsset->buildupTextures.empty()
                       ? BuildingState::Complete
                       : BuildingState::Constructing;
  buildings.push_back(BuildingInstance{
    selectedBuildAsset->id,
    candidate,
    state,
    nowTicks,
    selectedBuildMaxHp,
    selectedBuildMaxHp
  });
  selectedBuildAsset = nullptr;
  previewPlacement = candidate;
}

void updatePreviewPlacementFromMouse(const float mouseX,
                                     const float mouseY,
                                     const float viewportWidth,
                                     const float sidebarWidth,
                                     const Vec2 mapOrigin,
                                     const float tileWidth,
                                     const float tileHeight,
                                     const BuildingAsset* selectedBuildAsset,
                                     BuildingPlacement& previewPlacement) {
  if (selectedBuildAsset == nullptr || mouseX >= viewportWidth - sidebarWidth) {
    return;
  }

  previewPlacement = placementFromAnchor(screenToIso(mouseX, mouseY, mapOrigin, tileWidth, tileHeight),
                                         selectedBuildAsset->foundation);
}

void buildRenderQueue(const BuildingAssetMap& buildingAssets,
                      const std::vector<BuildingInstance>& buildings,
                      const BuildingAsset* selectedBuildAsset,
                      const BuildingPlacement& previewPlacement,
                      const std::uint32_t nowTicks,
                      const MapGrid& map,
                      Renderer2D& renderer,
                      const Vec2 mapOrigin,
                      const float tileWidth,
                      const float tileHeight,
                      std::vector<BuildingRenderCommand>& renderQueue) {
  renderQueue.clear();
  renderQueue.reserve(buildings.size() + (selectedBuildAsset != nullptr ? 1 : 0));

  for (const auto& building : buildings) {
    const auto& asset = assetForInstance(buildingAssets, building);
    renderQueue.push_back(makeBuildingRenderCommand(asset,
                                                    building,
                                                    mapOrigin,
                                                    tileWidth,
                                                    tileHeight));
  }

  if (selectedBuildAsset == nullptr) {
    return;
  }

  const bool previewValid = map.canPlace(foundationTiles(previewPlacement));
  drawPlacementPreview(renderer, previewPlacement, map, mapOrigin, tileWidth, tileHeight);
  renderQueue.push_back(makeBuildingRenderCommand(
    *selectedBuildAsset,
    BuildingInstance{selectedBuildAsset->id, previewPlacement, BuildingState::Preview, nowTicks},
    mapOrigin,
    tileWidth,
    tileHeight,
    previewValid ? 0.55f : 1.0f,
    previewValid ? 0.85f : 0.45f,
    previewValid ? 1.0f : 0.45f,
    0.58f));
}

std::optional<std::size_t> hitTestBuildingAtPoint(const BuildingAssetMap& buildingAssets,
                                                  const std::vector<BuildingInstance>& buildings,
                                                  const float mouseX,
                                                  const float mouseY,
                                                  const Vec2 mapOrigin,
                                                  const float tileWidth,
                                                  const float tileHeight) {
  std::optional<std::size_t> selectedIndex;
  float selectedDepth = std::numeric_limits<float>::infinity();
  const auto mouseTile = screenToIso(mouseX, mouseY, mapOrigin, tileWidth, tileHeight);

  for (std::size_t index = 0; index < buildings.size(); ++index) {
    const auto& building = buildings[index];
    const auto footprint = foundationTiles(building.placement);
    const bool containsMouseTile = std::any_of(footprint.begin(), footprint.end(), [&](const TileCoord coord) {
      return coord.x == mouseTile.x && coord.y == mouseTile.y;
    });
    if (!containsMouseTile) {
      continue;
    }

    const auto& asset = assetForInstance(buildingAssets, building);
    const auto command = makeBuildingRenderCommand(asset, building, mapOrigin, tileWidth, tileHeight);
    if (!selectedIndex.has_value() || command.depth01 < selectedDepth) {
      selectedIndex = index;
      selectedDepth = command.depth01;
    }
  }

  return selectedIndex;
}

void drawBuildingHealthOverlay(Renderer2D& renderer,
                               const BuildingAssetMap& buildingAssets,
                               const std::vector<BuildingInstance>& buildings,
                               const std::optional<std::size_t> buildingIndex,
                               const bool showBone,
                               const Vec2 mapOrigin,
                               const float tileWidth,
                               const float tileHeight) {
  if (!buildingIndex.has_value() || *buildingIndex >= buildings.size()) {
    return;
  }

  const auto& building = buildings[*buildingIndex];
  if (building.maxHp <= 0) {
    return;
  }

  const auto& asset = assetForInstance(buildingAssets, building);

  if (showBone) {
    drawSelectedBuildingBone(renderer, asset, building, mapOrigin, tileWidth, tileHeight);
  }

  const int maxBloodNum = std::max(1, building.maxHp / 100);
  int currentBloodNum = building.hp <= 0 ? 0 : building.hp / 100;
  if (building.hp > 0) {
    currentBloodNum = std::max(1, currentBloodNum);
  }
  currentBloodNum = std::clamp(currentBloodNum, 0, maxBloodNum);

  constexpr float kScale = 1.0f;
  const int barWidth = 10 + (maxBloodNum - 1) * 4;
  const int startX = barWidth - 6;
  const Vec2 barOrigin = buildingHealthBarOrigin(asset, building, mapOrigin, tileWidth, tileHeight, barWidth);

  const float hpRatio = std::clamp(static_cast<float>(building.hp) / static_cast<float>(building.maxHp), 0.0f, 1.0f);
  BuildingHealthColors colors{
    BarColor{0.0f, 50.0f / 255.0f, 0.0f, 1.0f},
    BarColor{0.0f, 128.0f / 255.0f, 0.0f, 1.0f},
    BarColor{0.0f, 1.0f, 0.0f, 1.0f}
  };
  if (hpRatio <= 0.25f) {
    colors = BuildingHealthColors{
      BarColor{134.0f / 255.0f, 17.0f / 255.0f, 34.0f / 255.0f, 1.0f},
      BarColor{204.0f / 255.0f, 34.0f / 255.0f, 34.0f / 255.0f, 1.0f},
      BarColor{238.0f / 255.0f, 68.0f / 255.0f, 51.0f / 255.0f, 1.0f}
    };
  } else if (hpRatio <= 0.5f) {
    colors = BuildingHealthColors{
      BarColor{50.0f / 255.0f, 50.0f / 255.0f, 0.0f, 1.0f},
      BarColor{128.0f / 255.0f, 128.0f / 255.0f, 0.0f, 1.0f},
      BarColor{1.0f, 1.0f, 0.0f, 1.0f}
    };
  }

  int x = startX;
  int y = 0;
  for (int i = 0; i < currentBloodNum; ++i) {
    drawFilledBuildingHealthPip(renderer,
                                barOrigin,
                                x,
                                y,
                                kScale,
                                colors,
                                i == 0,
                                i == currentBloodNum - 1);
    x -= 4;
    y += 2;
  }

  const int emptyBloodNum = maxBloodNum - currentBloodNum;
  for (int i = 0; i < emptyBloodNum; ++i) {
    drawEmptyBuildingHealthPip(renderer,
                               barOrigin,
                               x,
                               y,
                               kScale,
                               i == emptyBloodNum - 1);
    x -= 4;
    y += 2;
  }
}

void redrawBuildingOccludersForRhino(Renderer2D& renderer,
                                     const BuildingAssetMap& buildingAssets,
                                     const std::vector<BuildingInstance>& buildings,
                                     const RhinoUnitState& rhinoUnit,
                                     const Vec2 mapOrigin,
                                     const float tileWidth,
                                     const float tileHeight,
                                     const int viewportWidth,
                                     const int viewportHeight,
                                     const float rhinoFootprintK,
                                     const std::uint32_t nowTicks) {
  const auto candidateTiles = occluderCandidateTileSet(rhinoUnit);
  std::vector<BuildingRenderCommand> occluders;
  occluders.reserve(buildings.size());

  for (const auto& building : buildings) {
    if (!buildingMayOccludeRhino(building, candidateTiles)) {
      continue;
    }

    const auto& asset = assetForInstance(buildingAssets, building);
    occluders.push_back(makeBuildingRenderCommand(asset,
                                                  building,
                                                  mapOrigin,
                                                  tileWidth,
                                                  tileHeight));
  }

  if (occluders.empty()) {
    return;
  }

  std::sort(occluders.begin(),
            occluders.end(),
            [](const BuildingRenderCommand& lhs, const BuildingRenderCommand& rhs) {
              return lhs.depth01 > rhs.depth01;
            });

  const auto clip = rhinoRedrawClipRect(rhinoUnit, mapOrigin, tileWidth, tileHeight, rhinoFootprintK);
  const int scissorX = std::max(0, static_cast<int>(std::floor(clip.left)));
  const int scissorYTop = std::max(0, static_cast<int>(std::floor(clip.top)));
  const int scissorRight = std::min(viewportWidth, static_cast<int>(std::ceil(clip.right)));
  const int scissorBottom = std::min(viewportHeight, static_cast<int>(std::ceil(clip.bottom)));
  const int scissorWidth = std::max(0, scissorRight - scissorX);
  const int scissorHeight = std::max(0, scissorBottom - scissorYTop);
  if (scissorWidth <= 0 || scissorHeight <= 0) {
    return;
  }

  renderer.beginUiPass();
  glEnable(GL_SCISSOR_TEST);
  glScissor(scissorX,
            viewportHeight - (scissorYTop + scissorHeight),
            scissorWidth,
            scissorHeight);

  for (const auto& occluder : occluders) {
    drawBuildingForegroundOverlay(renderer,
                                  *occluder.asset,
                                  occluder.instance,
                                  rhinoUnit,
                                  mapOrigin,
                                  tileWidth,
                                  tileHeight,
                                  nowTicks,
                                  occluder.depth01);
  }

  glDisable(GL_SCISSOR_TEST);
}

void drawWarFactoryOverUnitLayers(Renderer2D& renderer,
                                  const BuildingAsset& asset,
                                  const BuildingInstance& building,
                                  const Vec2 mapOrigin,
                                  const float tileWidth,
                                  const float tileHeight,
                                  const std::uint32_t nowTicks,
                                  const float depth01,
                                  const bool productionSplit) {
  if (!isWarFactory(asset.id) || building.state != BuildingState::Complete) {
    return;
  }

  renderer.beginUiPass();
  if (productionSplit) {
    drawWarFactoryProductionLayerSubset(renderer,
                                        asset,
                                        building,
                                        mapOrigin,
                                        tileWidth,
                                        tileHeight,
                                        nowTicks,
                                        depth01,
                                        false);
    return;
  }

  RhinoUnitState dummyRhino;
  drawBuildingForegroundOverlay(renderer,
                                asset,
                                building,
                                dummyRhino,
                                mapOrigin,
                                tileWidth,
                                tileHeight,
                                nowTicks,
                                depth01);
}

void drawWarFactoryProductionUnderUnitLayers(Renderer2D& renderer,
                                             const BuildingAsset& asset,
                                             const BuildingInstance& building,
                                             const Vec2 mapOrigin,
                                             const float tileWidth,
                                             const float tileHeight,
                                             const std::uint32_t nowTicks,
                                             const float depth01) {
  if (asset.id != "NAWEAP" || building.state != BuildingState::Complete) {
    return;
  }

  renderer.beginWorldPass();
  drawWarFactoryProductionLayerSubset(renderer,
                                      asset,
                                      building,
                                      mapOrigin,
                                      tileWidth,
                                      tileHeight,
                                      nowTicks,
                                      depth01,
                                      true);
}
