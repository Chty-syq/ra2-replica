#include "unit_selection.h"
#include <algorithm>

namespace ra2 {
SelectionRect SelectionRect::between(Vec2 a,Vec2 b) {
  return {std::min(a.x,b.x),std::min(a.y,b.y),std::max(a.x,b.x),std::max(a.y,b.y)};
}
bool SelectionRect::contains(Vec2 point) const {
  return point.x>=left && point.x<=right && point.y>=top && point.y<=bottom;
}
void SelectionGesture::begin(Vec2 point,bool additive) {
  start_=current_=point; active_=true; dragging_=false; additive_=additive;
}
void SelectionGesture::update(Vec2 point) {
  if (!active_) return;
  current_=point;
  const auto dx=point.x-start_.x,dy=point.y-start_.y;
  dragging_=dragging_ || dx*dx+dy*dy>=25;
}
std::optional<SelectionRelease> SelectionGesture::release(Vec2 point) {
  if (!active_) return std::nullopt;
  update(point);
  const SelectionRelease result{point,rectangle(),dragging_,additive_};
  cancel(); return result;
}
void SelectionGesture::cancel() { active_=dragging_=false; }
bool UnitSelection::contains(EntityId id) const { return std::binary_search(ids_.begin(),ids_.end(),id); }
void UnitSelection::selectOne(EntityId id) { ids_.clear(); if (id) ids_.push_back(id); }
void UnitSelection::prune(std::span<const EntitySnapshot> entities) {
  std::erase_if(ids_,[&](EntityId id) {
    return std::none_of(entities.begin(),entities.end(),[&](const auto& entity) { return entity.id==id && entity.hp>0 && !entity.containedBy && !entity.parasiteHost && !entity.missile && !entity.spawnedAircraft; });
  });
}
void UnitSelection::retainVehicles(std::span<const RhinoUnitState> vehicles,std::uint32_t owner) {
  std::erase_if(ids_,[&](EntityId id) {
    return std::none_of(vehicles.begin(),vehicles.end(),[&](const auto& vehicle) {
      return vehicle.entityId==id && vehicle.owner==owner && vehicle.hp>0 && vehicle.commandable;
    });
  });
}
void UnitSelection::selectBox(SelectionRect rectangle,std::span<const RhinoUnitState> vehicles,
                              Vec2 origin,float tileWidth,float tileHeight,bool additive,std::uint32_t owner) {
  if (!additive) clear(); else retainVehicles(vehicles,owner);
  for(const auto& vehicle:vehicles) {
    if (vehicle.owner!=owner || vehicle.hp<=0 || !vehicle.commandable) continue;
    auto centre=rhinoGroundAnchor(vehicle,origin,tileWidth,tileHeight);
    centre.y-=vehicle.heightLeptons*tileHeight/256.0f+tileHeight*0.3f;
    if (rectangle.contains(centre)) ids_.push_back(vehicle.entityId);
  }
  std::sort(ids_.begin(),ids_.end()); ids_.erase(std::unique(ids_.begin(),ids_.end()),ids_.end());
}
void UnitSelection::toggleVehicle(EntityId id,std::span<const RhinoUnitState> vehicles,std::uint32_t owner) {
  const auto found=std::find_if(vehicles.begin(),vehicles.end(),[&](const auto& vehicle) {
    return vehicle.entityId==id && vehicle.owner==owner && vehicle.hp>0 && vehicle.commandable;
  });
  if (found==vehicles.end()) return;
  retainVehicles(vehicles,owner);
  const auto it=std::lower_bound(ids_.begin(),ids_.end(),id);
  if (it!=ids_.end() && *it==id) ids_.erase(it); else ids_.insert(it,id);
}
void UnitSelection::selectBox(SelectionRect rectangle,std::span<const SelectableUnit> units,
                             Vec2 origin,float w,float h,bool additive,std::uint32_t owner) {
  if(!additive)clear();
  else std::erase_if(ids_,[&](auto id){return std::none_of(units.begin(),units.end(),[&](const auto& u){return u.id==id && u.owner==owner && u.hp>0;});});
  for(const auto& u:units)if(u.owner==owner && u.hp>0) {
    const Vec2 point{origin.x+(u.position.x-u.position.y)*w*.5f,
      origin.y+(u.position.x+u.position.y)*h*.5f-u.heightLeptons*h/256-h*.3f};
    if(rectangle.contains(point))ids_.push_back(u.id);
  }
  std::sort(ids_.begin(),ids_.end());ids_.erase(std::unique(ids_.begin(),ids_.end()),ids_.end());
}
void UnitSelection::toggleUnit(EntityId id,std::span<const SelectableUnit> units,std::uint32_t owner) {
  if(std::none_of(units.begin(),units.end(),[&](const auto& u){return u.id==id && u.owner==owner && u.hp>0;}))return;
  std::erase_if(ids_,[&](auto selected){return std::none_of(units.begin(),units.end(),[&](const auto& u){return u.id==selected && u.owner==owner && u.hp>0;});});
  const auto it=std::lower_bound(ids_.begin(),ids_.end(),id);if(it!=ids_.end() && *it==id)ids_.erase(it);else ids_.insert(it,id);
}
namespace {
bool accepts(const EntitySnapshot& entity,const UnitTypeDef& type,CommandKind kind) {
  if(type.missileSpawn || type.spawnedAircraft || entity.grappled)return false;
  switch(kind) {
    case CommandKind::Move: return type.groundMobile || type.flight.has_value() || type.aircraft.has_value() || type.factory=="INFANTRYTYPE";
    case CommandKind::AttackGround: if(type.leap || type.engineer || type.ivan || type.spy || type.teleporter)return false;[[fallthrough]];
    case CommandKind::AttackEntity: return !type.primary.empty();
    case CommandKind::EnterBuilding: return (type.aircraft && type.aircraft->fixedWing) || type.engineer || type.occupier || type.spy || (type.naval && type.groundMobile);
    case CommandKind::EnterTransport: return (type.infantry.has_value() || (type.groundMobile && !type.naval)) && !type.flight;
    case CommandKind::Unload: return entity.cargo.has_value();
    case CommandKind::Deploy: return entity.cargo.has_value() || !type.deploysInto.empty() || !type.undeploysInto.empty() || (type.infantry && type.infantry->deployer);
    case CommandKind::Harvest: case CommandKind::ReturnToRefinery: return entity.harvesting.has_value();
    case CommandKind::Stop: return true;
    default: return false;
  }
}
}
std::size_t issueSelectionCommand(World& world,const DefinitionRegistry& definitions,const UnitSelection& selection,
  CommandKind kind,TileCoord cell,EntityId target,std::uint64_t& sequence,std::uint32_t player) {
  const auto entities=world.snapshot();
  const auto destination=std::find_if(entities.begin(),entities.end(),[&](const auto& entity){return entity.id==target && entity.hp>0 && !entity.containedBy;});
  const auto validTarget=[&](const EntitySnapshot& entity) {
    const auto& type=definitions.unit(entity.type);
    if(kind==CommandKind::AttackEntity) {
      if(destination==entities.end() || destination->leaping || destination->parasiteHost)return false;
      const auto& victim=definitions.unit(destination->type);
      const bool water=canonicalId(world.map().cell(destination->cell).surface.land)=="WATER";
      if(!definitions.targetDomainAllowed(type,destination->airborne,water,destination->submerged))return false;
      if(victim.cloakable && !world.recognizes(destination->id,player))return false;
      if(type.canDisguise)return destination->id!=entity.id && victim.infantry.has_value();
      if(type.c4 && victim.category==TypeCategory::Building)return destination->owner!=player && victim.canC4 && !destination->c4Detonates && !destination->frozen;
      if(type.leap && !(type.leap->naval && victim.organic) && (destination->airborne || (victim.category!=TypeCategory::Infantry &&
          (type.leap->infantryOnly || victim.category!=TypeCategory::Vehicle || (type.leap->naval?(!victim.naval || victim.organic):victim.naval) || !victim.parasiteable || destination->infestedBy))))return false;
      if(destination->owner==player && definitions.chargeWeapon(type,definitions.unit(destination->type)))
        return destination->activity==Activity::Idle;
      const auto* weapon=definitions.weaponForTarget(type,destination->airborne,entity.infantry && entity.infantry->posture==InfantryPosture::Deployed,entity.cargo?entity.cargo->mode:0);
      if(!weapon)return false;
      if(destination->frozen && !weapon->temporal)return false;
      if(weapon->disarmsBomb)return destination->bomb.has_value() && !destination->airborne;
      if(weapon->plantsBomb)return destination->id!=entity.id && !destination->bomb && victim.bombable && !destination->airborne;
      if(weapon->sonic && destination->grappled)return true;
      const bool repairs=!entity.activeWeapon.empty() && definitions.weapon(entity.activeWeapon).repairs;
      return repairs?destination->id!=entity.id && destination->owner==player && definitions.unit(destination->type).category==TypeCategory::Vehicle && (destination->hp<destination->maxHp || destination->infestedBy):destination->owner!=player;
    }
    if(kind==CommandKind::EnterBuilding) {
      if(destination==entities.end())return false;
      const auto& host=definitions.unit(destination->type);
      return host.category==TypeCategory::Building && ((type.aircraft && type.aircraft->fixedWing && host.airport && destination->owner==player) || (type.naval && host.naval && host.unitRepair && destination->owner==player && entity.hp<entity.maxHp) || (type.spy && destination->owner!=player && host.spyable) || (type.engineer && (destination->owner==player?destination->hp<destination->maxHp && host.repairable:host.capturable)) ||
        (type.occupier && host.garrisonable && destination->cargo && (destination->cargo->passengers.empty() || destination->owner==player)));
    }
    if(kind==CommandKind::EnterTransport)return destination!=entities.end() && destination->owner==player && definitions.unit(destination->type).passengers>0 &&
      (type.infantry || definitions.unit(destination->type).naval || type.spriteVehicle) && (!entity.cargo || entity.cargo->passengers.empty());
    return true;
  };
  std::vector<const EntitySnapshot*> actors;
  for(const auto& entity:entities) if(selection.contains(entity.id) && entity.owner==player && entity.hp>0 && !entity.containedBy && !entity.leaping && !entity.parasiteHost && !entity.frozen &&
      accepts(entity,definitions.unit(entity.type),kind) && validTarget(entity)) actors.push_back(&entity);
  if (kind==CommandKind::Move && actors.size()>1) {
    Command command{world.tick()+1,player,++sequence,CommandKind::GroupMove,0,0,cell};
    for(const auto* actor:actors) command.members.push_back(actor->id);
    return world.enqueue(std::move(command)) ? actors.size() : 0;
  }
  std::size_t issued=0;
  for(const auto* actor:actors)
    if (world.enqueue({world.tick()+1,player,++sequence,kind,actor->id,target,cell})) ++issued;
  return issued;
}
}
