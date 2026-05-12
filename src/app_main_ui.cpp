#include "building_system.h"
#include "demo_app_support.h"
#include "demo_scene.h"
#include "imgui_debug_panel.h"
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
#include <filesystem>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
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

void cleanupApp(SDL_Window*& window,
                SDL_GLContext& glContext,
                Renderer2D& renderer,
                SidebarAssets& sidebarAssets,
                BuildingAssetCache& buildingAssetCache,
                RhinoUnitUiAssets& rhinoUiAssets) {
  shutdownImGuiDebugPanel();
  destroySidebarAssets(sidebarAssets);
  destroyBuildingAssetCache(buildingAssetCache);
  destroyRhinoUnitUiAssets(rhinoUiAssets);
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

void applyViewportState(SDL_Window* window, Renderer2D& renderer, const ViewportState& viewport) {
  SDL_SetWindowSize(window, viewport.width, viewport.height);
  renderer.setViewport(viewport.width, viewport.height);
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

    window = SDL_CreateWindow("RA2 Replica Demo - Allied Base",
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
    initializeImGuiDebugPanel(window, glContext);

    const auto paths = locateDemoAppPaths(std::filesystem::current_path());
    const auto artIni = ArtIni::load(paths.settingsRoot / "art.ini");
    const auto houseColors = loadHouseColors(paths.settingsRoot / "rules.ini");
    const auto voxelLighting = VplFile::load(paths.paletteRoot / "voxel" / "voxels.vpl");
    const auto paletteLibrary = loadPaletteLibrary(paths.paletteRoot);
    const auto sidebarPalette = Palette::load(paths.paletteRoot / "ui" / "sidebar" / "sidec01.pal");
    const auto cameoPalette = Palette::load(paths.paletteRoot / "ui" / "cameo.pal");
    const auto mousePalette = Palette::load(paths.paletteRoot / "ui" / "mousepal.pal");
    const auto overlayPalette = Palette::load(paths.paletteRoot / "ui" / "palette.pal");

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
    sidebarAssets = loadSidebarAssets(paths.sidebarUiRoot,
                                      paths.iconRoot,
                                      sidebarPalette,
                                      cameoPalette);
    rhinoUiAssets = loadRhinoUnitUiAssets(paths.unitOverlayRoot, overlayPalette);

    VplBoxRenderer rhinoRenderer;
    rhinoRenderer.initialize(window);
    rhinoRenderer.loadRhinoAssets(paths.rhinoRoot, voxelLighting);
    rhinoRenderer.setPalette(initialTheaterPalettes.unit);

    SidebarState sidebarState{};
    MapGrid map(64, 64);

    const auto& initialAsset = ensureCachedAssetForStyle(buildingAssetCache,
                                                         paths.buildingRoot,
                                                         artIni,
                                                         paletteLibrary,
                                                         houseColors,
                                                         debugPanelState.style,
                                                         "GAPOWR");
    const BuildingPlacement initialPlacement{
      {18, 14},
      initialAsset.foundation.width,
      initialAsset.foundation.height
    };
    if (!map.canPlace(foundationTiles(initialPlacement))) {
      throw std::runtime_error("Initial power plant placement is invalid");
    }

    std::vector<BuildingInstance> buildings{
      BuildingInstance{"GAPOWR", initialPlacement, BuildingState::Complete, 0}
    };
    map.setOccupied(foundationTiles(initialPlacement), true);

    RhinoUnitState rhinoTank;
    initializeRhinoUnit(rhinoTank, map, TileCoord{7, 10});
    const BuildingAsset* selectedBuildAsset = nullptr;
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
              const auto buildingId = buildingIdForIcon(sidebarClick.iconId);
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
              }
            }
            continue;
          }

          if (selectedBuildAsset != nullptr) {
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
                                     previewPlacement);
            continue;
          }

          const bool hitRhino = hitTestRhinoUnit(rhinoTank,
                                                 mouseX,
                                                 mouseY,
                                                 mapOrigin,
                                                 DemoAppConfig::kTileWidth,
                                                 DemoAppConfig::kTileHeight);
          if (hitRhino) {
            rhinoTank.selected = true;
            continue;
          }

          if (rhinoTank.selected) {
            const auto targetCell = screenToIso(mouseX,
                                                mouseY,
                                                mapOrigin,
                                                DemoAppConfig::kTileWidth,
                                                DemoAppConfig::kTileHeight);
            if (map.isBuildable(targetCell) ||
                (targetCell.x == rhinoTank.occupiedCell.x && targetCell.y == rhinoTank.occupiedCell.y)) {
              issueRhinoMoveCommand(rhinoTank, map, targetCell, nowTicks);
            }
            continue;
          }

          rhinoTank.selected = false;
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
            continue;
          }

          rhinoTank.selected = false;
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
                                                              "GAPOWR");
              previewPlacement = placementFromAnchor(previewPlacement.topLeft,
                                                     selectedBuildAsset->foundation);
              break;
            case SDLK_c:
              selectedBuildAsset = nullptr;
              break;
            default:
              break;
          }

          moveFirstBuilding(buildings, map, delta);
        }
      }

      updateRhinoUnit(rhinoTank, map, simulationFrame.deltaSeconds);

      beginImGuiDebugPanelFrame();
      const ImGuiDebugPanelState previousDebugPanelState = debugPanelState;
      const bool debugPanelChanged = drawImGuiDebugPanel(debugPanelState, houseColors);
      if (debugPanelChanged) {
        const bool styleChanged = !sameVisualStyle(previousDebugPanelState.style, debugPanelState.style);
        const bool resolutionSelectionChanged =
          previousDebugPanelState.display.resolutionIndex != debugPanelState.display.resolutionIndex;

        if (styleChanged) {
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
          if (selectedBuildingId.has_value()) {
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
          }
          renderer.setRemapColor(activeHouseColorValue(debugPanelState.style, houseColors));
        }

        if (resolutionSelectionChanged) {
          viewport = viewportStateForPanel(debugPanelState);
          applyViewportState(window, renderer, viewport);
          mapOrigin = mapOriginForViewport(viewport);
        }
      }

      renderer.beginFrame(0.58f, 0.78f, 0.97f, 1.0f);
      renderer.beginUiPass();
      drawInfiniteIsoGroundGrid(renderer,
                                mapOrigin,
                                DemoAppConfig::kTileWidth,
                                DemoAppConfig::kTileHeight,
                                static_cast<float>(viewport.width),
                                static_cast<float>(viewport.height));

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

      renderer.beginWorldPass();
      for (const auto& command : renderQueue) {
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
      }

      drawRhinoUnit(renderer,
                    rhinoRenderer,
                    rhinoUiAssets,
                    rhinoTank,
                    mapOrigin,
                    DemoAppConfig::kTileWidth,
                    DemoAppConfig::kTileHeight,
                    debugPanelState.rhinoPlacement.footprintK,
                    viewport.width,
                    viewport.height,
                    nowTicks,
                    buildRhinoTankRenderState(houseColors,
                                              debugPanelState,
                                              rhinoDirectionIndex(rhinoTank)));

      redrawBuildingOccludersForRhino(renderer,
                                      *activeBuildingAssets,
                                      buildings,
                                      rhinoTank,
                                      mapOrigin,
                                      DemoAppConfig::kTileWidth,
                                      DemoAppConfig::kTileHeight,
                                      viewport.width,
                                      viewport.height,
                                      debugPanelState.rhinoPlacement.footprintK,
                                      nowTicks);

      renderer.beginUiPass();
      drawSidebar(renderer,
                  static_cast<float>(viewport.width),
                  static_cast<float>(viewport.height),
                  sidebarAssets,
                  sidebarState);
      renderImGuiDebugPanel();
      SDL_GL_SwapWindow(window);
    }

    cleanupApp(window, glContext, renderer, sidebarAssets, buildingAssetCache, rhinoUiAssets);
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    cleanupApp(window, glContext, renderer, sidebarAssets, buildingAssetCache, rhinoUiAssets);
    return 1;
  }
}
