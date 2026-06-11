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

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <cmath>
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

struct VehicleRenderSpec {
  VehicleUnitKind kind;
  BuildFaction faction;
  std::string_view iconId;
  std::string_view objectId;
  std::string_view folder;
  std::string_view bodyStem;
  std::string_view turretStem;
  std::string_view barrelStem;
};

struct VehicleRendererCache {
  std::unordered_map<int, std::unique_ptr<VplBoxRenderer>> renderers;
};

constexpr int kMcvDeployDirectionIndex = 12;
constexpr int kWarFactoryExitDirectionIndex = 0;
constexpr std::uint32_t kBuildingBuildupFrameDurationMs = 50;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kGrandCannonTurnSpeedRadiansPerSecond = 1.6f;

constexpr VehicleRenderSpec kVehicleRenderSpecs[] = {
  {VehicleUnitKind::AlliedHarvester, BuildFaction::Allied, "ahrvicon", "CMIN", "allied_harvester", "cmin", "", ""},
  {VehicleUnitKind::Grizzly, BuildFaction::Allied, "gtnkicon", "MTNK", "grizzly_tank", "gtnk", "gtnktur", "gtnkbarl"},
  {VehicleUnitKind::Ifv, BuildFaction::Allied, "fvicon", "FV", "ifv", "fv", "fvtur", ""},
  {VehicleUnitKind::TankDestroyer, BuildFaction::Allied, "tnkdicon", "TNKD", "tank_destroyer", "tnkd", "", ""},
  {VehicleUnitKind::BlackEagle, BuildFaction::Allied, "beagicon", "BEAG", "black_eagle", "beag", "", ""},
  {VehicleUnitKind::PrismTank, BuildFaction::Allied, "sreficon", "SREF", "prism_tank", "sref", "sreftur", ""},
  {VehicleUnitKind::MirageTank, BuildFaction::Allied, "rtnkicon", "RTNK", "mirage_tank", "rtnk", "rtnktur", "rtnkbarl"},
  {VehicleUnitKind::AlliedMcv, BuildFaction::Allied, "mcvicon", "AMCV", "allied_mcv", "mcv", "", ""},
  {VehicleUnitKind::Intruder, BuildFaction::Allied, "falcicon", "FALC", "intruder", "falc", "", ""},
  {VehicleUnitKind::BlackHawk, BuildFaction::Allied, "shadicon", "SHAD", "blackhawk", "shad", "", ""},
  {VehicleUnitKind::LandingCraft, BuildFaction::Allied, "landicon", "LCRF", "landing_craft", "lcrf", "", ""},
  {VehicleUnitKind::Destroyer, BuildFaction::Allied, "desticon", "DEST", "destroyer", "dest", "", ""},
  {VehicleUnitKind::AegisCruiser, BuildFaction::Allied, "agisicon", "AEGIS", "aegis_cruiser", "aegis", "", ""},
  {VehicleUnitKind::AircraftCarrier, BuildFaction::Allied, "carricon", "CARRIER", "aircraft_carrier", "carrier", "", ""},
  {VehicleUnitKind::SovietHarvester, BuildFaction::Soviet, "harvicon", "HARV", "soviet_harvester", "harv", "harvtur", ""},
  {VehicleUnitKind::Rhino, BuildFaction::Soviet, "htnkicon", "HTNK", "rhino_tank", "htnk", "htnktur", "htnkbarl"},
  {VehicleUnitKind::V3Launcher, BuildFaction::Soviet, "v3icon", "V3", "v3_launcher", "v3", "", ""},
  {VehicleUnitKind::FlakTrack, BuildFaction::Soviet, "htkicon", "HTK", "flak_track", "htk", "htktur", "htkbarl"},
  {VehicleUnitKind::TeslaTank, BuildFaction::Soviet, "ttnkicon", "TTNK", "tesla_tank", "ttnk", "ttnktur", ""},
  {VehicleUnitKind::ApocalypseTank, BuildFaction::Soviet, "mtnkicon", "APOC", "apocalypse_tank", "mtnk", "mtnktur", "mtnkbarl"},
  {VehicleUnitKind::AmphibiousTransport, BuildFaction::Soviet, "sapcicon", "SAPC", "amphibious_transport", "trs", "", ""},
  {VehicleUnitKind::Submarine, BuildFaction::Soviet, "subicon", "SUB", "submarine", "sub", "", ""},
  {VehicleUnitKind::Dreadnought, BuildFaction::Soviet, "dredicon", "DRED", "dreadnought", "dred", "", ""},
  {VehicleUnitKind::KirovAirship, BuildFaction::Soviet, "zepicon", "ZEP", "kirov_airship", "zep", "", ""},
  {VehicleUnitKind::SovietMcv, BuildFaction::Soviet, "mcvicon", "SMCV", "soviet_mcv", "smcv", "", ""}
};

