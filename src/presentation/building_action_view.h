#pragma once
#include "building_system.h"
#include "iso_world.h"
#include <SDL.h>
#include <array>
#include <span>

namespace ra2 {
enum class BuildingActionMode { None, Repair, Sell };
struct BuildingActionVisualAssets {
  std::array<UiTexture,7> wrench;
  std::array<SDL_Cursor*,20> repair{};
  std::array<SDL_Cursor*,10> sell{};
  SDL_Cursor* repairBlocked=nullptr;
  SDL_Cursor* sellBlocked=nullptr;
};
void loadBuildingActionVisualAssets(BuildingActionVisualAssets&,const std::filesystem::path& assets);
void destroyBuildingActionVisualAssets(BuildingActionVisualAssets&);
void updateBuildingActionCursor(const BuildingActionVisualAssets&,BuildingActionMode,bool allowed,GameFrame frame);
void drawBuildingRepairs(Renderer2D&,const BuildingActionVisualAssets&,const BuildingAssetMap&,
                        std::span<const BuildingInstance>,Vec2 origin,float w,float h);
}
