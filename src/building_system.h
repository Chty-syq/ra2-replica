#pragma once

#include "art_ini.h"
#include "compat/rules_timing.h"
#include "demo_style.h"
#include "map_grid.h"
#include "palette.h"
#include "shp_ts.h"
#include "ui_texture.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// demo 中建筑只需要一个很小的状态机：
// - Preview：正在放置预览
// - Constructing：播放建造动画
// - Complete：播放完成态与工作层
enum class BuildingState {
  Preview,
  Constructing,
  Packing,
  Complete
};

// 同一种建筑共享的不可变资源。
// 实例只保留 assetId 和运行时状态，避免重复持有纹理。
struct BuildingAsset {
  std::string id;
  ArtDefinition art;
  FoundationSize foundation;

  enum class LayerRole {
    UnderUnit,
    Normal,
    OverUnit,
    ProductionUnderUnit,
    ProductionOverUnit
  };

  // 完成态的基础纹理和原始帧。
  UiTexture completeTexture;
  DecodedFrame completeFrame;

  // 完成态可选附加层。
  // 例如：主楼体、bib、灯光、转动部件等都可以作为独立层叠加。
  struct RenderLayer {
    std::string id;
    std::vector<UiTexture> textures;
    std::uint32_t frameDurationMs = 0;
    LayerRole role = LayerRole::Normal;
    std::vector<std::size_t> sourceFrames{};
    int gameFrameDelay = -1; // -1 keeps the legacy experiment clock; 0 freezes an animation.
    bool normalized = false;
    PixelOffset offset{}; // Screen pixels, relative to the common building draw center.
    int zAdjust = 0;
    int ySort = 0;
    int normalZAdjust = 0;
  };
  std::vector<RenderLayer> completeLayers;

  // 建造动画会同时保留纹理和解码帧：
  // - 渲染走纹理
  // - 锚点和尺寸推导仍可能需要原始帧信息
  std::vector<UiTexture> buildupTextures;
  std::vector<DecodedFrame> buildupFrames;
};

// 地图上的建筑实例。
struct BuildingInstance {
  std::string assetId;
  BuildingPlacement placement;
  BuildingState state = BuildingState::Complete;
  std::uint32_t stateStartTicks = 0;
  int maxHp = 1000;
  int hp = 1000;
  float turretHeadingRadians = 4.71238898038f;
  float turretTargetHeadingRadians = 4.71238898038f;
  std::uint32_t stateDurationMs = 0;
  bool layeredFactory = false;
  bool productionSplit = false;
  bool deferOverUnit = false;
  std::uint64_t entityId = 0;
  std::uint64_t exitingUnitId = 0;
  bool factoryExitActive = false;
  std::uint32_t factoryCycleStartTicks = 0;
  ra2::GameFrame visualFrame = 0, animationStartedFrame = 0, factoryCycleStartedFrame = 0;
  ra2::GameSpeed animationSpeed{}, productionAnimationSpeed{};
};

// Every building pass uses the same clock, including foreground redraws.
// Production layers start a fresh loop for each simulation-owned exit cycle.
[[nodiscard]] inline const UiTexture* buildingLayerTexture(const BuildingAsset::RenderLayer& layer,
                                                          const BuildingInstance& instance,
                                                          std::uint32_t nowTicks) {
  if (layer.textures.empty()) return nullptr;
  const bool production = layer.role == BuildingAsset::LayerRole::ProductionUnderUnit ||
                          layer.role == BuildingAsset::LayerRole::ProductionOverUnit;
  const bool factoryCycle = production && instance.factoryExitActive;
  if (layer.gameFrameDelay >= 0) {
    const auto speed = factoryCycle ? instance.productionAnimationSpeed : instance.animationSpeed;
    const int delay = layer.normalized ? ra2::compat::normalizedAnimationDelay(layer.gameFrameDelay,speed) : layer.gameFrameDelay;
    const auto startFrame = factoryCycle ? instance.factoryCycleStartedFrame : instance.animationStartedFrame;
    const auto elapsed = instance.visualFrame >= startFrame ? instance.visualFrame-startFrame : 0;
    return &layer.textures[delay > 0 ? (elapsed / delay) % layer.textures.size() : 0];
  }
  const auto start = factoryCycle ? instance.factoryCycleStartTicks : instance.stateStartTicks;
  const auto duration = layer.frameDurationMs < 50 ? 50 : layer.frameDurationMs;
  const auto index = layer.frameDurationMs > 0 ? ((nowTicks - start) / duration) % layer.textures.size() : 0;
  return &layer.textures[index];
}

using BuildingAssetMap = std::unordered_map<std::string, BuildingAsset>;

[[nodiscard]] inline std::size_t buildingBuildupFrameIndex(const BuildingInstance& instance,
                                                          std::size_t count, std::uint32_t nowTicks) {
  if (!count) return 0;
  const auto elapsed = nowTicks - instance.stateStartTicks;
  const auto index = instance.stateDurationMs > 0 ?
    static_cast<std::uint64_t>(elapsed) * count / instance.stateDurationMs : elapsed / 50;
  const auto bounded = index < count ? static_cast<std::size_t>(index) : count - 1;
  return instance.state == BuildingState::Packing ? count - 1 - bounded : bounded;
}

// 加载 demo 当前支持的建筑资源。
// 这里会把 art.ini、调色盘和 SHP 资源组装成主渲染层、工作层与建造动画。
[[nodiscard]] BuildingAssetMap loadBuildingAssets(const std::filesystem::path& spriteRoot,
                                                  const ArtIni& artIni,
                                                  const Palette& unitPalette,
                                                  const Palette& terrainPalette,
                                                  TheaterStyle theater,
                                                  BuildFaction faction);

// 按 building id 单独加载一栋建筑的资源。
// 这给按需缓存提供了基础，避免切换风格时总是一次性把整套建筑全重建。
[[nodiscard]] BuildingAsset loadBuildingAssetById(const std::filesystem::path& spriteRoot,
                                                  const ArtIni& artIni,
                                                  const Palette& unitPalette,
                                                  const Palette& terrainPalette,
                                                  TheaterStyle theater,
                                                  const std::string& buildingId);

// 侧栏图标使用 cameo id，世界里的建筑使用 building id。
// 这个映射就是 UI 与建筑系统之间的桥。
[[nodiscard]] std::optional<std::string> buildingIdForIcon(std::string_view iconId, BuildFaction faction);

[[nodiscard]] const BuildingAsset& assetForInstance(const BuildingAssetMap& assets,
                                                    const BuildingInstance& instance);

// 推进所有“建造中”建筑的状态。
// 建造动画播完后，会自动切到 Complete，并从工作层第 0 帧开始循环。
void updateConstructionStates(std::vector<BuildingInstance>& buildings,
                              const BuildingAssetMap& assets,
                              std::uint32_t nowTicks);

// 释放建筑资源持有的 OpenGL 纹理。
void destroyBuildingAssets(BuildingAssetMap& assets);
