#include "building_system.h"
#include "demo_app_support.h"
#include "demo_scene.h"
#include "imgui_debug_panel.h"
#include "iso_grid_renderer.h"
#include "iso_world.h"
#include "palette.h"
#include "renderer2d.h"
#include "rhino_unit.h"
#include "rhino_render_state.h"
#include "rules_colors.h"
#include "sidebar.h"
#include "vpl_box_renderer.h"
#include "voxel_unit.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
struct ViewportState {
  int width = DemoAppConfig::kViewportWidth;
  int height = DemoAppConfig::kViewportHeight;
};

struct SimulationFrame {
  std::uint32_t nowTicks = 0;
  float deltaSeconds = 0.0f;
};

enum class McvTransitionPhase {
  None,
  TurningToDeploy,
  PackingToVehicle
};

struct McvTransitionState {
  McvTransitionPhase phase = McvTransitionPhase::None;
  BuildFaction faction = BuildFaction::Allied;
  BuildingPlacement placement{};
  TileCoord pendingMoveTarget{};
  std::size_t buildingIndex = 0;
  float hpRatio = 1.0f;
};

struct WarFactoryProductionState {
  bool active = false;
  BuildingPlacement factoryPlacement{};
  TileCoord exitCell{};
  std::size_t tankIndex = 0;
};

constexpr int kMcvDeployDirectionIndex = 12;
constexpr int kWarFactoryExitDirectionIndex = 0;
constexpr std::uint32_t kBuildingBuildupFrameDurationMs = 50;

void cleanupApp(SDL_Window*& window,
                SDL_GLContext& glContext,
                Renderer2D& renderer,
                IsoGridRenderer& gridRenderer,
                VplBoxRenderer& rhinoRenderer,
                VplBoxRenderer& mcvRenderer,
                SidebarAssets& sidebarAssets,
                BuildingAssetCache& buildingAssetCache,
                RhinoUnitUiAssets& rhinoUiAssets) {
  shutdownImGuiDebugPanel();
  destroySidebarAssets(sidebarAssets);
  destroyBuildingAssetCache(buildingAssetCache);
  destroyRhinoUnitUiAssets(rhinoUiAssets);
  mcvRenderer.destroy();
  rhinoRenderer.destroy();
  gridRenderer.destroy();
  renderer.destroy();

  if (glContext != nullptr) {
    SDL_GL_DeleteContext(glContext);
    glContext = nullptr;
  }
  if (window != nullptr) {
    SDL_DestroyWindow(window);
    window = nullptr;
  }

  SDL_Quit();
}

[[nodiscard]] ViewportState viewportStateForPanel(const ImGuiDebugPanelState& debugPanelState) {
  const std::size_t safeIndex =
    std::min(debugPanelState.display.resolutionIndex, std::size(kDisplayResolutionOptions) - 1);
  const auto& resolution = kDisplayResolutionOptions[safeIndex];
  return ViewportState{resolution.width, resolution.height};
}

[[nodiscard]] Vec2 mapOriginForViewport(const ViewportState& viewport) {
  return Vec2{viewport.width * 0.5f, DemoAppConfig::kMapOrigin.y};
}

[[nodiscard]] const char* defaultPowerPlantId(const BuildFaction faction) {
  switch (faction) {
    case BuildFaction::Allied: return "GAPOWR";
    case BuildFaction::Soviet: return "NAPOWR";
  }
  return "GAPOWR";
}

[[nodiscard]] const char* constructionYardIdForFaction(const BuildFaction faction) {
  switch (faction) {
    case BuildFaction::Allied: return "GACNST";
    case BuildFaction::Soviet: return "NACNST";
  }
  return "GACNST";
}

[[nodiscard]] const char* mcvUnitIdForFaction(const BuildFaction faction) {
  switch (faction) {
    case BuildFaction::Allied: return "AMCV";
    case BuildFaction::Soviet: return "SMCV";
  }
  return "AMCV";
}

[[nodiscard]] const char* mcvVoxelStemForFaction(const BuildFaction faction) {
  switch (faction) {
    case BuildFaction::Allied: return "mcv";
    case BuildFaction::Soviet: return "smcv";
  }
  return "mcv";
}

[[nodiscard]] const char* warFactoryIdForFaction(const BuildFaction faction) {
  switch (faction) {
    case BuildFaction::Allied: return "GAWEAP";
    case BuildFaction::Soviet: return "NAWEAP";
  }
  return "GAWEAP";
}

[[nodiscard]] bool isTankProductionIcon(const std::string_view iconId) {
  return iconId == "htnkicon" || iconId == "gtnkicon";
}

