#pragma once
#include "demo_style.h"
#include "rhino_unit.h"
#include "simulation/world.h"
#include <map>
#include <span>
namespace ra2 {
struct DisguiseSprite { UiTexture body,shadow; };
struct SpecialVehicleAssets { std::array<std::map<std::string,DisguiseSprite>,3> trees; };
void loadSpecialVehicleAssets(SpecialVehicleAssets&,const DefinitionRegistry&,const std::filesystem::path& assets);
void destroySpecialVehicleAssets(SpecialVehicleAssets&);
void drawVehicleDisguise(Renderer2D&,const SpecialVehicleAssets&,TheaterStyle,const RhinoUnitState&,Vec2 origin,float w,float h);
void drawLaserBeams(Renderer2D&,std::span<const BeamSnapshot>,const DefinitionRegistry&,Tick now,
                    Vec2 origin,float w,float h,Rgba friendly,Rgba enemy);
void drawElectricBolts(Renderer2D&,std::span<const BeamSnapshot>,const DefinitionRegistry&,Tick now,
                       Vec2 origin,float w,float h,const Palette& palette);
void drawRadiationBeams(Renderer2D&,std::span<const BeamSnapshot>,const DefinitionRegistry&,Tick now,Vec2,float,float);
void drawRadiationField(Renderer2D&,const World&,const DefinitionRegistry&,Vec2,float,float);
}
