#pragma once
#include "simulation/world.h"
#include "iso_world.h"
namespace ra2 {
struct SelectableUnit {
  EntityId id=0;
  std::uint32_t owner=0;
  Vec2 position;
  float heightLeptons=0;
  int hp=0;
};
struct InfantryVisual {
  EntityId id=0;
  std::string type;
  std::uint32_t owner=0;
  Vec2 position;
  float heightLeptons=0;
  int frame=0, hp=0, maxHp=1;
  bool selected=false, corpse=false;
  bool frozen=false,reforming=false,spyDisguised=false,veteran=false;
  bool airborne=false,leaping=false,vehicleSprite=false;
  float groundHeightLeptons=0,opacity=1;
};
struct BombVisual {
  EntityId target=0;
  Vec2 position;
  float heightLeptons=0;
  int frame=0;
};
}