[[nodiscard]] std::optional<BuildFaction> factionForConstructionYardId(const std::string& buildingId) {
  if (buildingId == "GACNST") {
    return BuildFaction::Allied;
  }
  if (buildingId == "NACNST") {
    return BuildFaction::Soviet;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> findCompleteWarFactory(const std::vector<BuildingInstance>& buildings,
                                                                const BuildFaction faction) {
  const auto* factoryId = warFactoryIdForFaction(faction);
  for (std::size_t index = 0; index < buildings.size(); ++index) {
    const auto& building = buildings[index];
    if (building.assetId == factoryId && building.state == BuildingState::Complete) {
      return index;
    }
  }
  return std::nullopt;
}

[[nodiscard]] BuildingPlacement deploymentPlacementForUnit(const TileCoord cell, const FoundationSize foundation) {
  // The RA2-style 4x4 footprint generated by foundationTiles() is anchored around
  // this logical cell, so the MCV cell itself must be kept as the deployment anchor.
  return BuildingPlacement{
    cell,
    foundation.width,
    foundation.height
  };
}

[[nodiscard]] std::uint32_t buildingBuildupDurationMs(const BuildingAsset& asset) {
  return static_cast<std::uint32_t>(asset.buildupTextures.size()) * kBuildingBuildupFrameDurationMs;
}

[[nodiscard]] TileCoord warFactoryProductionCenter(const BuildingInstance& factory) {
  return factory.placement.topLeft;
}

[[nodiscard]] TileCoord warFactoryDoorCell(const BuildingInstance& factory) {
  return TileCoord{factory.placement.topLeft.x + 2, factory.placement.topLeft.y};
}

[[nodiscard]] TileCoord warFactoryExitCell(const BuildingInstance& factory) {
  const auto door = warFactoryDoorCell(factory);
  return TileCoord{door.x + 1, door.y};
}

[[nodiscard]] std::vector<TileCoord> warFactoryExitPath(const BuildingInstance& factory) {
  const auto center = warFactoryProductionCenter(factory);
  const auto door = warFactoryDoorCell(factory);
  return std::vector<TileCoord>{
    TileCoord{center.x + 1, center.y},
    door,
    warFactoryExitCell(factory)
  };
}

[[nodiscard]] bool isLayeredWarFactoryInstance(const BuildingInstance& building) {
  return building.assetId == "GAWEAP" && building.state == BuildingState::Complete;
}

[[nodiscard]] bool footprintContainsTile(const BuildingPlacement& placement, const TileCoord tile) {
  for (const auto footprintTile : foundationTiles(placement)) {
    if (footprintTile.x == tile.x && footprintTile.y == tile.y) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool rhinoOccupiesMouseTile(const RhinoUnitState& tank, const TileCoord mouseTile) {
  const int minX = static_cast<int>(std::floor(tank.tilePosition.x));
  const int maxX = static_cast<int>(std::ceil(tank.tilePosition.x));
  const int minY = static_cast<int>(std::floor(tank.tilePosition.y));
  const int maxY = static_cast<int>(std::ceil(tank.tilePosition.y));
  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      if (mouseTile.x == x && mouseTile.y == y) {
        return true;
      }
    }
  }
  return mouseTile.x == tank.occupiedCell.x && mouseTile.y == tank.occupiedCell.y;
}

[[nodiscard]] bool rhinoInsideWarFactoryFootprint(const RhinoUnitState& tank,
                                                  const BuildingInstance& factory) {
  if (!isLayeredWarFactoryInstance(factory)) {
    return false;
  }

  const int minX = static_cast<int>(std::floor(tank.tilePosition.x));
  const int maxX = static_cast<int>(std::ceil(tank.tilePosition.x));
  const int minY = static_cast<int>(std::floor(tank.tilePosition.y));
  const int maxY = static_cast<int>(std::ceil(tank.tilePosition.y));
  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      if (footprintContainsTile(factory.placement, TileCoord{x, y})) {
        return true;
      }
    }
  }
  return false;
}

[[nodiscard]] bool rhinoInsideAnyWarFactoryFootprint(const RhinoUnitState& tank,
                                                     const std::vector<BuildingInstance>& buildings) {
  for (const auto& building : buildings) {
    if (rhinoInsideWarFactoryFootprint(tank, building)) {
      return true;
    }
  }
  return false;
}

void deselectRhinoTanks(std::vector<RhinoUnitState>& tanks) {
  for (auto& tank : tanks) {
    tank.selected = false;
  }
}

