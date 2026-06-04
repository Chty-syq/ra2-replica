#pragma once

#include "building_system.h"
#include "iso_world.h"
#include "map_grid.h"
#include "renderer2d.h"
#include "sidebar.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

struct RhinoUnitState;

void updateSidebarStateForDemo(SidebarState& sidebarState,
                               const SidebarAssets& sidebarAssets,
                               int viewportHeight,
                               std::uint32_t nowTicks);

void moveFirstBuilding(std::vector<BuildingInstance>& buildings,
                       MapGrid& map,
                       TileCoord delta);

void tryPlaceSelectedBuilding(float mouseX,
                              float mouseY,
                              std::uint32_t nowTicks,
                              float viewportWidth,
                              float sidebarWidth,
                              Vec2 mapOrigin,
                              float tileWidth,
                              float tileHeight,
                              MapGrid& map,
                              std::vector<BuildingInstance>& buildings,
                              const BuildingAsset*& selectedBuildAsset,
                              int selectedBuildMaxHp,
                              BuildingPlacement& previewPlacement);

void updatePreviewPlacementFromMouse(float mouseX,
                                     float mouseY,
                                     float viewportWidth,
                                     float sidebarWidth,
                                     Vec2 mapOrigin,
                                     float tileWidth,
                                     float tileHeight,
                                     const BuildingAsset* selectedBuildAsset,
                                     BuildingPlacement& previewPlacement);

void buildRenderQueue(const BuildingAssetMap& buildingAssets,
                      const std::vector<BuildingInstance>& buildings,
                      const BuildingAsset* selectedBuildAsset,
                      const BuildingPlacement& previewPlacement,
                      std::uint32_t nowTicks,
                      const MapGrid& map,
                      Renderer2D& renderer,
                      Vec2 mapOrigin,
                      float tileWidth,
                      float tileHeight,
                      std::vector<BuildingRenderCommand>& renderQueue);

[[nodiscard]] std::optional<std::size_t> hitTestBuildingAtPoint(const BuildingAssetMap& buildingAssets,
                                                                const std::vector<BuildingInstance>& buildings,
                                                                float mouseX,
                                                                float mouseY,
                                                                Vec2 mapOrigin,
                                                                float tileWidth,
                                                                float tileHeight);

void drawBuildingHealthOverlay(Renderer2D& renderer,
                               const BuildingAssetMap& buildingAssets,
                               const std::vector<BuildingInstance>& buildings,
                               std::optional<std::size_t> buildingIndex,
                               bool showBone,
                               Vec2 mapOrigin,
                               float tileWidth,
                               float tileHeight);

void redrawBuildingOccludersForRhino(Renderer2D& renderer,
                                     const BuildingAssetMap& buildingAssets,
                                     const std::vector<BuildingInstance>& buildings,
                                     const RhinoUnitState& rhinoUnit,
                                     Vec2 mapOrigin,
                                     float tileWidth,
                                     float tileHeight,
                                     int viewportWidth,
                                     int viewportHeight,
                                     float rhinoFootprintK,
                                     std::uint32_t nowTicks);

void drawWarFactoryOverUnitLayers(Renderer2D& renderer,
                                  const BuildingAsset& asset,
                                  const BuildingInstance& building,
                                  Vec2 mapOrigin,
                                  float tileWidth,
                                  float tileHeight,
                                  std::uint32_t nowTicks,
                                  float depth01);
