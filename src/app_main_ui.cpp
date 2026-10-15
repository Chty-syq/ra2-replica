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
#include "weapon_system.h"
#include "presentation/world_view.h"
#include "presentation/resource_view.h"
#include "interaction/unit_selection.h"
#include <imgui.h>

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {
struct ViewportState { int width = DemoAppConfig::kViewportWidth; int height = DemoAppConfig::kViewportHeight; };
struct VehicleRendererCache { std::unordered_map<std::string, std::unique_ptr<VplBoxRenderer>> renderers; };
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
void destroyVehicleRendererCache(VehicleRendererCache& cache) {
  for (auto& [id, renderer] : cache.renderers) { (void)id; renderer->destroy(); }
  cache.renderers.clear();
}
void setVehicleRendererCachePalette(VehicleRendererCache& cache, const Palette& palette) {
  for (auto& [id, renderer] : cache.renderers) { (void)id; renderer->setPalette(palette); }
}
VplBoxRenderer& vehicleRendererForType(const ra2::UnitTypeDef& type, VehicleRendererCache& cache,
                                      SDL_Window* window, const DemoAppPaths& paths,
                                      const VplFile& lighting, const Palette& palette) {
  if (const auto it = cache.renderers.find(type.id); it != cache.renderers.end()) return *it->second;
  if (type.visual.folder.empty() || type.visual.body.empty()) throw std::runtime_error("Missing visual mapping: " + type.id);
  auto renderer = std::make_unique<VplBoxRenderer>();
  renderer->initialize(window);
  renderer->loadVehicleAssets(paths.vehicleRoot / type.visual.folder, lighting, type.visual.body, type.visual.turret, type.visual.barrel);
  renderer->setPalette(palette);
  return *cache.renderers.emplace(type.id, std::move(renderer)).first->second;
}
void cleanupApp(SDL_Window*& window,
                SDL_GLContext& glContext,
                Renderer2D& renderer,
                IsoGridRenderer& gridRenderer,
                VplBoxRenderer& rhinoRenderer,
                VplBoxRenderer& grizzlyRenderer,
                VehicleRendererCache& vehicleRendererCache,
                VplBoxRenderer& grandCannonRenderer,
                VplBoxRenderer& mcvRenderer,
                SidebarAssets& sidebarAssets,
                BuildingAssetCache& buildingAssetCache,
                RhinoUnitUiAssets& rhinoUiAssets,
                WeaponVisualAssets& weaponVisualAssets,
                ra2::ResourceVisualAssets& resourceVisualAssets) {
  shutdownImGuiDebugPanel();
  ra2::destroyResourceVisualAssets(resourceVisualAssets);
  destroyWeaponVisualAssets(weaponVisualAssets);
  destroySidebarAssets(sidebarAssets);
  destroyBuildingAssetCache(buildingAssetCache);
  destroyRhinoUnitUiAssets(rhinoUiAssets);
  mcvRenderer.destroy();
  grandCannonRenderer.destroy();
  destroyVehicleRendererCache(vehicleRendererCache);
  grizzlyRenderer.destroy();
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

[[nodiscard]] const char* mcvUnitIdForFaction(const BuildFaction faction) {
  switch (faction) {
    case BuildFaction::Allied: return "AMCV";
    case BuildFaction::Soviet: return "SMCV";
  }
  return "AMCV";
}

[[nodiscard]] bool isLayeredWarFactoryInstance(const BuildingInstance& building) {
  return building.layeredFactory &&
         building.state == BuildingState::Complete;
}

[[nodiscard]] bool isCompleteGrandCannon(const BuildingInstance& building) {
  return building.assetId == "GTGCAN" && building.state == BuildingState::Complete;
}

[[nodiscard]] float normalizeAngleRadians(float radians) {
  while (radians < 0.0f) {
    radians += kTwoPi;
  }
  while (radians >= kTwoPi) {
    radians -= kTwoPi;
  }
  return radians;
}

[[nodiscard]] float shortestAngleDelta(const float fromRadians, const float toRadians) {
  float delta = normalizeAngleRadians(toRadians) - normalizeAngleRadians(fromRadians);
  if (delta > kPi) {
    delta -= kTwoPi;
  } else if (delta < -kPi) {
    delta += kTwoPi;
  }
  return delta;
}

[[nodiscard]] Vec2 grandCannonTurretAnchor(const BuildingInstance& building,
                                           const ra2::IniDocument& rules,
                                           const Vec2 mapOrigin,
                                           const float tileWidth,
                                           const float tileHeight) {
  const auto center = buildingDrawCenter(building.placement, mapOrigin, tileWidth, tileHeight);
  return {center.x + rules.integer(building.assetId, "TurretAnimX"),
          center.y + rules.integer(building.assetId, "TurretAnimY")};
}

[[nodiscard]] VplBoxRendererState grandCannonRenderState(const HouseColorSet& houseColors,
                                                         const ImGuiDebugPanelState& debugPanelState,
                                                         const BuildingInstance& building) {
  constexpr float kTurretAnimZAdjust = -60.0f;
  constexpr float kRulesLeptonToVoxelWorldUnit = 30.0f * 1.41421356237f / 256.0f;

  auto state = buildRhinoTankRenderState(houseColors,
                                         debugPanelState,
                                         building.turretHeadingRadians);
  state.bodyRotationDegrees = 0.0f;
  state.turretRotationDegrees = 0.0f;
  state.turretOffsetPixels = 0.0f;
  // rules.ini 没有给建筑炮塔 VXL 单独的缩放参数，尺寸应使用 VXL/HVA 自身变换。
  state.modelOffset[2] = -kTurretAnimZAdjust * kRulesLeptonToVoxelWorldUnit;
  state.shadowAlpha = 0.0f;
  return state;
}

[[nodiscard]] float grandCannonTurretDepth(const BuildingPlacement& placement,
                                           const float tileWidth,
                                           const float tileHeight) {
  constexpr float kTurretDepthBias01 = 0.00008f;

  float frontMostDepth = 0.999f;
  for (const auto tile : foundationTiles(placement)) {
    const auto tileDepth = depthFromLogicalCenter(Vec2{static_cast<float>(tile.x),
                                                       static_cast<float>(tile.y)},
                                                 tileWidth,
                                                 tileHeight);
    frontMostDepth = std::min(frontMostDepth, tileDepth);
  }
  return std::clamp(frontMostDepth - kTurretDepthBias01, 0.001f, 0.999f);
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

[[nodiscard]] bool drawnWithFactory(const RhinoUnitState& vehicle, const std::vector<BuildingInstance>& buildings) {
  return std::any_of(buildings.begin(), buildings.end(), [&](const auto& factory) {
    return ra2::isFactoryExitVehicle(vehicle, factory);
  });
}

[[nodiscard]] bool isStopCommandEvent(const SDL_Event& event) {
  return event.type == SDL_KEYDOWN &&
         (event.key.keysym.sym == SDLK_s || event.key.keysym.scancode == SDL_SCANCODE_S);
}

[[nodiscard]] bool isCtrlModifierDown() {
  int keyCount = 0;
  const auto* keyboardState = SDL_GetKeyboardState(&keyCount);
  const bool scancodeCtrl =
    keyboardState != nullptr &&
    keyCount > SDL_SCANCODE_RCTRL &&
    (keyboardState[SDL_SCANCODE_LCTRL] != 0 || keyboardState[SDL_SCANCODE_RCTRL] != 0);
  return (SDL_GetModState() & KMOD_CTRL) != 0 || scancodeCtrl;
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

void applyViewportState(SDL_Window* window,
                        Renderer2D& renderer,
                        IsoGridRenderer& gridRenderer,
                        const ViewportState& viewport) {
  SDL_SetWindowSize(window, viewport.width, viewport.height);
  renderer.setViewport(viewport.width, viewport.height);
  gridRenderer.setViewport(viewport.width, viewport.height);
}

// Hidden-window regression: compare rendered source pixels, not just enum values.
// This catches palette routing, accidental house remaps and the old 0.82 dimming.
void verifyMineralPaletteRendering(Renderer2D& renderer, const ra2::ResourceVisualAssets& assets,
                                   const PaletteLibrary& library, const Palette& animationPalette,
                                   const std::filesystem::path& assetRoot, int viewportHeight) {
  constexpr std::array<const char*,3> directories{"temperate","snow","urban"};
  constexpr std::array<const char*,3> extensions{".tem",".sno",".urb"};
  constexpr std::array<const char*,3> palettes{"temperat.pal","snow.pal","urban.pal"};
  for (std::size_t theater=0;theater<directories.size();++theater) {
    const auto& active=palettesForTheater(library,static_cast<TheaterStyle>(theater));
    renderer.setIndexedPalettes(active.unit,active.terrain);
    renderer.setResourcePalettes(active.resource,animationPalette);
    renderer.setRemapColor({0,255,255,255});
    const auto expectedResource=Palette::load(assetRoot/"palettes"/"resources"/palettes[theater]);
    for(int kind=0;kind<3;++kind) {
      const bool effect=kind==2;
      const auto source=effect ? assetRoot/"effects"/"harvesting"/"oregath.shp" :
        assetRoot/"terrain"/"resources"/directories[theater]/(std::string(kind==0?"tib01":"gem01")+extensions[theater]);
      const auto frame=ShpTsFile::load(source).decodeFrame(effect?6:11);
      const auto& texture=effect ? assets.gathering : assets.overlays[theater][kind];
      const auto& palette=effect ? animationPalette : expectedResource;
      const float u0=effect?6.f/15:11.f/12, u1=effect?7.f/15:1.f, v1=effect?1.f/8:1.f/12;
      const float w=frame.width,h=frame.height;
      const std::array<RenderVertex,6> quad{{{0,0,0,u0,0},{w,0,0,u1,0},{w,h,0,u1,v1},
                                            {0,0,0,u0,0},{w,h,0,u1,v1},{0,h,0,u0,v1}}};
      renderer.beginFrame(0,0,0,1);
      renderer.beginUiPass();
      renderer.draw(GL_TRIANGLES,texture,quad.data(),quad.size());
      std::vector<std::uint8_t> pixels(frame.width*frame.height*4);
      glReadPixels(0,viewportHeight-frame.height,frame.width,frame.height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
      for(int y=0;y<frame.height;++y) for(int x=0;x<frame.width;++x) {
        const auto index=frame.indices[y*frame.width+x];
        const auto expected=index==0?Rgba{0,0,0,255}:palette.color(index);
        const auto offset=((frame.height-1-y)*frame.width+x)*4;
        if(std::abs(int(pixels[offset])-expected.r)>1 || std::abs(int(pixels[offset+1])-expected.g)>1 ||
           std::abs(int(pixels[offset+2])-expected.b)>1)
          throw std::runtime_error("Mineral palette pixel mismatch: "+source.string()+" index="+std::to_string(index));
      }
    }
  }
  std::cout<<"Mineral palette GPU readback passed for ore, gems and gathering in all 3 theaters\n";
}

} // namespace

int main(const int argc, char** argv) {
  const bool simulationSmokeTest = argc >= 2 && std::string_view(argv[1]) == "--simulation-smoke-test";
  const bool gameSpeedSmokeTest = argc == 2 && std::string_view(argv[1]) == "--game-speed-smoke-test";
  const bool buildingSmokeTest = argc >= 2 && std::string_view(argv[1]) == "--building-smoke-test";
  const bool miningSmokeTest = argc >= 2 && std::string_view(argv[1]) == "--mining-smoke-test";
  const bool tankSmokeTest = argc >= 2 && std::string_view(argv[1]) == "--tank-smoke-test";
  const bool selectionSmokeTest = argc >= 2 && std::string_view(argv[1]) == "--selection-smoke-test";
  const bool smokeTest = selectionSmokeTest || tankSmokeTest || miningSmokeTest || simulationSmokeTest || gameSpeedSmokeTest || buildingSmokeTest || (argc == 2 && std::string_view(argv[1]) == "--smoke-test");
  SDL_Window* window = nullptr;
  SDL_GLContext glContext = nullptr;
  Renderer2D renderer;
  IsoGridRenderer gridRenderer;
  VplBoxRenderer rhinoRenderer;
  VplBoxRenderer grizzlyRenderer;
  VehicleRendererCache vehicleRendererCache;
  VplBoxRenderer grandCannonRenderer;
  VplBoxRenderer mcvRenderer;
  SidebarAssets sidebarAssets;
  BuildingAssetCache buildingAssetCache;
  RhinoUnitUiAssets rhinoUiAssets;
  WeaponVisualAssets weaponVisualAssets;
  ra2::ResourceVisualAssets resourceVisualAssets;

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
                              SDL_WINDOW_OPENGL | (smokeTest ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN));
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
    const auto definitions = ra2::DefinitionRegistry::load(paths.settingsRoot);
    const auto artIni = ArtIni::fromDocument(definitions.art(), definitions.replica());
    const auto houseColors = loadHouseColors(definitions.rules());
    const auto voxelLighting = VplFile::load(paths.paletteRoot / "voxel" / "voxels.vpl");
    const auto paletteLibrary = loadPaletteLibrary(paths.paletteRoot);
    const auto cameoPalette = Palette::load(paths.paletteRoot / "ui" / "cameo.pal");
    const auto mousePalette = Palette::load(paths.paletteRoot / "ui" / "mousepal.pal");
    const auto overlayPalette = Palette::load(paths.paletteRoot / "ui" / "palette.pal");
    const auto animationPalette = Palette::load(paths.paletteRoot / "effects" / "anim.pal");
    (void)mousePalette;

    debugPanelState.style.houseColorIndex = houseColors.defaultIndex;
    ViewportState viewport = viewportStateForPanel(debugPanelState);

    const auto& initialTheaterPalettes = palettesForTheater(paletteLibrary, debugPanelState.style.theater);
    renderer.setIndexedPalettes(initialTheaterPalettes.unit, initialTheaterPalettes.terrain);
    renderer.setResourcePalettes(initialTheaterPalettes.resource, animationPalette);
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
    weaponVisualAssets = loadWeaponVisualAssets(paths.effectsRoot, animationPalette, &definitions);
    ra2::loadResourceVisualAssets(resourceVisualAssets, paths.projectRoot / "assets");
    if(miningSmokeTest) {
      verifyMineralPaletteRendering(renderer,resourceVisualAssets,paletteLibrary,animationPalette,
                                     paths.projectRoot/"assets",viewport.height);
      renderer.setIndexedPalettes(initialTheaterPalettes.unit,initialTheaterPalettes.terrain);
      renderer.setResourcePalettes(initialTheaterPalettes.resource,animationPalette);
      renderer.setRemapColor(activeHouseColorValue(debugPanelState.style,houseColors));
    }

    grandCannonRenderer.initialize(window);
    grandCannonRenderer.loadTurretAssets(paths.buildingRoot / "allied" / "gtgcan", voxelLighting, "gtgcantur", "gtgcanbarl");
    grandCannonRenderer.setPalette(initialTheaterPalettes.unit);

    ra2::World world(definitions);
    debugPanelState.gameplay.speed = world.gameSpeed();
    const auto initialBuildingId = defaultPowerPlantId(debugPanelState.style.faction);
    if (!world.spawn(initialBuildingId, {18,14}) || !world.spawn("HTNK", {7,10}) ||
        !world.spawn(mcvUnitIdForFaction(debugPanelState.style.faction), {10,14}) || !world.spawn("MTNK", {12,10}, 1))
      throw std::runtime_error("Initial scenario cannot be created");
    const auto selectionSmokeUnit=selectionSmokeTest?world.spawn("MTNK",{8,10}):0;
    if(selectionSmokeTest && !selectionSmokeUnit) throw std::runtime_error("Selection smoke unit placement failed");
    if (!smokeTest || miningSmokeTest) {
      if (!world.spawn("GAREFN",{5,4}) || !world.spawn("NAREFN",{14,6}))
        throw std::runtime_error("Mining scenario placement failed");
      // Dense centers and sparse, irregular edges make depletion readable.
      for(int x=2;x<=7;++x) for(int y=11;y<=15;++y) {
        const int dx=2*x-9, dy=2*y-26;
        const int edge=dx*dx+2*dy*dy;
        if(edge<=30) world.setResource({x,y},"Riparius",std::clamp(38-edge,6,32));
      }
      for(int x=12;x<=16;++x) for(int y=12;y<=15;++y) {
        const int dx=2*x-28, dy=2*y-27;
        const int edge=dx*dx+2*dy*dy;
        if(edge<=22) world.setResource({x,y},"Cruentus",std::clamp(28-edge,4,24));
      }
    }
    const auto& map = world.map();
    const BuildingPlacement initialPlacement{{18,14}, definitions.unit(initialBuildingId).foundationWidth, definitions.unit(initialBuildingId).foundationHeight};
    SidebarState sidebarState{};
    const BuildingAsset* selectedBuildAsset = nullptr;
    BuildingPlacement previewPlacement = initialPlacement;
    ra2::UnitSelection selection;
    ra2::SelectionGesture selectionGesture;
    std::uint64_t sequence = 0;
    std::vector<BuildingRenderCommand> renderQueue;
    auto presentation = ra2::makeWorldView(world, definitions, selection.ids());
    auto lastCounter = SDL_GetPerformanceCounter();
    double fractionalMicroseconds = 0;
    std::uint64_t nextUnlimitedRenderCounter = 0;
    bool swapIntervalEnabled = true;
    double metricsMicroseconds = 0, measuredGameFps = 0;
    ra2::Tick metricsStartFrame = 0;
    std::string interactionMessage;
    auto send = [&](ra2::CommandKind kind, ra2::EntityId entity = 0, TileCoord cell = {}, std::string type = {}, ra2::EntityId target = 0) {
      if (!world.enqueue({world.tick() + 1, 0, ++sequence, kind, entity, target, cell, std::move(type)})) interactionMessage = "Command rejected";
    };
    auto sendSelection = [&](ra2::CommandKind kind, TileCoord cell = {}, ra2::EntityId target = 0) {
      return ra2::issueSelectionCommand(world,definitions,selection,kind,cell,target,sequence);
    };
    ra2::EntityId testMcv = 0;
    ra2::EntityId testSovietMcv = 0;
    if (tankSmokeTest) {
      selection.selectOne(2);
      send(ra2::CommandKind::Move,2,{11,12});
      world.enqueue({1,1,1,ra2::CommandKind::Move,4,0,{12,13}});
    }
    if (buildingSmokeTest) {
      testMcv = 3;
      testSovietMcv = world.spawn("SMCV", {5,5});
      if (!testSovietMcv) throw std::runtime_error("Building smoke scenario placement failed");
      selection.selectOne(1);
      send(ra2::CommandKind::Deploy,testMcv);
      send(ra2::CommandKind::Deploy,testSovietMcv);
      world.enqueue({101,0,++sequence,ra2::CommandKind::Deploy,testMcv});
      world.enqueue({101,0,++sequence,ra2::CommandKind::Deploy,testSovietMcv});
    }
    if (simulationSmokeTest) {
      const auto alliedFactory = world.spawn("GAWEAP", {10,4});
      const auto sovietFactory = world.spawn("NAWEAP", {1,4});
      testMcv = world.spawn("AMCV", {5,0});
      if (!alliedFactory || !sovietFactory || !testMcv) throw std::runtime_error("Smoke scenario placement failed");
      send(ra2::CommandKind::Deploy, testMcv);
      send(ra2::CommandKind::Produce, alliedFactory, {}, "AMCV");
      send(ra2::CommandKind::Produce, sovietFactory, {}, "HTNK");
      send(ra2::CommandKind::AttackEntity, 2, {12,10}, {}, 4);
      send(ra2::CommandKind::PlaceBuilding, 0, {0,10}, "GAPILE");
    }
    constexpr std::array<ra2::GameSpeed,6> smokeSpeeds{{
      {ra2::GameSpeedMode::Multiplayer,0}, {ra2::GameSpeedMode::Multiplayer,5},
      {ra2::GameSpeedMode::Singleplayer,5}, {ra2::GameSpeedMode::Singleplayer,6},
      {ra2::GameSpeedMode::Singleplayer,6}, {ra2::GameSpeedMode::Multiplayer,6}}};
    ra2::Tick speedSmokePreviousFrame = 0;
    if (gameSpeedSmokeTest) {
      debugPanelState.gameplay.speed = smokeSpeeds.front();
      world.setGameSpeed(smokeSpeeds.front());
    }
    bool running = true;
    int smokeTestFrames = 0;
    bool sawInternalExit = false, sawCombatDamage = false;
    bool sawMiningCargo = false, sawMiningReturn = false;
    bool sawMovingTurret = false, sawShell = false, sawExplosion = false;
    std::array<bool,2> sawBuildup{}, sawComplete{}, sawPacking{};
    while (running) {
      const auto counter = SDL_GetPerformanceCounter();
      const double elapsed = static_cast<double>(counter - lastCounter) * 1000000.0 / SDL_GetPerformanceFrequency();
      lastCounter = counter;
      fractionalMicroseconds += elapsed;
      const auto wholeMicroseconds = static_cast<std::uint64_t>(fractionalMicroseconds);
      fractionalMicroseconds -= static_cast<double>(wholeMicroseconds);
      const bool unlimited = !ra2::targetGameFrameRate(world.gameSpeed()) && !simulationSmokeTest && !miningSmokeTest && !tankSmokeTest && !buildingSmokeTest && !selectionSmokeTest;
      if (unlimited == swapIntervalEnabled) {
        SDL_GL_SetSwapInterval(unlimited ? 0 : 1);
        swapIntervalEnabled = !unlimited;
        nextUnlimitedRenderCounter = counter;
      }
      if (buildingSmokeTest || tankSmokeTest || selectionSmokeTest) world.step();
      else if (simulationSmokeTest || miningSmokeTest) world.advance(1000000);
      else world.advance(gameSpeedSmokeTest ? 100000 : wholeMicroseconds, unlimited ? 8 : 120);
      metricsMicroseconds += (simulationSmokeTest || miningSmokeTest) ? 1000000 : elapsed;
      if (metricsMicroseconds >= 500000) {
        measuredGameFps = (world.tick() - metricsStartFrame) * 1000000.0 / metricsMicroseconds;
        metricsStartFrame = world.tick(); metricsMicroseconds = 0;
      }
      // In unlimited mode, spend available CPU time on small simulation batches.
      // Only rendering/input uses a 60 Hz deadline; there is no logic FPS cap.
      if (unlimited && counter < nextUnlimitedRenderCounter) continue;
      if (unlimited) nextUnlimitedRenderCounter = counter + SDL_GetPerformanceFrequency() / 60;
      const auto nowTicks = ra2::gameMilliseconds(world.tick());
      if (buildingSmokeTest && world.tick()==2) selection.selectOne(testMcv);
      selection.prune(world.snapshot());
      Vec2 mapOrigin = mapOriginForViewport(viewport);
      presentation = ra2::makeWorldView(world, definitions, selection.ids());
      const auto& buildings = presentation.buildings;
      const auto& rhinoTanks = presentation.vehicles;
      const auto& projectiles = presentation.projectiles;
      const auto& combatEffects = presentation.effects;
      if (tankSmokeTest) {
        for (const auto& entity:world.snapshot())
          sawMovingTurret=sawMovingTurret || (entity.position!=entity.previous && entity.turretHeading!=entity.heading);
        sawShell=sawShell || !projectiles.empty();
        for (const auto& effect:combatEffects) sawExplosion=sawExplosion || effect.kind==CombatEffectKind::Explosion;
      }
      if (miningSmokeTest) for(const auto& entity:world.snapshot()) if(entity.harvesting) {
        if (!selection.primary()) selection.selectOne(entity.id);
        sawMiningCargo=sawMiningCargo || entity.harvesting->cargo>0;
        sawMiningReturn=sawMiningReturn || entity.harvesting->phase==ra2::HarvestPhase::Unloading;
      }
      if (simulationSmokeTest) for (const auto& entity : world.snapshot())
        if (entity.id == 4 && entity.hp < entity.maxHp) sawCombatDamage = true;
      if (simulationSmokeTest) for (const auto& vehicle : rhinoTanks) {
        if (!vehicle.exitFactoryId) continue;
        const auto parent = std::find_if(buildings.begin(), buildings.end(), [&](const auto& b) { return b.entityId == vehicle.exitFactoryId; });
        if (parent == buildings.end() || !ra2::isFactoryExitVehicle(vehicle, *parent))
          throw std::runtime_error("Factory presentation link lost");
        if (world.structureAt(vehicle.occupiedCell) == vehicle.exitFactoryId) sawInternalExit = true;
      }
      for (const auto& building : buildings) {
        (void)ensureCachedAssetForStyle(buildingAssetCache, paths.buildingRoot, artIni, paletteLibrary, houseColors, debugPanelState.style, building.assetId);
        if (buildingSmokeTest && (building.entityId==testMcv || building.entityId==testSovietMcv)) {
          const auto index = building.entityId==testMcv ? 0 : 1;
          if (activeBuildingAssets->at(building.assetId).buildupTextures.empty()) throw std::runtime_error("Missing construction yard buildup SHP");
          sawBuildup[index] = sawBuildup[index] || building.state==BuildingState::Constructing;
          sawComplete[index] = sawComplete[index] || building.state==BuildingState::Complete;
          sawPacking[index] = sawPacking[index] || building.state==BuildingState::Packing;
        }
      }
      updateSidebarStateForDemo(sidebarState, sidebarAssets, viewport.height, nowTicks);
      const auto clampWorldPoint=[&](float x,float y) {
        return Vec2{std::clamp(x,0.0f,float(viewport.width)-DemoAppConfig::kSidebarWidth-1),
                    std::clamp(y,0.0f,float(viewport.height)-1)};
      };
      if(selectionSmokeTest) {
        const auto origin=mapOriginForViewport(viewport);
        const Vec2 first{origin.x+(7-10)*30,origin.y+(7+10)*15};
        const Vec2 second{origin.x+(8-10)*30,origin.y+(8+10)*15};
        const Vec2 start{first.x-30,first.y-35},end{second.x+30,second.y+25};
        const Vec2 goal{origin.x+(10-8)*30,origin.y+(10+8)*15};
        const auto mouseEvent=[&](Uint32 type,Vec2 point) {
          SDL_Event input{}; input.type=type;
          if(type==SDL_MOUSEMOTION) { input.motion.x=int(point.x); input.motion.y=int(point.y); input.motion.state=SDL_BUTTON_LMASK; }
          else { input.button.button=SDL_BUTTON_LEFT; input.button.state=type==SDL_MOUSEBUTTONDOWN?SDL_PRESSED:SDL_RELEASED;
                 input.button.x=int(point.x); input.button.y=int(point.y); }
          if(SDL_PushEvent(&input)<0) throw std::runtime_error(SDL_GetError());
        };
        if(world.tick()==1) mouseEvent(SDL_MOUSEBUTTONDOWN,start);
        if(world.tick()==2) mouseEvent(SDL_MOUSEMOTION,end);
        if(world.tick()==3) mouseEvent(SDL_MOUSEBUTTONUP,end);
        if(world.tick()==4) mouseEvent(SDL_MOUSEBUTTONDOWN,goal);
        if(world.tick()==5) mouseEvent(SDL_MOUSEBUTTONUP,goal);
      }
      SDL_Event event{};
      while (SDL_PollEvent(&event)) {
        processImGuiDebugPanelEvent(event);
        if (event.type == SDL_QUIT) { running = false; continue; }
        if (event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST) {
          selectionGesture.cancel(); SDL_CaptureMouse(SDL_FALSE); continue;
        }
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) { running = false; continue; }
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_d && (event.key.keysym.mod & KMOD_CTRL)) { debugPanelState.visible = !debugPanelState.visible; continue; }
        if (event.type == SDL_KEYDOWN && imguiDebugPanelWantsKeyboard()) continue;
        if (isStopCommandEvent(event)) { sendSelection(ra2::CommandKind::Stop); continue; }
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_h) {
          for(const auto& entity:world.snapshot()) if(selection.contains(entity.id) && entity.owner==0 && entity.harvesting)
            send(ra2::CommandKind::Harvest,entity.id,entity.cell);
          continue;
        }
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_d) { sendSelection(ra2::CommandKind::Deploy); continue; }
        if (event.type == SDL_MOUSEMOTION) {
          selectionGesture.update(clampWorldPoint(float(event.motion.x),float(event.motion.y)));
          if (selectedBuildAsset) updatePreviewPlacementFromMouse(static_cast<float>(event.motion.x), static_cast<float>(event.motion.y),
              static_cast<float>(viewport.width), DemoAppConfig::kSidebarWidth,
              mapOrigin, DemoAppConfig::kTileWidth, DemoAppConfig::kTileHeight, selectedBuildAsset, previewPlacement);
          continue;
        }
        if (event.type!=SDL_MOUSEBUTTONDOWN && event.type!=SDL_MOUSEBUTTONUP) continue;
        const auto mouseX = static_cast<float>(event.button.x), mouseY = static_cast<float>(event.button.y);
        if (event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_RIGHT) {
          selectionGesture.cancel(); SDL_CaptureMouse(SDL_FALSE);
          if (!imguiDebugPanelContainsPoint(debugPanelState,mouseX,mouseY) && !imguiDebugPanelWantsMouse()) {
            selectedBuildAsset=nullptr; selection.clear();
          }
          continue;
        }
        if (event.button.button != SDL_BUTTON_LEFT) continue;
        std::optional<ra2::SelectionRelease> release;
        if (event.type==SDL_MOUSEBUTTONUP) {
          release=selectionGesture.release(clampWorldPoint(mouseX,mouseY));
          SDL_CaptureMouse(SDL_FALSE);
          if (!release) continue;
          if (release->dragged) {
            selection.selectBox(release->rectangle,rhinoTanks,mapOrigin,DemoAppConfig::kTileWidth,
                                DemoAppConfig::kTileHeight,release->additive);
            continue;
          }
        }
        if (imguiDebugPanelContainsPoint(debugPanelState, mouseX, mouseY) || imguiDebugPanelWantsMouse()) continue;
        if (event.type==SDL_MOUSEBUTTONDOWN) {
          const auto click = handleSidebarLeftClick(sidebarState, sidebarAssets, static_cast<float>(viewport.width), static_cast<float>(viewport.height), mouseX, mouseY);
          if (click.consumed) {
            applySidebarClick(sidebarState, sidebarAssets, click);
            if (click.action != SidebarClickAction::ClickIcon) continue;
            if (const auto buildingId = buildingIdForIcon(click.iconId, debugPanelState.style.faction)) {
              selectedBuildAsset = &ensureCachedAssetForStyle(buildingAssetCache, paths.buildingRoot, artIni, paletteLibrary, houseColors, debugPanelState.style, *buildingId);
              previewPlacement = placementFromAnchor(previewPlacement.topLeft, selectedBuildAsset->foundation);
              selection.clear();
            } else {
              const auto faction = debugPanelState.style.faction == BuildFaction::Allied ? "ALLIED" : "SOVIET";
              const ra2::UnitTypeDef* requested = nullptr;
              for (const auto& [id, type] : definitions.units()) {
                (void)id;
                if (ra2::canonicalId(type.visual.cameo) == ra2::canonicalId(click.iconId) && ra2::canonicalId(type.visual.faction) == faction) { requested = &type; break; }
              }
              if (!requested) continue;
              if (!requested->unsupported.empty()) { interactionMessage = requested->id + ": unsupported " + requested->unsupported.front(); continue; }
              ra2::EntityId factory = 0;
              for (const auto& entity : world.snapshot()) {
                const auto& type = definitions.unit(entity.type);
                if (entity.owner == 0 && type.factory == "UNITTYPE" && type.naval == requested->naval &&
                    entity.activity == ra2::Activity::Idle && std::any_of(requested->owners.begin(), requested->owners.end(),
                      [&](const auto& owner) { return std::find(type.owners.begin(), type.owners.end(), owner) != type.owners.end(); })) {
                  if (!factory || entity.id == selection.primary()) factory = entity.id;
                }
              }
              if (factory) { send(ra2::CommandKind::Produce, factory, {}, requested->id); selection.selectOne(factory); selectedBuildAsset = nullptr; interactionMessage.clear(); }
              else interactionMessage = "Place a compatible war factory first";
            }
            continue;
          }
          const auto cell = screenToIso(mouseX, mouseY, mapOrigin, DemoAppConfig::kTileWidth, DemoAppConfig::kTileHeight);
          if (selectedBuildAsset) {
            selectionGesture.cancel();
            previewPlacement = placementFromAnchor(cell, selectedBuildAsset->foundation);
            if (map.canPlace(foundationTiles(previewPlacement))) { send(ra2::CommandKind::PlaceBuilding, 0, cell, selectedBuildAsset->id); selectedBuildAsset = nullptr; }
            else interactionMessage = "Placement blocked";
            continue;
          }
          if (mouseX<0 || mouseY<0 || mouseX>=float(viewport.width)-DemoAppConfig::kSidebarWidth || mouseY>=viewport.height) continue;
          if (isCtrlModifierDown() && selection.primary()) { sendSelection(ra2::CommandKind::AttackGround,cell); continue; }
          selectionGesture.begin({mouseX,mouseY},(SDL_GetModState()&KMOD_SHIFT)!=0);
          SDL_CaptureMouse(SDL_TRUE);
          continue;
        }
        if (mouseX<0 || mouseY<0 || mouseX>=float(viewport.width)-DemoAppConfig::kSidebarWidth || mouseY>=viewport.height) continue;
        const auto cell = screenToIso(mouseX, mouseY, mapOrigin, DemoAppConfig::kTileWidth, DemoAppConfig::kTileHeight);
        ra2::EntityId hit = 0;
        if (const auto vehicle = hitTestRhinoTanks(rhinoTanks, mouseX, mouseY, mapOrigin, DemoAppConfig::kTileWidth, DemoAppConfig::kTileHeight)) hit = rhinoTanks[*vehicle].entityId;
        else if (const auto building = hitTestBuildingAtPoint(*activeBuildingAssets, buildings, mouseX, mouseY, mapOrigin, DemoAppConfig::kTileWidth, DemoAppConfig::kTileHeight)) hit = presentation.buildingIds[*building];
        const auto entities = world.snapshot();
        if (release->additive) {
          selection.toggleVehicle(hit,rhinoTanks);
          continue;
        }
        const auto selectedView=std::find_if(entities.begin(),entities.end(),[&](const auto& item){return item.id==selection.primary() && item.owner==0;});
        if (selection.size()==1 && selectedView!=entities.end() && selectedView->harvesting && !release->additive) {
          const auto target=std::find_if(entities.begin(),entities.end(),[&](const auto& item){return item.id==hit;});
          if (target!=entities.end() && target->owner==0 && definitions.unit(target->type).refinery) {
            send(ra2::CommandKind::ReturnToRefinery,selection.primary(),cell,{},hit); continue;
          }
          if ((!hit || hit==selection.primary()) && world.resourceAt(cell)) { send(ra2::CommandKind::Harvest,selection.primary(),cell); continue; }
        }
        if (hit) {
          const auto found = std::find_if(entities.begin(), entities.end(), [&](const auto& item) { return item.id == hit; });
          if (found != entities.end() && found->owner != 0) {
            if (sendSelection(ra2::CommandKind::AttackEntity,cell,hit)==0) selection.selectOne(hit);
          }
          else if (found != entities.end() && selection.size()==1 && selection.primary() == hit &&
                   (!definitions.unit(found->type).deploysInto.empty() || !definitions.unit(found->type).undeploysInto.empty()))
            send(ra2::CommandKind::Deploy, selection.primary());
          else selection.selectOne(hit);
        } else if (selection.primary()) sendSelection(ra2::CommandKind::Move,cell);
      }

      presentation=ra2::makeWorldView(world,definitions,selection.ids());

      std::optional<std::size_t> selectedBuildingIndex;
      for (std::size_t i = 0; i < presentation.buildingIds.size(); ++i) if (presentation.buildingIds[i] == selection.primary()) selectedBuildingIndex = i;
      beginImGuiDebugPanelFrame();
      const ImGuiDebugPanelState previousDebugPanelState = debugPanelState;
      bool debugPanelChanged = drawImGuiDebugPanel(debugPanelState, houseColors);
      if (miningSmokeTest && (smokeTestFrames==15 || smokeTestFrames==30)) {
        debugPanelState.style.theater = smokeTestFrames==15 ? TheaterStyle::Snow : TheaterStyle::Urban;
        debugPanelChanged = true;
      }
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
            (void)ensureCachedAssetForStyle(buildingAssetCache,
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
            renderer.setResourcePalettes(currentTheaterPalettes.resource, animationPalette);
            setVehicleRendererCachePalette(vehicleRendererCache, currentTheaterPalettes.unit);
            grandCannonRenderer.setPalette(currentTheaterPalettes.unit);
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
        imguiDebugPanelWantsMouse();
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
      ra2::drawResources(renderer,resourceVisualAssets,debugPanelState.style.theater,
                         world,definitions,mapOrigin,DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight);

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

      const auto& drawTheaterPalettes = palettesForTheater(paletteLibrary, debugPanelState.style.theater);
      auto drawRhinoTankInstance = [&](const RhinoUnitState& tank) {
        VplBoxRenderer& vehicleRenderer = vehicleRendererForType(definitions.unit(tank.typeId), vehicleRendererCache, window, paths, voxelLighting, drawTheaterPalettes.unit);
        auto unitDebug = debugPanelState;
        if (tank.owner != 0) {
          unitDebug.style.houseColorIndex = (unitDebug.style.houseColorIndex + 1) % houseColors.colors.size();
          for (std::size_t i = 0; i < houseColors.colors.size(); ++i) if (ra2::canonicalId(houseColors.colors[i].name) == "RED") unitDebug.style.houseColorIndex = i;
        }
        auto renderState = buildRhinoTankRenderState(houseColors,
                                                     unitDebug,
                                                     tank.headingRadians);
        applyRhinoSurfaceTilt(renderState,tank.pitchDegrees,tank.rollDegrees);
        {
          renderState.turretRotationDegrees =
            shortestAngleDelta(tank.headingRadians, tank.turretHeadingRadians) * 180.0f / kPi;
        }
        drawRhinoUnit(renderer,
                      vehicleRenderer,
                      rhinoUiAssets,
                      tank,
                      mapOrigin,
                      DemoAppConfig::kTileWidth,
                      DemoAppConfig::kTileHeight,
                      debugPanelState.rhinoPlacement.footprintK,
                      viewport.width,
                      viewport.height,
                      nowTicks,
                      renderState);
      };

      renderer.beginWorldPass();
      for (const auto& command : renderQueue) {
        const bool layeredWarFactory = isLayeredWarFactoryInstance(command.instance);
        const bool productionSplit =
          layeredWarFactory && command.instance.productionSplit && command.instance.factoryExitActive;

        if (productionSplit) {
          drawWarFactoryProductionUnderUnitLayers(renderer,
                                                  *command.asset,
                                                  command.instance,
                                                  mapOrigin,
                                                  DemoAppConfig::kTileWidth,
                                                  DemoAppConfig::kTileHeight,
                                                  nowTicks,
                                                  command.depth01);
        } else {
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
        }

        if (isCompleteGrandCannon(command.instance)) {
          const auto anchor = grandCannonTurretAnchor(command.instance, definitions.rules(),
                                                      mapOrigin,
                                                      DemoAppConfig::kTileWidth,
                                                      DemoAppConfig::kTileHeight);
          const float turretDepth = grandCannonTurretDepth(command.instance.placement,
                                                          DemoAppConfig::kTileWidth,
                                                          DemoAppConfig::kTileHeight);
          grandCannonRenderer.renderInWorld(grandCannonRenderState(houseColors,
                                                                    debugPanelState,
                                                                    command.instance),
                                            viewport.width,
                                            viewport.height,
                                            anchor.x,
                                            anchor.y,
                                            turretDepth,
                                            0.0f);
          renderer.beginWorldPass();
        }

        if (layeredWarFactory) {
          for (const auto& tank : rhinoTanks) {
            if (ra2::isFactoryExitVehicle(tank, command.instance)) {
              drawRhinoTankInstance(tank);
            }
          }
          if (command.instance.deferOverUnit || productionSplit) {
            drawWarFactoryOverUnitLayers(renderer,
                                         *command.asset,
                                         command.instance,
                                         mapOrigin,
                                         DemoAppConfig::kTileWidth,
                                         DemoAppConfig::kTileHeight,
                                         nowTicks,
                                         command.depth01,
                                         productionSplit);
          }
        }
      }



      for (const auto& tank : rhinoTanks) {
        if (drawnWithFactory(tank, buildings)) {
          continue;
        }
        drawRhinoTankInstance(tank);
      }

      renderer.beginWorldPass();
      ra2::drawGathering(renderer,resourceVisualAssets,world,definitions,
                         mapOrigin,DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight);
      drawWeaponProjectilesAndEffects(renderer,
                                      weaponVisualAssets,
                                      projectiles,
                                      combatEffects,
                                      mapOrigin,
                                      DemoAppConfig::kTileWidth,
                                      DemoAppConfig::kTileHeight);



      for (const auto& tank : rhinoTanks) {
        if (drawnWithFactory(tank, buildings)) {
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
      ra2::drawHarvesterCargo(renderer,world,selection.primary(),mapOrigin,DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight);
      drawSidebar(renderer,
                  static_cast<float>(viewport.width),
                  static_cast<float>(viewport.height),
                  sidebarAssets,
                  sidebarState);
      if (mouseInWorld && !mouseCapturedByDebugPanel && !hoveredBuildingIndex && !selectedBuildAsset) {
        const auto cell=screenToIso(mouseX,mouseY,mapOrigin,DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight);
        if (const auto resource=world.resourceAt(cell)) {
          const auto& mineral=definitions.resource(resource->type);
          ImGui::BeginTooltip();
          ImGui::TextUnformatted(mineral.image==2 ? "宝石" : "矿石");
          ImGui::Text("剩余 %d 单位  ·  每单位 $%d",resource->amount,mineral.value);
          ImGui::Text("本格总价值 $%lld",static_cast<long long>(resource->amount)*mineral.value);
          ImGui::EndTooltip();
        }
      }
      ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
      ImGui::SetNextWindowBgAlpha(0.82f);
      if (ImGui::Begin("Simulation", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("左键框选/移动/攻击，Shift 增选，Ctrl+左键强制攻击");
        ImGui::TextUnformatted("S 整组停止，D 部署，右键取消选择");
        if (selection.size()>1) ImGui::Text("已选中 %zu 个单位",selection.size());
        ImGui::TextUnformatted("矿车：点矿区采集，点己方矿场卸货，H 自动采矿");
        drawGameSpeedControls(debugPanelState.gameplay);
        ImGui::Text("实际逻辑：%.1f 帧/秒    绘制：%.1f FPS", measuredGameFps, ImGui::GetIO().Framerate);
        ImGui::Text("Tick: %llu  Credits: %d  Entities: %zu", static_cast<unsigned long long>(world.tick()), world.credits(), world.snapshot().size());
        if (!interactionMessage.empty()) ImGui::TextWrapped("%s", interactionMessage.c_str());
        if (!world.lastMessage().empty()) ImGui::TextWrapped("%s", world.lastMessage().c_str());
        for (const auto& entity : world.snapshot()) if (entity.id == selection.primary()) {
          ImGui::Text("%s  HP %d / %d", entity.type.c_str(), entity.hp, entity.maxHp);
          if(entity.harvesting) {
            const auto& cargo=*entity.harvesting;
            ImGui::Text("货舱 %d / %d    价值 $%d",cargo.cargo,cargo.capacity,cargo.value);
            ImGui::ProgressBar(static_cast<float>(cargo.cargo)/cargo.capacity,ImVec2(-1,0),ra2::harvestPhaseName(cargo.phase));
            if(ImGui::Button("自动采矿")) send(ra2::CommandKind::Harvest,selection.primary(),entity.cell);
          }
          switch (entity.factoryPhase) {
            case ra2::FactoryExitPhase::Blocked: ImGui::TextUnformatted("出口堵塞，等待通道空闲"); break;
            case ra2::FactoryExitPhase::Preparing: ImGui::TextUnformatted("出车准备中"); break;
            case ra2::FactoryExitPhase::Driving: ImGui::TextUnformatted("车辆正在驶出"); break;
            case ra2::FactoryExitPhase::Clearing: ImGui::TextUnformatted("出车收尾中"); break;
            default: break;
          }
          if (entity.activity == ra2::Activity::LeavingFactory)
            ImGui::TextUnformatted(entity.hasDeferredCommand ? "正在出厂，驶离后执行新命令" : "正在出厂");
          if (entity.queueSize) {
            ImGui::Text("%s  Queue: %zu", entity.productionType.c_str(), entity.queueSize);
            ImGui::ProgressBar(1.0f - static_cast<float>(entity.productionRemaining) / std::max(1, entity.productionTotal));
            if (ImGui::Button("取消最后一项")) send(ra2::CommandKind::CancelProduction, selection.primary());
          }
        }
      }
      ImGui::End();
      if (gameSpeedSmokeTest) {
        const auto advanced = world.tick() - speedSmokePreviousFrame;
        const auto rate = ra2::targetGameFrameRate(smokeSpeeds[smokeTestFrames]);
        if ((rate && advanced != static_cast<ra2::Tick>(*rate / 10)) || (!rate && (advanced < 8 || advanced % 8 != 0)))
          throw std::runtime_error("Game speed render-loop smoke assertion failed");
        std::cout << "Speed smoke render " << smokeTestFrames << ": " << advanced << " game frames\n";
        speedSmokePreviousFrame = world.tick();
        if (smokeTestFrames + 1 < static_cast<int>(smokeSpeeds.size()))
          debugPanelState.gameplay.speed = smokeSpeeds[smokeTestFrames + 1];
      }
      world.setGameSpeed(debugPanelState.gameplay.speed);
      if (selectionGesture.dragging()) {
        const auto box=selectionGesture.rectangle();
        auto* overlay=ImGui::GetForegroundDrawList();
        overlay->AddRectFilled(ImVec2(box.left,box.top),ImVec2(box.right,box.bottom),IM_COL32(64,255,100,20));
        overlay->AddRect(ImVec2(box.left,box.top),ImVec2(box.right,box.bottom),IM_COL32(64,255,100,255),0,0,1.5f);
      }
      renderImGuiDebugPanel();
      const std::array<ra2::Tick,7> buildingCaptureFrames{1,20,47,73,74,145,191};
      const bool captureBuilding = buildingSmokeTest && std::find(buildingCaptureFrames.begin(),buildingCaptureFrames.end(),world.tick())!=buildingCaptureFrames.end();
      const std::array<ra2::Tick,10> tankCaptureFrames{1,8,16,24,30,34,38,48,80,120};
      const bool captureTank=tankSmokeTest && std::find(tankCaptureFrames.begin(),tankCaptureFrames.end(),world.tick())!=tankCaptureFrames.end();
      const std::array<ra2::Tick,5> selectionCaptureFrames{2,3,7,60,140};
      const bool captureSelection=selectionSmokeTest && std::find(selectionCaptureFrames.begin(),selectionCaptureFrames.end(),world.tick())!=selectionCaptureFrames.end();
      if (((simulationSmokeTest && (smokeTestFrames == 31 || smokeTestFrames == 35)) || captureBuilding || captureTank || captureSelection || (miningSmokeTest && (smokeTestFrames==9 || smokeTestFrames==24 || smokeTestFrames==44))) && argc == 3) {
        std::vector<unsigned char> pixels(static_cast<std::size_t>(viewport.width) * viewport.height * 4);
        glReadPixels(0, 0, viewport.width, viewport.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        const auto pitch = static_cast<std::size_t>(viewport.width) * 4;
        for (int y = 0; y < viewport.height / 2; ++y) std::swap_ranges(pixels.begin() + y * pitch, pixels.begin() + (y + 1) * pitch, pixels.begin() + (viewport.height - y - 1) * pitch);
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(), viewport.width, viewport.height, 32, static_cast<int>(pitch), SDL_PIXELFORMAT_RGBA32);
        if (!surface) throw std::runtime_error(SDL_GetError());
        auto output = std::filesystem::path(argv[2]);
        if (captureBuilding || captureTank || captureSelection) output.replace_filename(output.stem().string() + "-frame-" + std::to_string(world.tick()) + ".bmp");
        else if (miningSmokeTest) output.replace_filename(output.stem().string() + "-frame-" + std::to_string(world.tick()) + ".bmp");
        else if (smokeTestFrames == 31) output.replace_filename(output.stem().string() + "-egress.bmp");
        const int saved = SDL_SaveBMP(surface, output.string().c_str());
        SDL_FreeSurface(surface);
        if (saved != 0) throw std::runtime_error(SDL_GetError());
      }
      SDL_GL_SwapWindow(window);
      if (smokeTest && ++smokeTestFrames >= (selectionSmokeTest ? 140 : tankSmokeTest ? 120 : miningSmokeTest ? 45 : buildingSmokeTest ? 192 : simulationSmokeTest ? 36 : gameSpeedSmokeTest ? static_cast<int>(smokeSpeeds.size()) : 3)) {
        if(selectionSmokeTest) {
          const auto entities=world.snapshot();
          std::vector<TileCoord> destinations;
          for(const auto& entity:entities) if(selection.contains(entity.id)) {
            if(entity.cell!=entity.destination || std::abs(entity.cell.x-10)>1 || std::abs(entity.cell.y-8)>1)
              throw std::runtime_error("Box-selected tank failed to arrive at group destination");
            destinations.push_back(entity.cell);
          }
          if(selection.size()!=2 || !selection.contains(2) || !selection.contains(selectionSmokeUnit) ||
              destinations.size()!=2 || destinations[0]==destinations[1] || glGetError()!=GL_NO_ERROR)
            throw std::runtime_error("Box selection/group command presentation smoke assertions failed");
        }
        if (tankSmokeTest && (!sawMovingTurret || !sawShell || !sawExplosion || glGetError()!=GL_NO_ERROR))
          throw std::runtime_error("Tank movement/turret/projectile presentation smoke assertions failed");
        if (miningSmokeTest && glGetError()!=GL_NO_ERROR)
          throw std::runtime_error("OpenGL error while rendering original mining assets");
        if(miningSmokeTest && (!sawMiningCargo || !sawMiningReturn || world.credits()<=definitions.profile().startingCredits))
          throw std::runtime_error("Mining presentation/income smoke assertions failed");
        if (buildingSmokeTest) {
          if (!sawBuildup[0] || !sawBuildup[1] || !sawComplete[0] || !sawComplete[1] || !sawPacking[0] || !sawPacking[1])
            throw std::runtime_error("Construction yard animation phase missing");
          for (const auto& entity : world.snapshot()) if ((entity.id==testMcv && entity.type!="AMCV") ||
                                                         (entity.id==testSovietMcv && entity.type!="SMCV"))
            throw std::runtime_error("Construction yard did not finish packing");
        }
        if (simulationSmokeTest) {
          bool deployed = false, enemyAlive = false;
          for (const auto& entity : world.snapshot()) {
            if (entity.id == testMcv && entity.type == "GACNST") deployed = true;
            if (entity.id == 4) enemyAlive = true;
          }
          if (!deployed || enemyAlive || !sawCombatDamage || !sawInternalExit || world.snapshot().size() != 9) throw std::runtime_error("Simulation/presentation smoke assertions failed");
        }
        running = false;
      }
    }

    cleanupApp(window,
               glContext,
               renderer,
               gridRenderer,
               rhinoRenderer,
               grizzlyRenderer,
               vehicleRendererCache,
               grandCannonRenderer,
               mcvRenderer,
               sidebarAssets,
               buildingAssetCache,
               rhinoUiAssets,
               weaponVisualAssets,
               resourceVisualAssets);
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    cleanupApp(window,
               glContext,
               renderer,
               gridRenderer,
               rhinoRenderer,
               grizzlyRenderer,
               vehicleRendererCache,
               grandCannonRenderer,
               mcvRenderer,
               sidebarAssets,
               buildingAssetCache,
               rhinoUiAssets,
               weaponVisualAssets,
               resourceVisualAssets);
    return 1;
  }
}
