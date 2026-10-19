#pragma once
#include "unit_view.h"
#include <array>
#include <map>
#include <span>
namespace ra2 {
struct InfantryAtlas { UiTexture texture,shadow; int frameWidth=0,frameHeight=0,columns=16,rows=0,frames=0; };
struct InfantryVisualAssets {
  std::map<std::string,InfantryAtlas> atlases;
  UiTexture healthBorder,vehicleHealthBorder,veteranPip,ammoPip;
  std::array<UiTexture,3> healthPips; // Green, yellow, red; original palette colors.
  std::array<UiTexture,3> passengerPips; // Empty silhouette, white occupant, blue engineer.
  std::vector<UiTexture> bombIcons;
  int conditionRed=2500, conditionYellow=5000; // Ratios in ten-thousandths.
  bool enemyHealth=true;
};
void loadInfantryVisualAssets(InfantryVisualAssets&,const DefinitionRegistry&,const std::filesystem::path&);
void destroyInfantryVisualAssets(InfantryVisualAssets&);
void drawInfantry(Renderer2D&,const InfantryVisualAssets&,std::span<const InfantryVisual>,Vec2,float,float,Rgba,Rgba,bool arctic=false);
void drawInfantrySelection(Renderer2D&,const InfantryVisualAssets&,std::span<const InfantryVisual>,
                          Vec2,float,float,EntityId hovered=0);
EntityId hitTestInfantry(std::span<const InfantryVisual>,float,float,Vec2,float,float);
void drawIvanBombIcons(Renderer2D&,const InfantryVisualAssets&,std::span<const BombVisual>,Vec2,float,float);
void drawCargoSelection(Renderer2D&,const InfantryVisualAssets&,std::span<const EntitySnapshot>,
                        std::span<const EntityId>,Vec2,float,float);
}
