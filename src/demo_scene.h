#pragma once

#include "building_system.h"
#include "iso_world.h"
#include "map_grid.h"
#include "renderer2d.h"
#include "sidebar.h"

#include <cstdint>
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
