#pragma once

#include "art_ini.h"
#include "building_system.h"
#include "demo_style.h"
#include "iso_world.h"
#include "palette.h"

#include <cstdint>
#include <filesystem>
#include <unordered_map>

// 当前 demo 的窗口、地图和侧栏尺寸常量。
// 这些值在主循环、地图坐标换算和 UI 布局里都会用到，单独收口后更容易查找和调整。
struct DemoAppConfig {
  static constexpr int kViewportWidth = 1280;
  static constexpr int kViewportHeight = 720;
  static constexpr float kTileWidth = 60.0f;
  static constexpr float kTileHeight = 30.0f;
  static constexpr float kSidebarWidth = 168.0f;
  static constexpr Vec2 kMapOrigin{640.0f, 110.0f};
};

// 同一剧院下，建筑主体和地形分别使用不同调色盘。
struct TheaterPaletteSet {
  Palette unit;
  Palette terrain;
};

struct PaletteLibrary {
  TheaterPaletteSet temperate;
  TheaterPaletteSet snow;
  TheaterPaletteSet urban;
};

// 项目当前真正使用的资源根路径。
// 统一放在这里，避免主入口里散落大量 path 拼接代码。
struct DemoAppPaths {
  std::filesystem::path projectRoot;
  std::filesystem::path settingsRoot;
  std::filesystem::path buildingRoot;
  std::filesystem::path paletteRoot;
  std::filesystem::path iconRoot;
  std::filesystem::path sidebarUiRoot;
  std::filesystem::path unitOverlayRoot;
  std::filesystem::path rhinoRoot;
};

using BuildingAssetStyleKey = std::uint64_t;

struct BuildingAssetCache {
  // key = theater + houseColorIndex。
  // 同一套 style 的建筑资源只构建一次，后续切换时直接复用。
  std::unordered_map<BuildingAssetStyleKey, BuildingAssetMap> cachedByStyle;
};

void configureOpenGlContext();

[[nodiscard]] DemoAppPaths locateDemoAppPaths(std::filesystem::path current);
[[nodiscard]] PaletteLibrary loadPaletteLibrary(const std::filesystem::path& paletteRoot);
[[nodiscard]] const TheaterPaletteSet& palettesForTheater(const PaletteLibrary& library,
                                                          TheaterStyle theater);

[[nodiscard]] BuildingAssetMap buildAssetsForStyle(const std::filesystem::path& spriteRoot,
                                                   const ArtIni& artIni,
                                                   const PaletteLibrary& paletteLibrary,
                                                   const HouseColorSet& houseColors,
                                                   const DemoVisualStyle& style);

[[nodiscard]] BuildingAssetStyleKey makeBuildingAssetStyleKey(const DemoVisualStyle& style);

[[nodiscard]] BuildingAssetMap& cachedAssetsForStyle(BuildingAssetCache& cache,
                                                     const std::filesystem::path& spriteRoot,
                                                     const ArtIni& artIni,
                                                     const PaletteLibrary& paletteLibrary,
                                                     const HouseColorSet& houseColors,
                                                     const DemoVisualStyle& style);

[[nodiscard]] const BuildingAsset& ensureCachedAssetForStyle(BuildingAssetCache& cache,
                                                             const std::filesystem::path& spriteRoot,
                                                             const ArtIni& artIni,
                                                             const PaletteLibrary& paletteLibrary,
                                                             const HouseColorSet& houseColors,
                                                             const DemoVisualStyle& style,
                                                             const std::string& buildingId);

void switchBuildingAssetsForStyle(BuildingAssetCache& cache,
                                  BuildingAssetMap*& activeAssets,
                                  const std::filesystem::path& spriteRoot,
                                  const ArtIni& artIni,
                                  const PaletteLibrary& paletteLibrary,
                                  const HouseColorSet& houseColors,
                                  const DemoVisualStyle& style,
                                  const BuildingAsset*& selectedBuildAsset,
                                  BuildingPlacement& previewPlacement);

void destroyBuildingAssetCache(BuildingAssetCache& cache);
