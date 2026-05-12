#include "demo_scene.h"
#include "rhino_unit.h"

#include <SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <unordered_set>

namespace {
struct ScreenRect {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;
};

[[nodiscard]] std::uint64_t tileKey(const TileCoord coord) {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(coord.x)) << 32) |
         static_cast<std::uint32_t>(coord.y);
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

void drawBuildingForegroundOverlay(Renderer2D& renderer,
                                   const BuildingAsset& asset,
                                   const BuildingInstance& instance,
                                   const Vec2 origin,
                                   const float tileWidth,
                                   const float tileHeight,
                                   const std::uint32_t nowTicks,
                                   const float depth01) {
  const UiTexture* texture = &asset.completeTexture;
  const DecodedFrame* frame = &asset.completeFrame;

  if (instance.state == BuildingState::Constructing && !asset.buildupTextures.empty()) {
    constexpr std::uint32_t kBuildupFrameDurationMs = 50;
    const std::size_t frameIndex = std::min<std::size_t>(
      (nowTicks - instance.stateStartTicks) / kBuildupFrameDurationMs,
      asset.buildupTextures.size() - 1);
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
  const Vec2 spriteBottomRight{spriteTopLeft.x + texture->width, spriteTopLeft.y + texture->height};
  const Vec2 spriteBottomLeft{spriteTopLeft.x, spriteTopLeft.y + texture->height};

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

  const auto baseVertices = makeQuadVertices(spriteTopLeft,
                                             spriteTopRight,
                                             spriteBottomRight,
                                             spriteBottomLeft,
                                             depth01,
                                             1.0f,
                                             1.0f,
                                             1.0f,
                                             1.0f);
  renderer.draw(GL_TRIANGLES,
                *texture,
                baseVertices.data(),
                baseVertices.size(),
                logicalDepthWithFootprint);

  if (instance.state != BuildingState::Complete) {
    return;
  }

  for (std::size_t layerIndex = 1; layerIndex < asset.completeLayers.size(); ++layerIndex) {
    const auto& layer = asset.completeLayers[layerIndex];
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
                                                1.0f,
                                                1.0f,
                                                1.0f,
                                                1.0f);
    renderer.draw(GL_TRIANGLES,
                  *layerTexture,
                  layerVertices.data(),
                  layerVertices.size(),
                  logicalDepthWithFootprint);
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
  sidebarState.tabVisible = {true, true, false, false};
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
  buildings.push_back(BuildingInstance{selectedBuildAsset->id, candidate, state, nowTicks});
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
                                  mapOrigin,
                                  tileWidth,
                                  tileHeight,
                                  nowTicks,
                                  occluder.depth01);
  }

  glDisable(GL_SCISSOR_TEST);
}