void destroyVehicleRendererCache(VehicleRendererCache& cache);

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
                WeaponVisualAssets& weaponVisualAssets) {
  shutdownImGuiDebugPanel();
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

[[nodiscard]] WeaponRuntimeParams weaponParamsFromDebugState(const WeaponDebugState& state) {
  return WeaponRuntimeParams{
    state.rangeCells,
    state.rofFrames,
    state.rofFramesPerSecond,
    state.turretTurnSpeedRadiansPerSecond,
    state.fireTurnToleranceRadians,
    state.fireForwardLeptons,
    state.fireLateralLeptons,
    state.fireHeightLeptons,
    state.arcingSpeedLeptonsPerFrame,
    state.gravityLeptonsPerFrameSquared,
    state.projectileRulesFramesPerSecond,
    state.minDurationRulesFrames
  };
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

[[nodiscard]] int vehicleRendererCacheKey(const VehicleUnitKind kind) {
  return static_cast<int>(kind);
}

[[nodiscard]] bool usesDedicatedVehicleRenderer(const VehicleUnitKind kind) {
  return kind == VehicleUnitKind::Rhino || kind == VehicleUnitKind::Grizzly;
}

[[nodiscard]] const VehicleRenderSpec* vehicleSpecForKind(const VehicleUnitKind kind) {
  for (const auto& spec : kVehicleRenderSpecs) {
    if (spec.kind == kind) {
      return &spec;
    }
  }
  return nullptr;
}

[[nodiscard]] const VehicleRenderSpec* vehicleSpecForProductionIcon(const std::string_view iconId,
                                                                    const BuildFaction faction) {
  for (const auto& spec : kVehicleRenderSpecs) {
    if (spec.faction == faction && spec.iconId == iconId) {
      return &spec;
    }
  }
  return nullptr;
}

VplBoxRenderer& ensureCachedVehicleRenderer(VehicleRendererCache& cache,
                                            SDL_Window* window,
                                            const DemoAppPaths& paths,
                                            const VplFile& voxelLighting,
                                            const Palette& unitPalette,
                                            const VehicleUnitKind kind) {
  const int cacheKey = vehicleRendererCacheKey(kind);
  if (const auto it = cache.renderers.find(cacheKey); it != cache.renderers.end()) {
    return *it->second;
  }

  const auto* spec = vehicleSpecForKind(kind);
  if (spec == nullptr) {
    throw std::runtime_error("Missing vehicle render spec");
  }

  auto renderer = std::make_unique<VplBoxRenderer>();
  renderer->initialize(window);
  renderer->loadVehicleAssets(paths.vehicleRoot / std::string(spec->folder),
                              voxelLighting,
                              std::string(spec->bodyStem),
                              std::string(spec->turretStem),
                              std::string(spec->barrelStem));
  renderer->setPalette(unitPalette);

  auto [it, inserted] = cache.renderers.emplace(cacheKey, std::move(renderer));
  (void)inserted;
  return *it->second;
}

void preloadVehicleRenderers(VehicleRendererCache& cache,
                             SDL_Window* window,
                             const DemoAppPaths& paths,
                             const VplFile& voxelLighting,
                             const Palette& unitPalette) {
  for (const auto& spec : kVehicleRenderSpecs) {
    if (!usesDedicatedVehicleRenderer(spec.kind)) {
      ensureCachedVehicleRenderer(cache, window, paths, voxelLighting, unitPalette, spec.kind);
    }
  }
}

void setVehicleRendererCachePalette(VehicleRendererCache& cache, const Palette& unitPalette) {
  for (auto& [cacheKey, renderer] : cache.renderers) {
    (void)cacheKey;
    renderer->setPalette(unitPalette);
  }
}

void destroyVehicleRendererCache(VehicleRendererCache& cache) {
  for (auto& [cacheKey, renderer] : cache.renderers) {
    (void)cacheKey;
    renderer->destroy();
  }
  cache.renderers.clear();
}

VplBoxRenderer& vehicleRendererForKind(const VehicleUnitKind kind,
                                       VplBoxRenderer& rhinoRenderer,
                                       VplBoxRenderer& grizzlyRenderer,
                                       VehicleRendererCache& cache,
                                       SDL_Window* window,
                                       const DemoAppPaths& paths,
                                       const VplFile& voxelLighting,
                                       const Palette& unitPalette) {
  switch (kind) {
    case VehicleUnitKind::Rhino:
      return rhinoRenderer;
    case VehicleUnitKind::Grizzly:
      return grizzlyRenderer;
    default:
      return ensureCachedVehicleRenderer(cache, window, paths, voxelLighting, unitPalette, kind);
  }
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
  return (building.assetId == "GAWEAP" || building.assetId == "NAWEAP") &&
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

[[nodiscard]] float rotateTowardAngle(const float currentRadians,
                                      const float targetRadians,
                                      const float maxStepRadians) {
  const float delta = shortestAngleDelta(currentRadians, targetRadians);
  if (std::fabs(delta) <= maxStepRadians) {
    return normalizeAngleRadians(targetRadians);
  }
  return normalizeAngleRadians(currentRadians + (delta > 0.0f ? maxStepRadians : -maxStepRadians));
}

[[nodiscard]] Vec2 screenToLogicalFloat(const float screenX,
                                        const float screenY,
                                        const Vec2 origin,
                                        const float tileWidth,
                                        const float tileHeight) {
  const float dx = (screenX - origin.x) / (tileWidth * 0.5f);
  const float dy = (screenY - origin.y) / (tileHeight * 0.5f);
  return Vec2{
    (dy + dx) * 0.5f,
    (dy - dx) * 0.5f
  };
}

[[nodiscard]] Vec2 logicalCenterForPlacementLocal(const BuildingPlacement& placement) {
  const auto tiles = foundationTiles(placement);
  if (tiles.empty()) {
    return Vec2{static_cast<float>(placement.topLeft.x), static_cast<float>(placement.topLeft.y)};
  }

  Vec2 sum{};
  for (const auto tile : tiles) {
    sum.x += static_cast<float>(tile.x);
    sum.y += static_cast<float>(tile.y);
  }
  const float invCount = 1.0f / static_cast<float>(tiles.size());
  return Vec2{sum.x * invCount, sum.y * invCount};
}

bool issueGrandCannonTurnCommand(BuildingInstance& building,
                                 const float mouseX,
                                 const float mouseY,
                                 const Vec2 mapOrigin,
                                 const float tileWidth,
                                 const float tileHeight) {
  if (!isCompleteGrandCannon(building)) {
    return false;
  }

  const auto center = logicalCenterForPlacementLocal(building.placement);
  const auto target = screenToLogicalFloat(mouseX, mouseY, mapOrigin, tileWidth, tileHeight);
  const Vec2 delta{target.x - center.x, target.y - center.y};
  if (delta.x * delta.x + delta.y * delta.y <= 0.0001f) {
    return false;
  }

  building.turretTargetHeadingRadians = normalizeAngleRadians(std::atan2(delta.y, delta.x));
  return true;
}

void updateGrandCannonTurrets(std::vector<BuildingInstance>& buildings, const float deltaSeconds) {
  const float maxStep = kGrandCannonTurnSpeedRadiansPerSecond * std::max(0.0f, deltaSeconds);
  for (auto& building : buildings) {
    if (!isCompleteGrandCannon(building)) {
      continue;
    }
    building.turretHeadingRadians =
      rotateTowardAngle(building.turretHeadingRadians, building.turretTargetHeadingRadians, maxStep);
  }
}

[[nodiscard]] Vec2 grandCannonTurretAnchor(const BuildingInstance& building,
                                           const Vec2 mapOrigin,
                                           const float tileWidth,
                                           const float tileHeight) {
  constexpr float kTurretAnimX = 3.0f;
  constexpr float kTurretAnimY = 28.0f;

  const auto baseAnchor = isoToScreen(building.placement.topLeft.x,
                                      building.placement.topLeft.y,
                                      mapOrigin,
                                      tileWidth,
                                      tileHeight);
  return Vec2{
    baseAnchor.x + kTurretAnimX,
    baseAnchor.y + kTurretAnimY
  };
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

[[nodiscard]] bool stopSelectedRhinoGroundFire(std::vector<RhinoUnitState>& tanks) {
  bool hasSelectedRhino = false;
  for (auto& tank : tanks) {
    if (tank.kind == VehicleUnitKind::Rhino && tank.selected) {
      stopRhinoGroundFire(tank);
      hasSelectedRhino = true;
    }
  }
  return hasSelectedRhino;
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
  VplBoxRenderer grizzlyRenderer;
  VehicleRendererCache vehicleRendererCache;
  VplBoxRenderer grandCannonRenderer;
  VplBoxRenderer mcvRenderer;
  SidebarAssets sidebarAssets;
  BuildingAssetCache buildingAssetCache;
  RhinoUnitUiAssets rhinoUiAssets;
  WeaponVisualAssets weaponVisualAssets;

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
    const auto animationPalette = Palette::load(paths.paletteRoot / "effects" / "anim.pal");
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
    weaponVisualAssets = loadWeaponVisualAssets(paths.effectsRoot, animationPalette);

    rhinoRenderer.initialize(window);
    rhinoRenderer.loadRhinoAssets(paths.rhinoRoot, voxelLighting);
    rhinoRenderer.setPalette(initialTheaterPalettes.unit);
    grizzlyRenderer.initialize(window);
    grizzlyRenderer.loadVehicleAssets(paths.grizzlyRoot, voxelLighting, "gtnk", "gtnktur", "gtnkbarl");
    grizzlyRenderer.setPalette(initialTheaterPalettes.unit);
    preloadVehicleRenderers(vehicleRendererCache,
                            window,
                            paths,
                            voxelLighting,
                            initialTheaterPalettes.unit);
    grandCannonRenderer.initialize(window);
    grandCannonRenderer.loadTurretAssets(paths.buildingRoot / "allied" / "gtgcan",
                                         voxelLighting,
                                         "gtgcantur",
                                         "gtgcanbarl");
    grandCannonRenderer.setPalette(initialTheaterPalettes.unit);
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
    std::vector<ProjectileInstance> projectiles;
    std::vector<CombatEffectInstance> combatEffects;
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
      const WeaponRuntimeParams weaponParams = weaponParamsFromDebugState(debugPanelState.weapon);

      updateSidebarStateForDemo(sidebarState, sidebarAssets, viewport.height, nowTicks);
      updateConstructionStates(buildings, *activeBuildingAssets, nowTicks);
      updateGrandCannonTurrets(buildings, simulationFrame.deltaSeconds);

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

        if (isStopCommandEvent(event) && stopSelectedRhinoGroundFire(rhinoTanks)) {
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
              } else if (const auto* vehicleSpec =
                           vehicleSpecForProductionIcon(sidebarClick.iconId, debugPanelState.style.faction);
                         vehicleSpec != nullptr &&
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
                    const VehicleUnitKind producedKind = vehicleSpec->kind;
                    RhinoUnitState producedTank;
                    producedTank.kind = producedKind;
                    producedTank.tilePosition =
                      Vec2{static_cast<float>(productionCenter.x), static_cast<float>(productionCenter.y)};
                    producedTank.occupiedCell = productionCenter;
                    producedTank.selected = true;
                    producedTank.maxHp = maxHpForObject(std::string(vehicleSpec->objectId));
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

          if (isCtrlModifierDown()) {
            if (const auto selectedTankIndex = selectedRhinoTankIndex(rhinoTanks); selectedTankIndex.has_value()) {
              auto& selectedTank = rhinoTanks[*selectedTankIndex];
              if (issueRhinoGroundFireCommand(selectedTank,
                                              map,
                                              screenToLogicalFloat(mouseX,
                                                                   mouseY,
                                                                   mapOrigin,
                                                                   DemoAppConfig::kTileWidth,
                                                                   DemoAppConfig::kTileHeight),
                                              nowTicks,
                                              weaponParams)) {
                continue;
              }
            }
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

          if (selectedBuildingIndex.has_value() && *selectedBuildingIndex < buildings.size()) {
            auto& selectedBuilding = buildings[*selectedBuildingIndex];
            if (isCompleteGrandCannon(selectedBuilding)) {
              const auto targetCell = screenToIso(mouseX,
                                                  mouseY,
                                                  mapOrigin,
                                                  DemoAppConfig::kTileWidth,
                                                  DemoAppConfig::kTileHeight);
              if (map.isBuildable(targetCell)) {
                issueGrandCannonTurnCommand(selectedBuilding,
                                            mouseX,
                                            mouseY,
                                            mapOrigin,
                                            DemoAppConfig::kTileWidth,
                                            DemoAppConfig::kTileHeight);
              }
              continue;
            }
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
        updateRhinoWeapon(tank, projectiles, combatEffects, simulationFrame.deltaSeconds, nowTicks, weaponParams);
      }
      updateWeaponProjectilesAndEffects(projectiles, combatEffects, simulationFrame.deltaSeconds);
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
            grizzlyRenderer.setPalette(currentTheaterPalettes.unit);
            setVehicleRendererCachePalette(vehicleRendererCache, currentTheaterPalettes.unit);
            grandCannonRenderer.setPalette(currentTheaterPalettes.unit);
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

      const auto& drawTheaterPalettes = palettesForTheater(paletteLibrary, debugPanelState.style.theater);
      auto drawRhinoTankInstance = [&](const RhinoUnitState& tank) {
        VplBoxRenderer& vehicleRenderer = vehicleRendererForKind(tank.kind,
                                                                 rhinoRenderer,
                                                                 grizzlyRenderer,
                                                                 vehicleRendererCache,
                                                                 window,
                                                                 paths,
                                                                 voxelLighting,
                                                                 drawTheaterPalettes.unit);
        auto renderState = buildRhinoTankRenderState(houseColors,
                                                     debugPanelState,
                                                     rhinoDirectionIndex(tank));
        if (tank.kind == VehicleUnitKind::Rhino) {
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
      auto warFactoryHasInternalTank = [&](const BuildingInstance& factory) {
        for (const auto& tank : rhinoTanks) {
          if (rhinoInsideWarFactoryFootprint(tank, factory)) {
            return true;
          }
        }
        return false;
      };

      renderer.beginWorldPass();
      for (const auto& command : renderQueue) {
        const bool layeredWarFactory = isLayeredWarFactoryInstance(command.instance);
        const bool hasInternalTank =
          layeredWarFactory && warFactoryHasInternalTank(command.instance);
        const bool sovietProductionSplit =
          command.instance.assetId == "NAWEAP" && hasInternalTank;

        if (sovietProductionSplit) {
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
          const auto anchor = grandCannonTurretAnchor(command.instance,
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
            if (rhinoInsideWarFactoryFootprint(tank, command.instance)) {
              drawRhinoTankInstance(tank);
            }
          }
          if (command.instance.assetId == "GAWEAP" || sovietProductionSplit) {
            drawWarFactoryOverUnitLayers(renderer,
                                         *command.asset,
                                         command.instance,
                                         mapOrigin,
                                         DemoAppConfig::kTileWidth,
                                         DemoAppConfig::kTileHeight,
                                         nowTicks,
                                         command.depth01,
                                         sovietProductionSplit);
          }
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

      renderer.beginWorldPass();
      drawWeaponProjectilesAndEffects(renderer,
                                      weaponVisualAssets,
                                      projectiles,
                                      combatEffects,
                                      mapOrigin,
                                      DemoAppConfig::kTileWidth,
                                      DemoAppConfig::kTileHeight);

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
               grizzlyRenderer,
               vehicleRendererCache,
               grandCannonRenderer,
               mcvRenderer,
               sidebarAssets,
               buildingAssetCache,
               rhinoUiAssets,
               weaponVisualAssets);
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
               weaponVisualAssets);
    return 1;
  }
}
