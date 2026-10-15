#pragma once
#include "iso_world.h"
#include "simulation/world.h"
#include "demo_style.h"
#include <array>

namespace ra2 {
struct ResourceVisualAssets {
  // Temperate, snow, urban; each has original ore and gem index atlases.
  std::array<std::array<UiTexture, 2>, 3> overlays;
  UiTexture gathering;
};
void loadResourceVisualAssets(ResourceVisualAssets& assets, const std::filesystem::path& assetRoot);
void destroyResourceVisualAssets(ResourceVisualAssets& assets);
void drawResources(Renderer2D& renderer, const ResourceVisualAssets& assets, TheaterStyle theater,
                   const World& world, const DefinitionRegistry& definitions,
                   Vec2 origin, float tileWidth, float tileHeight);
void drawGathering(Renderer2D& renderer, const ResourceVisualAssets& assets,
                   const World& world, const DefinitionRegistry& definitions,
                   Vec2 origin, float tileWidth, float tileHeight);
void drawHarvesterCargo(Renderer2D& renderer, const World& world, EntityId selected,
                        Vec2 origin, float tileWidth, float tileHeight);
}
