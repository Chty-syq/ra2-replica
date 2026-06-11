#include "demo_app_support.h"

#include <SDL.h>

#include <optional>
#include <stdexcept>

void configureOpenGlContext() {
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
}

DemoAppPaths locateDemoAppPaths(std::filesystem::path current) {
  while (!current.empty()) {
    if (std::filesystem::exists(current / "assets")) {
      return DemoAppPaths{
        current,
        current / "assets" / "settings",
        current / "assets" / "buildings",
        current / "assets" / "palettes",
        current / "assets" / "icons" / "cameo",
        current / "assets" / "ui" / "sidebar",
        current / "assets" / "effects",
        current / "assets" / "ui" / "unit_overlays" / "common",
        current / "assets" / "vehicles",
        current / "assets" / "vehicles" / "rhino_tank",
        current / "assets" / "vehicles" / "grizzly_tank",
        current / "assets" / "vehicles" / "allied_mcv",
        current / "assets" / "vehicles" / "soviet_mcv"
      };
    }

    if (current == current.root_path()) {
      break;
    }
    current = current.parent_path();
  }

  throw std::runtime_error("Could not locate project root containing assets/");
}

std::filesystem::path sidebarUiRootForFaction(const DemoAppPaths& paths, const BuildFaction faction) {
  switch (faction) {
    case BuildFaction::Allied: return paths.sidebarRoot / "sidec01";
    case BuildFaction::Soviet: return paths.sidebarRoot / "sidec02";
  }
  return paths.sidebarRoot / "sidec01";
}

std::filesystem::path sidebarPalettePathForFaction(const DemoAppPaths& paths, const BuildFaction faction) {
  switch (faction) {
    case BuildFaction::Allied: return paths.paletteRoot / "ui" / "sidebar" / "sidec01.pal";
    case BuildFaction::Soviet: return paths.paletteRoot / "ui" / "sidebar" / "sidec02.pal";
  }
  return paths.paletteRoot / "ui" / "sidebar" / "sidec01.pal";
}

std::filesystem::path mcvVoxelRootForFaction(const DemoAppPaths& paths, const BuildFaction faction) {
  switch (faction) {
    case BuildFaction::Allied: return paths.alliedMcvRoot;
    case BuildFaction::Soviet: return paths.sovietMcvRoot;
  }
  return paths.alliedMcvRoot;
}

PaletteLibrary loadPaletteLibrary(const std::filesystem::path& paletteRoot) {
  return PaletteLibrary{
    TheaterPaletteSet{
      Palette::load(paletteRoot / "theater" / "unittem.pal"),
      Palette::load(paletteRoot / "theater" / "isotem.pal")
    },
    TheaterPaletteSet{
      Palette::load(paletteRoot / "theater" / "unitsno.pal"),
      Palette::load(paletteRoot / "theater" / "isosno.pal")
    },
    TheaterPaletteSet{
      Palette::load(paletteRoot / "theater" / "uniturb.pal"),
      Palette::load(paletteRoot / "theater" / "isourb.pal")
    }
  };
}

const TheaterPaletteSet& palettesForTheater(const PaletteLibrary& library, const TheaterStyle theater) {
  switch (theater) {
    case TheaterStyle::Temperate: return library.temperate;
    case TheaterStyle::Snow: return library.snow;
    case TheaterStyle::Urban: return library.urban;
  }
  return library.temperate;
}

BuildingAssetMap buildAssetsForStyle(const std::filesystem::path& spriteRoot,
                                     const ArtIni& artIni,
                                     const PaletteLibrary& paletteLibrary,
                                     const HouseColorSet& houseColors,
                                     const DemoVisualStyle& style) {
  (void)houseColors;
  const auto& paletteSet = palettesForTheater(paletteLibrary, style.theater);
  return loadBuildingAssets(spriteRoot,
                            artIni,
                            paletteSet.unit,
                            paletteSet.terrain,
                            style.theater,
                            style.faction);
}

BuildingAssetStyleKey makeBuildingAssetStyleKey(const DemoVisualStyle& style) {
  return static_cast<BuildingAssetStyleKey>(style.theater);
}

BuildingAssetMap& cachedAssetsForStyle(BuildingAssetCache& cache,
                                       const std::filesystem::path& spriteRoot,
                                       const ArtIni& artIni,
                                       const PaletteLibrary& paletteLibrary,
                                       const HouseColorSet& houseColors,
                                       const DemoVisualStyle& style) {
  (void)spriteRoot;
  (void)artIni;
  (void)paletteLibrary;
  (void)houseColors;

  const auto key = makeBuildingAssetStyleKey(style);
  if (const auto it = cache.cachedByStyle.find(key); it != cache.cachedByStyle.end()) {
    return it->second;
  }

  BuildingAssetMap assets;
  assets.reserve(32);
  return cache.cachedByStyle.emplace(key, std::move(assets)).first->second;
}

const BuildingAsset& ensureCachedAssetForStyle(BuildingAssetCache& cache,
                                               const std::filesystem::path& spriteRoot,
                                               const ArtIni& artIni,
                                               const PaletteLibrary& paletteLibrary,
                                               const HouseColorSet& houseColors,
                                               const DemoVisualStyle& style,
                                               const std::string& buildingId) {
  auto& assets = cachedAssetsForStyle(cache, spriteRoot, artIni, paletteLibrary, houseColors, style);
  if (const auto it = assets.find(buildingId); it != assets.end()) {
    return it->second;
  }

  (void)houseColors;
  const auto& paletteSet = palettesForTheater(paletteLibrary, style.theater);
  auto asset = loadBuildingAssetById(spriteRoot,
                                     artIni,
                                     paletteSet.unit,
                                     paletteSet.terrain,
                                     style.theater,
                                     buildingId);
  return assets.emplace(buildingId, std::move(asset)).first->second;
}

void switchBuildingAssetsForStyle(BuildingAssetCache& cache,
                                  BuildingAssetMap*& activeAssets,
                                  const std::filesystem::path& spriteRoot,
                                  const ArtIni& artIni,
                                  const PaletteLibrary& paletteLibrary,
                                  const HouseColorSet& houseColors,
                                  const DemoVisualStyle& style,
                                  const BuildingAsset*& selectedBuildAsset,
                                  BuildingPlacement& previewPlacement) {
  const auto selectedId = selectedBuildAsset != nullptr
                            ? std::optional<std::string>(selectedBuildAsset->id)
                            : std::nullopt;

  activeAssets = &cachedAssetsForStyle(cache, spriteRoot, artIni, paletteLibrary, houseColors, style);

  if (!selectedId.has_value()) {
    selectedBuildAsset = nullptr;
    return;
  }

  selectedBuildAsset = &ensureCachedAssetForStyle(cache,
                                                  spriteRoot,
                                                  artIni,
                                                  paletteLibrary,
                                                  houseColors,
                                                  style,
                                                  *selectedId);
  if (selectedBuildAsset != nullptr) {
    previewPlacement.width = selectedBuildAsset->foundation.width;
    previewPlacement.height = selectedBuildAsset->foundation.height;
  }
}

void destroyBuildingAssetCache(BuildingAssetCache& cache) {
  for (auto& [_, assets] : cache.cachedByStyle) {
    destroyBuildingAssets(assets);
  }
  cache.cachedByStyle.clear();
}
