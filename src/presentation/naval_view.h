#pragma once
#include "renderer2d.h"
#include "iso_world.h"
#include "demo_style.h"
#include "simulation/world.h"
#include <array>
#include <span>
namespace ra2 {
struct NavalVisualAssets {std::array<std::array<UiTexture,4>,3> water,shore;};
void loadNavalVisualAssets(NavalVisualAssets&,const std::filesystem::path& assets);
void destroyNavalVisualAssets(NavalVisualAssets&);
void drawSonicWaves(Renderer2D&,std::span<const BeamSnapshot>,const DefinitionRegistry&,Tick,Vec2,float,float);
void drawNavalTerrain(Renderer2D&,const NavalVisualAssets&,TheaterStyle,const MapGrid&,Vec2,float,float,int,int);
// Explicit training-map layout; it does not change rules or generate terrain during simulation.
void initializeNavalCoast(World&,int shoreX=22,int top=0,int bottom=28,int waterRight=48);
}