[[nodiscard]] std::optional<std::size_t> selectedRhinoTankIndex(const std::vector<RhinoUnitState>& tanks) {
  for (std::size_t index = 0; index < tanks.size(); ++index) {
    if (tanks[index].selected) {
      return index;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> hitTestRhinoTanks(const std::vector<RhinoUnitState>& tanks,
                                                           const float mouseX,
                                                           const float mouseY,
                                                           const Vec2 mapOrigin,
                                                           const float tileWidth,
                                                           const float tileHeight) {
  const auto mouseTile = screenToIso(mouseX, mouseY, mapOrigin, tileWidth, tileHeight);
  for (std::size_t reverseIndex = tanks.size(); reverseIndex > 0; --reverseIndex) {
    const std::size_t index = reverseIndex - 1;
    if (rhinoOccupiesMouseTile(tanks[index], mouseTile)) {
      return index;
    }
  }
  return std::nullopt;
}

[[nodiscard]] SidebarAssets loadSidebarAssetsForStyle(const DemoAppPaths& paths,
                                                      const DemoVisualStyle& style,
                                                      const Palette& cameoPalette) {
  const auto sidebarPalette = Palette::load(sidebarPalettePathForFaction(paths, style.faction));
  return loadSidebarAssets(sidebarUiRootForFaction(paths, style.faction),
                           paths.iconRoot,
                           sidebarPalette,
                           cameoPalette,
                           style.faction);
}

void loadMcvAssetsForFaction(VplBoxRenderer& mcvRenderer,
                             const DemoAppPaths& paths,
                             const BuildFaction faction,
                             const VplFile& voxelLighting) {
  mcvRenderer.loadVehicleAssets(mcvVoxelRootForFaction(paths, faction),
                                voxelLighting,
                                mcvVoxelStemForFaction(faction));
}

void applyViewportState(SDL_Window* window,
                        Renderer2D& renderer,
                        IsoGridRenderer& gridRenderer,
                        const ViewportState& viewport) {
  SDL_SetWindowSize(window, viewport.width, viewport.height);
  renderer.setViewport(viewport.width, viewport.height);
  gridRenderer.setViewport(viewport.width, viewport.height);
}

[[nodiscard]] SimulationFrame advanceSimulationClock(const ImGuiDebugPanelState& debugPanelState,
                                                     std::uint32_t& lastRealTicks,
                                                     double& simulationTicks) {
  const std::uint32_t realNowTicks = SDL_GetTicks();
  const std::uint32_t realDeltaTicks = realNowTicks - lastRealTicks;
  lastRealTicks = realNowTicks;

  const double speedMultiplier =
    std::clamp(static_cast<double>(debugPanelState.gameplay.speedMultiplier), 0.0, 8.0);
  const double simulatedDeltaTicks = static_cast<double>(realDeltaTicks) * speedMultiplier;
  simulationTicks += simulatedDeltaTicks;
  return SimulationFrame{
    static_cast<std::uint32_t>(simulationTicks),
    static_cast<float>(simulatedDeltaTicks / 1000.0)
  };
}
}  // namespace

int main(int, char**) {
  SDL_Window* window = nullptr;
  SDL_GLContext glContext = nullptr;
  Renderer2D renderer;
  IsoGridRenderer gridRenderer;
  VplBoxRenderer rhinoRenderer;
  VplBoxRenderer mcvRenderer;
  SidebarAssets sidebarAssets;
  BuildingAssetCache buildingAssetCache;
  RhinoUnitUiAssets rhinoUiAssets;

  try {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
      throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }

    configureOpenGlContext();

    ImGuiDebugPanelState debugPanelState{};
    const ViewportState initialViewport = viewportStateForPanel(debugPanelState);

    window = SDL_CreateWindow("RA2 Replica Demo - Construction Sandbox",
                              SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED,
                              initialViewport.width,
                              initialViewport.height,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (window == nullptr) {
      throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }

    glContext = SDL_GL_CreateContext(window);
    if (glContext == nullptr) {
      throw std::runtime_error(std::string("SDL_GL_CreateContext failed: ") + SDL_GetError());
    }
    SDL_GL_SetSwapInterval(1);

    renderer.initialize(initialViewport.width, initialViewport.height);
    gridRenderer.initialize(initialViewport.width, initialViewport.height);
    initializeImGuiDebugPanel(window, glContext);

    const auto paths = locateDemoAppPaths(std::filesystem::current_path());
    const auto artIni = ArtIni::load(paths.settingsRoot / "art.ini");
    const auto houseColors = loadHouseColors(paths.settingsRoot / "rules.ini");
    const auto objectStrengths = loadObjectStrengths(paths.settingsRoot / "rules.ini");
    const auto voxelLighting = VplFile::load(paths.paletteRoot / "voxel" / "voxels.vpl");
    const auto paletteLibrary = loadPaletteLibrary(paths.paletteRoot);
    const auto cameoPalette = Palette::load(paths.paletteRoot / "ui" / "cameo.pal");
    const auto mousePalette = Palette::load(paths.paletteRoot / "ui" / "mousepal.pal");
    const auto overlayPalette = Palette::load(paths.paletteRoot / "ui" / "palette.pal");
    (void)mousePalette;

    debugPanelState.style.houseColorIndex = houseColors.defaultIndex;
    ViewportState viewport = viewportStateForPanel(debugPanelState);

    const auto& initialTheaterPalettes = palettesForTheater(paletteLibrary, debugPanelState.style.theater);
    renderer.setIndexedPalettes(initialTheaterPalettes.unit, initialTheaterPalettes.terrain);
    renderer.setRemapColor(activeHouseColorValue(debugPanelState.style, houseColors));

    BuildingAssetMap* activeBuildingAssets =
      &cachedAssetsForStyle(buildingAssetCache,
                            paths.buildingRoot,
                            artIni,
                            paletteLibrary,
                            houseColors,
                            debugPanelState.style);
    sidebarAssets = loadSidebarAssetsForStyle(paths, debugPanelState.style, cameoPalette);
    rhinoUiAssets = loadRhinoUnitUiAssets(paths.unitOverlayRoot, overlayPalette);

    rhinoRenderer.initialize(window);
    rhinoRenderer.loadRhinoAssets(paths.rhinoRoot, voxelLighting);
    rhinoRenderer.setPalette(initialTheaterPalettes.unit);
    mcvRenderer.initialize(window);
    BuildFaction mcvFaction = debugPanelState.style.faction;
    loadMcvAssetsForFaction(mcvRenderer, paths, mcvFaction, voxelLighting);
    mcvRenderer.setPalette(initialTheaterPalettes.unit);

    SidebarState sidebarState{};
    MapGrid map(64, 64);
    const auto maxHpForObject = [&](const std::string& objectId) {
      const auto it = objectStrengths.find(objectId);
      return it != objectStrengths.end() ? it->second : 1000;
    };
    const auto visualMaxHpForFoundation = [](const FoundationSize foundation) {
      const int minAxis = std::min(foundation.width, foundation.height);
      const int maxAxis = std::max(foundation.width, foundation.height);
      if (minAxis >= 4 && maxAxis >= 4) {
        return 3000;
      }
      if (minAxis >= 3 || maxAxis >= 4) {
        return 2200;
      }
      if (maxAxis >= 2) {
        return 1500;
      }
      return 700;
    };
    const auto maxHpForBuilding = [&](const BuildingAsset& asset) {
      return std::max(maxHpForObject(asset.id), visualMaxHpForFoundation(asset.foundation));
    };

    const std::string initialBuildingId = defaultPowerPlantId(debugPanelState.style.faction);
    const auto& initialAsset = ensureCachedAssetForStyle(buildingAssetCache,
                                                         paths.buildingRoot,
                                                         artIni,
                                                         paletteLibrary,
                                                         houseColors,
                                                         debugPanelState.style,
                                                         initialBuildingId);
    const BuildingPlacement initialPlacement{
      {18, 14},
      initialAsset.foundation.width,
      initialAsset.foundation.height
    };
    if (!map.canPlace(foundationTiles(initialPlacement))) {
      throw std::runtime_error("Initial power plant placement is invalid");
    }

    const int initialBuildingMaxHp = maxHpForBuilding(initialAsset);
    std::vector<BuildingInstance> buildings{
      BuildingInstance{
        initialBuildingId,
        initialPlacement,
        BuildingState::Complete,
        0,
        initialBuildingMaxHp,
        initialBuildingMaxHp
      }
    };
    map.setOccupied(foundationTiles(initialPlacement), true);

    std::vector<RhinoUnitState> rhinoTanks(1);
    initializeRhinoUnit(rhinoTanks.front(), map, TileCoord{7, 10});
    RhinoUnitState mcvUnit;
    bool mcvActive = true;
    McvTransitionState mcvTransition;
    WarFactoryProductionState tankProduction;
    initializeRhinoUnit(mcvUnit, map, TileCoord{10, 10});
    mcvUnit.maxHp = maxHpForObject(mcvUnitIdForFaction(mcvFaction));
    mcvUnit.hp = mcvUnit.maxHp;
    const BuildingAsset* selectedBuildAsset = nullptr;
    std::optional<std::size_t> selectedBuildingIndex;
    BuildingPlacement previewPlacement = initialPlacement;
    std::vector<BuildingRenderCommand> renderQueue;

    std::uint32_t lastRealTicks = SDL_GetTicks();
    double simulationTicks = static_cast<double>(lastRealTicks);

    bool running = true;
    while (running) {
      const auto simulationFrame = advanceSimulationClock(debugPanelState, lastRealTicks, simulationTicks);
      const std::uint32_t nowTicks = simulationFrame.nowTicks;
      Vec2 mapOrigin = mapOriginForViewport(viewport);

      updateSidebarStateForDemo(sidebarState, sidebarAssets, viewport.height, nowTicks);
      updateConstructionStates(buildings, *activeBuildingAssets, nowTicks);

      SDL_Event event{};
      while (SDL_PollEvent(&event)) {
        processImGuiDebugPanelEvent(event);

        if (event.type == SDL_QUIT) {
          running = false;
          continue;
        }

        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
          running = false;
          continue;
        }

        if (event.type == SDL_KEYDOWN &&
            event.key.keysym.sym == SDLK_d &&
            (event.key.keysym.mod & KMOD_CTRL) != 0) {
          debugPanelState.visible = !debugPanelState.visible;
          continue;
        }

        if (event.type == SDL_KEYDOWN &&
            debugPanelState.visible &&
            imguiDebugPanelWantsKeyboard()) {
          continue;
        }

        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
          const float mouseX = static_cast<float>(event.button.x);
          const float mouseY = static_cast<float>(event.button.y);

          if (imguiDebugPanelContainsPoint(debugPanelState, mouseX, mouseY) ||
              (debugPanelState.visible && imguiDebugPanelWantsMouse())) {
            continue;
          }

          const auto sidebarClick = handleSidebarLeftClick(sidebarState,
                                                           sidebarAssets,
                                                           static_cast<float>(viewport.width),
                                                           static_cast<float>(viewport.height),
                                                           mouseX,
                                                           mouseY);
          if (sidebarClick.consumed) {
            applySidebarClick(sidebarState, sidebarAssets, sidebarClick);
            if (sidebarClick.action == SidebarClickAction::ClickIcon) {
              const auto buildingId = buildingIdForIcon(sidebarClick.iconId, debugPanelState.style.faction);
              if (buildingId.has_value()) {
                selectedBuildAsset = &ensureCachedAssetForStyle(buildingAssetCache,
                                                                paths.buildingRoot,
                                                                artIni,
                                                                paletteLibrary,
                                                                houseColors,
                                                                debugPanelState.style,
                                                                *buildingId);
                previewPlacement = placementFromAnchor(previewPlacement.topLeft,
                                                       selectedBuildAsset->foundation);
                selectedBuildingIndex.reset();
                deselectRhinoTanks(rhinoTanks);
                if (mcvActive) {
                  mcvUnit.selected = false;
                }
              } else if (isTankProductionIcon(sidebarClick.iconId) &&
                         !tankProduction.active &&
                         mcvTransition.phase == McvTransitionPhase::None) {
                const auto factoryIndex = findCompleteWarFactory(buildings, debugPanelState.style.faction);
                if (factoryIndex.has_value()) {
                  const auto& factory = buildings[*factoryIndex];
                  const TileCoord productionCenter = warFactoryProductionCenter(factory);
                  const auto exitPath = warFactoryExitPath(factory);
                  const TileCoord exitCell = exitPath.empty() ? productionCenter : exitPath.back();
                  if (map.isBuildable(exitCell)) {
                    map.setOccupied(foundationTiles(factory.placement), true);

                    deselectRhinoTanks(rhinoTanks);
                    RhinoUnitState producedTank;
                    producedTank.tilePosition =
                      Vec2{static_cast<float>(productionCenter.x), static_cast<float>(productionCenter.y)};
                    producedTank.occupiedCell = productionCenter;
                    producedTank.selected = true;
                    producedTank.maxHp = maxHpForObject("HTNK");
                    producedTank.hp = producedTank.maxHp;
                    producedTank.path = exitPath;
                    producedTank.moveTarget = exitCell;
                    producedTank.steeringTarget = productionCenter;
                    producedTank.steeringDirectionIndex = 0;
                    producedTank.hasPendingMove = false;
                    producedTank.finishPathBeforePendingMove = true;
                    producedTank.pendingMoveTarget = exitCell;
                    producedTank.waypointVisibleUntilTicks = 0;
                    setRhinoDirectionIndex(producedTank, kWarFactoryExitDirectionIndex);
                    rhinoTanks.push_back(producedTank);

                    tankProduction.active = true;
                    tankProduction.factoryPlacement = factory.placement;
                    tankProduction.exitCell = exitCell;
                    tankProduction.tankIndex = rhinoTanks.size() - 1;
                    selectedBuildAsset = nullptr;
                    selectedBuildingIndex.reset();
                    if (mcvActive) {
                      mcvUnit.selected = false;
                    }
                  }
                }
              }
            }
            continue;
          }

          if (selectedBuildAsset != nullptr) {
            const int selectedBuildMaxHp = maxHpForBuilding(*selectedBuildAsset);
            tryPlaceSelectedBuilding(mouseX,
                                     mouseY,
                                     nowTicks,
                                     static_cast<float>(viewport.width),
                                     DemoAppConfig::kSidebarWidth,
                                     mapOrigin,
                                     DemoAppConfig::kTileWidth,
                                     DemoAppConfig::kTileHeight,
                                     map,
                                     buildings,
                                     selectedBuildAsset,
                                     selectedBuildMaxHp,
                                     previewPlacement);
            continue;
          }

          if (mcvTransition.phase != McvTransitionPhase::None) {
            continue;
          }

          if (mcvActive) {
            const auto mouseTile = screenToIso(mouseX,
                                               mouseY,
                                               mapOrigin,
                                               DemoAppConfig::kTileWidth,
                                               DemoAppConfig::kTileHeight);
            const bool hitMcv = rhinoOccupiesMouseTile(mcvUnit, mouseTile);
            if (hitMcv) {
              if (mcvUnit.selected && mcvUnit.path.empty()) {
                const auto constructionYardId = std::string(constructionYardIdForFaction(mcvFaction));
                const auto& constructionYardAsset = ensureCachedAssetForStyle(buildingAssetCache,
                                                                              paths.buildingRoot,
                                                                              artIni,
                                                                              paletteLibrary,
                                                                              houseColors,
                                                                              debugPanelState.style,
                                                                              constructionYardId);
                const auto placement = deploymentPlacementForUnit(mcvUnit.occupiedCell,
                                                                  constructionYardAsset.foundation);
                map.setOccupied(std::vector<TileCoord>{mcvUnit.occupiedCell}, false);
                if (map.canPlace(foundationTiles(placement))) {
                  mcvTransition.phase = McvTransitionPhase::TurningToDeploy;
                  mcvTransition.faction = mcvFaction;
                  mcvTransition.placement = placement;
                  mcvTransition.hpRatio = mcvUnit.maxHp > 0
                                            ? static_cast<float>(mcvUnit.hp) / static_cast<float>(mcvUnit.maxHp)
                                            : 1.0f;
                  mcvUnit.path.clear();
                  mcvUnit.hasPendingMove = false;
                  mcvUnit.selected = true;
                  selectedBuildingIndex.reset();
                  deselectRhinoTanks(rhinoTanks);
                }
                map.setOccupied(std::vector<TileCoord>{mcvUnit.occupiedCell}, true);
              } else {
                mcvUnit.selected = true;
                deselectRhinoTanks(rhinoTanks);
                selectedBuildingIndex.reset();
              }
              continue;
            }
          }

          const auto hitRhino = hitTestRhinoTanks(rhinoTanks,
                                                  mouseX,
                                                  mouseY,
                                                  mapOrigin,
                                                  DemoAppConfig::kTileWidth,
                                                  DemoAppConfig::kTileHeight);
          if (hitRhino.has_value()) {
            deselectRhinoTanks(rhinoTanks);
            rhinoTanks[*hitRhino].selected = true;
            if (mcvActive) {
              mcvUnit.selected = false;
            }
            selectedBuildingIndex.reset();
            continue;
          }

          const auto hitBuilding = hitTestBuildingAtPoint(*activeBuildingAssets,
                                                          buildings,
                                                          mouseX,
                                                          mouseY,
                                                          mapOrigin,
                                                          DemoAppConfig::kTileWidth,
                                                          DemoAppConfig::kTileHeight);
          if (hitBuilding.has_value()) {
            selectedBuildingIndex = hitBuilding;
            deselectRhinoTanks(rhinoTanks);
            if (mcvActive) {
              mcvUnit.selected = false;
            }
            continue;
          }

          if (selectedBuildingIndex.has_value() && !mcvActive) {
            auto& selectedBuilding = buildings[*selectedBuildingIndex];
            const auto yardFaction = factionForConstructionYardId(selectedBuilding.assetId);
            if (yardFaction.has_value() && selectedBuilding.state == BuildingState::Complete) {
              const auto targetCell = screenToIso(mouseX,
                                                  mouseY,
                                                  mapOrigin,
                                                  DemoAppConfig::kTileWidth,
                                                  DemoAppConfig::kTileHeight);
              const float hpRatio = selectedBuilding.maxHp > 0
                                      ? static_cast<float>(selectedBuilding.hp) /
                                          static_cast<float>(selectedBuilding.maxHp)
                                      : 1.0f;
              selectedBuilding.state = BuildingState::Packing;
              selectedBuilding.stateStartTicks = nowTicks;
              mcvTransition.phase = McvTransitionPhase::PackingToVehicle;
              mcvTransition.faction = *yardFaction;
              mcvTransition.placement = selectedBuilding.placement;
              mcvTransition.pendingMoveTarget = targetCell;
              mcvTransition.buildingIndex = *selectedBuildingIndex;
              mcvTransition.hpRatio = hpRatio;
              deselectRhinoTanks(rhinoTanks);
              continue;
            }
          }

          if (const auto selectedTankIndex = selectedRhinoTankIndex(rhinoTanks); selectedTankIndex.has_value()) {
            const auto targetCell = screenToIso(mouseX,
                                                mouseY,
                                                mapOrigin,
                                                DemoAppConfig::kTileWidth,
                                                DemoAppConfig::kTileHeight);
            auto& selectedTank = rhinoTanks[*selectedTankIndex];
            if (map.isBuildable(targetCell) ||
                (targetCell.x == selectedTank.occupiedCell.x && targetCell.y == selectedTank.occupiedCell.y)) {
              issueRhinoMoveCommand(selectedTank, map, targetCell, nowTicks);
            }
            continue;
          }

          if (mcvActive && mcvUnit.selected) {
            const auto targetCell = screenToIso(mouseX,
                                                mouseY,
                                                mapOrigin,
                                                DemoAppConfig::kTileWidth,
                                                DemoAppConfig::kTileHeight);
            if (map.isBuildable(targetCell) ||
                (targetCell.x == mcvUnit.occupiedCell.x && targetCell.y == mcvUnit.occupiedCell.y)) {
              issueRhinoMoveCommand(mcvUnit, map, targetCell, nowTicks);
            }
            continue;
          }

          deselectRhinoTanks(rhinoTanks);
          if (mcvActive) {
            mcvUnit.selected = false;
          }
        }

        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT) {
          if (imguiDebugPanelContainsPoint(debugPanelState,
                                           static_cast<float>(event.button.x),
                                           static_cast<float>(event.button.y)) ||
              (debugPanelState.visible && imguiDebugPanelWantsMouse())) {
            continue;
          }

          if (selectedBuildAsset != nullptr) {
            selectedBuildAsset = nullptr;
            selectedBuildingIndex.reset();
            continue;
          }

          selectedBuildingIndex.reset();
          deselectRhinoTanks(rhinoTanks);
          if (mcvActive) {
            mcvUnit.selected = false;
          }
        }

        if (event.type == SDL_MOUSEMOTION) {
          if (debugPanelState.visible &&
              imguiDebugPanelContainsPoint(debugPanelState,
                                           static_cast<float>(event.motion.x),
                                           static_cast<float>(event.motion.y))) {
            continue;
          }

          updatePreviewPlacementFromMouse(static_cast<float>(event.motion.x),
                                          static_cast<float>(event.motion.y),
                                          static_cast<float>(viewport.width),
                                          DemoAppConfig::kSidebarWidth,
                                          mapOrigin,
                                          DemoAppConfig::kTileWidth,
                                          DemoAppConfig::kTileHeight,
                                          selectedBuildAsset,
                                          previewPlacement);
        }

        if (event.type == SDL_KEYDOWN) {
          TileCoord delta{};
          switch (event.key.keysym.sym) {
            case SDLK_LEFT: delta = TileCoord{-1, 0}; break;
            case SDLK_RIGHT: delta = TileCoord{1, 0}; break;
            case SDLK_UP: delta = TileCoord{0, -1}; break;
            case SDLK_DOWN: delta = TileCoord{0, 1}; break;
            case SDLK_b:
              selectedBuildAsset = &ensureCachedAssetForStyle(buildingAssetCache,
                                                              paths.buildingRoot,
                                                              artIni,
                                                              paletteLibrary,
                                                              houseColors,
                                                              debugPanelState.style,
                                                              defaultPowerPlantId(debugPanelState.style.faction));
              previewPlacement = placementFromAnchor(previewPlacement.topLeft,
                                                     selectedBuildAsset->foundation);
              selectedBuildingIndex.reset();
              deselectRhinoTanks(rhinoTanks);
              if (mcvActive) {
                mcvUnit.selected = false;
              }
              break;
            case SDLK_c:
              selectedBuildAsset = nullptr;
              selectedBuildingIndex.reset();
              break;
            default:
              break;
          }

          moveFirstBuilding(buildings, map, delta);
        }
      }

      for (auto& tank : rhinoTanks) {
        updateRhinoUnit(tank, map, simulationFrame.deltaSeconds);
      }
      if (tankProduction.active) {
        map.setOccupied(foundationTiles(tankProduction.factoryPlacement), true);
        // The produced tank can receive a queued move while exiting. Free the
        // factory once it has reached the reserved exit cell, not only when its
        // full path becomes empty.
        if (tankProduction.tankIndex >= rhinoTanks.size()) {
          tankProduction.active = false;
        } else {
          auto& producedTank = rhinoTanks[tankProduction.tankIndex];
          const bool reachedExit =
            producedTank.occupiedCell.x == tankProduction.exitCell.x &&
            producedTank.occupiedCell.y == tankProduction.exitCell.y;
          if (reachedExit || producedTank.path.empty()) {
            if (reachedExit) {
              producedTank.tilePosition = Vec2{static_cast<float>(tankProduction.exitCell.x),
                                               static_cast<float>(tankProduction.exitCell.y)};
              producedTank.occupiedCell = tankProduction.exitCell;
              map.setOccupied(std::vector<TileCoord>{tankProduction.exitCell}, true);
            }
            tankProduction.active = false;
          }
        }
      }
      if (mcvActive) {
        updateRhinoUnit(mcvUnit, map, simulationFrame.deltaSeconds);
      }
      if (mcvTransition.phase == McvTransitionPhase::TurningToDeploy && mcvActive) {
        if (rotateRhinoTowardDirectionIndex(mcvUnit, kMcvDeployDirectionIndex, simulationFrame.deltaSeconds)) {
          const auto constructionYardId = std::string(constructionYardIdForFaction(mcvTransition.faction));
          const auto& constructionYardAsset = ensureCachedAssetForStyle(buildingAssetCache,
                                                                        paths.buildingRoot,
                                                                        artIni,
                                                                        paletteLibrary,
                                                                        houseColors,
                                                                        debugPanelState.style,
                                                                        constructionYardId);
          map.setOccupied(std::vector<TileCoord>{mcvUnit.occupiedCell}, false);
          if (map.canPlace(foundationTiles(mcvTransition.placement))) {
            const int maxHp = maxHpForBuilding(constructionYardAsset);
            const int hp = std::clamp(static_cast<int>(std::round(mcvTransition.hpRatio *
                                                                  static_cast<float>(maxHp))),
                                      1,
                                      maxHp);
            buildings.push_back(BuildingInstance{
              constructionYardId,
              mcvTransition.placement,
              constructionYardAsset.buildupTextures.empty() ? BuildingState::Complete : BuildingState::Constructing,
              nowTicks,
              maxHp,
              hp
            });
            map.setOccupied(foundationTiles(mcvTransition.placement), true);
            mcvActive = false;
            mcvUnit.selected = false;
            selectedBuildingIndex = buildings.size() - 1;
          } else {
            map.setOccupied(std::vector<TileCoord>{mcvUnit.occupiedCell}, true);
          }
          mcvTransition = McvTransitionState{};
        }
      } else if (mcvTransition.phase == McvTransitionPhase::PackingToVehicle) {
        if (mcvTransition.buildingIndex >= buildings.size()) {
          mcvTransition = McvTransitionState{};
        } else {
          const auto& packingBuilding = buildings[mcvTransition.buildingIndex];
          const auto& packingAsset = ensureCachedAssetForStyle(buildingAssetCache,
                                                               paths.buildingRoot,
                                                               artIni,
                                                               paletteLibrary,
                                                               houseColors,
                                                               debugPanelState.style,
                                                               packingBuilding.assetId);
          const auto packingDurationMs = buildingBuildupDurationMs(packingAsset);
          if (packingDurationMs == 0 || nowTicks - packingBuilding.stateStartTicks >= packingDurationMs) {
            const TileCoord mcvCell = mcvTransition.placement.topLeft;
            map.setOccupied(foundationTiles(mcvTransition.placement), false);
            buildings.erase(std::next(buildings.begin(),
                                      static_cast<std::vector<BuildingInstance>::difference_type>(
                                        mcvTransition.buildingIndex)));
            selectedBuildingIndex.reset();
            mcvFaction = mcvTransition.faction;
            loadMcvAssetsForFaction(mcvRenderer, paths, mcvFaction, voxelLighting);
            mcvRenderer.setPalette(palettesForTheater(paletteLibrary, debugPanelState.style.theater).unit);
            initializeRhinoUnit(mcvUnit, map, mcvCell);
            setRhinoDirectionIndex(mcvUnit, kMcvDeployDirectionIndex);
            mcvUnit.maxHp = maxHpForObject(mcvUnitIdForFaction(mcvFaction));
            mcvUnit.hp = std::clamp(static_cast<int>(std::round(mcvTransition.hpRatio *
                                                               static_cast<float>(mcvUnit.maxHp))),
                                   1,
                                   mcvUnit.maxHp);
            mcvUnit.selected = true;
            mcvActive = true;
            deselectRhinoTanks(rhinoTanks);
            const auto pendingMoveTarget = mcvTransition.pendingMoveTarget;
            mcvTransition = McvTransitionState{};
            if (pendingMoveTarget.x != mcvCell.x || pendingMoveTarget.y != mcvCell.y) {
              issueRhinoMoveCommand(mcvUnit, map, pendingMoveTarget, nowTicks);
            }
          }
        }
      }

      beginImGuiDebugPanelFrame();
      const ImGuiDebugPanelState previousDebugPanelState = debugPanelState;
      const bool debugPanelChanged = drawImGuiDebugPanel(debugPanelState, houseColors);
      if (debugPanelChanged) {
        const bool styleChanged = !sameVisualStyle(previousDebugPanelState.style, debugPanelState.style);
        const bool resolutionSelectionChanged =
          previousDebugPanelState.display.resolutionIndex != debugPanelState.display.resolutionIndex;

        if (styleChanged) {
          const bool factionChanged = previousDebugPanelState.style.faction != debugPanelState.style.faction;
          const bool theaterChanged = previousDebugPanelState.style.theater != debugPanelState.style.theater;
          const auto selectedBuildingId =
            selectedBuildAsset != nullptr ? std::optional<std::string>(selectedBuildAsset->id) : std::nullopt;

          switchBuildingAssetsForStyle(buildingAssetCache,
                                       activeBuildingAssets,
                                       paths.buildingRoot,
                                       artIni,
                                       paletteLibrary,
                                       houseColors,
                                       debugPanelState.style,
                                       selectedBuildAsset,
                                       previewPlacement);
          for (const auto& building : buildings) {
            ensureCachedAssetForStyle(buildingAssetCache,
                                      paths.buildingRoot,
                                      artIni,
                                      paletteLibrary,
                                      houseColors,
                                      debugPanelState.style,
                                      building.assetId);
          }
          if (factionChanged) {
            selectedBuildAsset = nullptr;
            previewPlacement = initialPlacement;
            destroySidebarAssets(sidebarAssets);
            sidebarAssets = loadSidebarAssetsForStyle(paths, debugPanelState.style, cameoPalette);
            if (mcvActive && mcvTransition.phase == McvTransitionPhase::None) {
              mcvFaction = debugPanelState.style.faction;
              loadMcvAssetsForFaction(mcvRenderer, paths, mcvFaction, voxelLighting);
              const float hpRatio = mcvUnit.maxHp > 0
                                      ? static_cast<float>(mcvUnit.hp) / static_cast<float>(mcvUnit.maxHp)
                                      : 1.0f;
              mcvUnit.maxHp = maxHpForObject(mcvUnitIdForFaction(mcvFaction));
              mcvUnit.hp = std::clamp(static_cast<int>(std::round(hpRatio * static_cast<float>(mcvUnit.maxHp))),
                                      1,
                                      mcvUnit.maxHp);
              mcvUnit.selected = false;
            }
            sidebarState = SidebarState{};
            updateSidebarStateForDemo(sidebarState, sidebarAssets, viewport.height, nowTicks);
          } else if (selectedBuildingId.has_value()) {
            selectedBuildAsset = &ensureCachedAssetForStyle(buildingAssetCache,
                                                            paths.buildingRoot,
                                                            artIni,
                                                            paletteLibrary,
                                                            houseColors,
                                                            debugPanelState.style,
                                                            *selectedBuildingId);
            previewPlacement = placementFromAnchor(previewPlacement.topLeft,
                                                   selectedBuildAsset->foundation);
          }

          const auto& currentTheaterPalettes = palettesForTheater(paletteLibrary, debugPanelState.style.theater);
          if (theaterChanged) {
            renderer.setIndexedPalettes(currentTheaterPalettes.unit, currentTheaterPalettes.terrain);
            rhinoRenderer.setPalette(currentTheaterPalettes.unit);
            mcvRenderer.setPalette(currentTheaterPalettes.unit);
          }
          renderer.setRemapColor(activeHouseColorValue(debugPanelState.style, houseColors));
        }

        if (resolutionSelectionChanged) {
          viewport = viewportStateForPanel(debugPanelState);
          applyViewportState(window, renderer, gridRenderer, viewport);
          mapOrigin = mapOriginForViewport(viewport);
        }
      }

      std::optional<std::size_t> hoveredBuildingIndex;
      int mouseXInt = 0;
      int mouseYInt = 0;
      SDL_GetMouseState(&mouseXInt, &mouseYInt);
      const float mouseX = static_cast<float>(mouseXInt);
      const float mouseY = static_cast<float>(mouseYInt);
      const bool mouseInWorld =
        mouseX >= 0.0f &&
        mouseY >= 0.0f &&
        mouseX < static_cast<float>(viewport.width) - DemoAppConfig::kSidebarWidth &&
        mouseY < static_cast<float>(viewport.height);
      const bool mouseCapturedByDebugPanel =
        imguiDebugPanelContainsPoint(debugPanelState, mouseX, mouseY) ||
        (debugPanelState.visible && imguiDebugPanelWantsMouse());
      if (selectedBuildAsset == nullptr && mouseInWorld && !mouseCapturedByDebugPanel) {
        hoveredBuildingIndex = hitTestBuildingAtPoint(*activeBuildingAssets,
                                                      buildings,
                                                      mouseX,
                                                      mouseY,
                                                      mapOrigin,
                                                      DemoAppConfig::kTileWidth,
                                                      DemoAppConfig::kTileHeight);
      }

      renderer.beginFrame(0.58f, 0.78f, 0.97f, 1.0f);
      gridRenderer.draw(mapOrigin, DemoAppConfig::kTileWidth, DemoAppConfig::kTileHeight);
      renderer.beginUiPass();

      buildRenderQueue(*activeBuildingAssets,
                       buildings,
                       selectedBuildAsset,
                       previewPlacement,
                       nowTicks,
                       map,
                       renderer,
                       mapOrigin,
                       DemoAppConfig::kTileWidth,
                       DemoAppConfig::kTileHeight,
                       renderQueue);

      auto drawRhinoTankInstance = [&](const RhinoUnitState& tank) {
        drawRhinoUnit(renderer,
                      rhinoRenderer,
                      rhinoUiAssets,
                      tank,
                      mapOrigin,
                      DemoAppConfig::kTileWidth,
                      DemoAppConfig::kTileHeight,
                      debugPanelState.rhinoPlacement.footprintK,
                      viewport.width,
                      viewport.height,
                      nowTicks,
                      buildRhinoTankRenderState(houseColors,
                                                debugPanelState,
                                                rhinoDirectionIndex(tank)));
      };

      renderer.beginWorldPass();
      for (const auto& command : renderQueue) {
        renderer.beginWorldPass();
        drawBuildingInstance(renderer,
                             *command.asset,
                             command.instance,
                             mapOrigin,
                             DemoAppConfig::kTileWidth,
                             DemoAppConfig::kTileHeight,
                             nowTicks,
                             command.depth01,
                             command.tintR,
                             command.tintG,
                             command.tintB,
                             command.tintA);

        if (isLayeredWarFactoryInstance(command.instance)) {
          for (const auto& tank : rhinoTanks) {
            if (rhinoInsideWarFactoryFootprint(tank, command.instance)) {
              drawRhinoTankInstance(tank);
            }
          }
          drawWarFactoryOverUnitLayers(renderer,
                                       *command.asset,
                                       command.instance,
                                       mapOrigin,
                                       DemoAppConfig::kTileWidth,
                                       DemoAppConfig::kTileHeight,
                                       nowTicks,
                                       command.depth01);
        }
      }

      if (mcvActive) {
        drawRhinoUnit(renderer,
                      mcvRenderer,
                      rhinoUiAssets,
                      mcvUnit,
                      mapOrigin,
                      DemoAppConfig::kTileWidth,
                      DemoAppConfig::kTileHeight,
                      debugPanelState.rhinoPlacement.footprintK,
                      viewport.width,
                      viewport.height,
                      nowTicks,
                      buildRhinoTankRenderState(houseColors,
                                                debugPanelState,
                                                rhinoDirectionIndex(mcvUnit)));
      }

      for (const auto& tank : rhinoTanks) {
        if (rhinoInsideAnyWarFactoryFootprint(tank, buildings)) {
          continue;
        }
        drawRhinoTankInstance(tank);
      }

      if (mcvActive) {
        redrawBuildingOccludersForRhino(renderer,
                                        *activeBuildingAssets,
                                        buildings,
                                        mcvUnit,
                                        mapOrigin,
                                        DemoAppConfig::kTileWidth,
                                        DemoAppConfig::kTileHeight,
                                        viewport.width,
                                        viewport.height,
                                        debugPanelState.rhinoPlacement.footprintK,
                                        nowTicks);
      }

      for (const auto& tank : rhinoTanks) {
        if (rhinoInsideAnyWarFactoryFootprint(tank, buildings)) {
          continue;
        }
        redrawBuildingOccludersForRhino(renderer,
                                        *activeBuildingAssets,
                                        buildings,
                                        tank,
                                        mapOrigin,
                                        DemoAppConfig::kTileWidth,
                                        DemoAppConfig::kTileHeight,
                                        viewport.width,
                                        viewport.height,
                                        debugPanelState.rhinoPlacement.footprintK,
                                        nowTicks);
      }

      renderer.beginUiPass();
      const bool hoveredBuildingIsSelected =
        hoveredBuildingIndex.has_value() &&
        selectedBuildingIndex.has_value() &&
        *hoveredBuildingIndex == *selectedBuildingIndex;
      if (!hoveredBuildingIsSelected) {
        drawBuildingHealthOverlay(renderer,
                                  *activeBuildingAssets,
                                  buildings,
                                  hoveredBuildingIndex,
                                  false,
                                  mapOrigin,
                                  DemoAppConfig::kTileWidth,
                                  DemoAppConfig::kTileHeight);
      }
      drawBuildingHealthOverlay(renderer,
                                *activeBuildingAssets,
                                buildings,
                                selectedBuildingIndex,
                                true,
                                mapOrigin,
                                DemoAppConfig::kTileWidth,
                                DemoAppConfig::kTileHeight);
      drawSidebar(renderer,
                  static_cast<float>(viewport.width),
                  static_cast<float>(viewport.height),
                  sidebarAssets,
                  sidebarState);
      renderImGuiDebugPanel();
      SDL_GL_SwapWindow(window);
    }

    cleanupApp(window,
               glContext,
               renderer,
               gridRenderer,
               rhinoRenderer,
               mcvRenderer,
               sidebarAssets,
               buildingAssetCache,
               rhinoUiAssets);
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    cleanupApp(window,
               glContext,
               renderer,
               gridRenderer,
               rhinoRenderer,
               mcvRenderer,
               sidebarAssets,
               buildingAssetCache,
               rhinoUiAssets);
    return 1;
  }
}
