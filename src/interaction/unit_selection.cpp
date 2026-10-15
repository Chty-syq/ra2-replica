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
    return std::none_of(entities.begin(),entities.end(),[&](const auto& entity) { return entity.id==id && entity.hp>0; });
  });
}
void UnitSelection::retainVehicles(std::span<const RhinoUnitState> vehicles,std::uint32_t owner) {
  std::erase_if(ids_,[&](EntityId id) {
    return std::none_of(vehicles.begin(),vehicles.end(),[&](const auto& vehicle) {
      return vehicle.entityId==id && vehicle.owner==owner && vehicle.hp>0;
    });
  });
}
void UnitSelection::selectBox(SelectionRect rectangle,std::span<const RhinoUnitState> vehicles,
                              Vec2 origin,float tileWidth,float tileHeight,bool additive,std::uint32_t owner) {
  if (!additive) clear(); else retainVehicles(vehicles,owner);
  for(const auto& vehicle:vehicles) {
    if (vehicle.owner!=owner || vehicle.hp<=0) continue;
    auto centre=rhinoGroundAnchor(vehicle,origin,tileWidth,tileHeight);
    centre.y-=vehicle.heightLeptons*tileHeight/256.0f+tileHeight*0.3f;
    if (rectangle.contains(centre)) ids_.push_back(vehicle.entityId);
  }
  std::sort(ids_.begin(),ids_.end()); ids_.erase(std::unique(ids_.begin(),ids_.end()),ids_.end());
}
void UnitSelection::toggleVehicle(EntityId id,std::span<const RhinoUnitState> vehicles,std::uint32_t owner) {
  const auto found=std::find_if(vehicles.begin(),vehicles.end(),[&](const auto& vehicle) {
    return vehicle.entityId==id && vehicle.owner==owner && vehicle.hp>0;
  });
  if (found==vehicles.end()) return;
  retainVehicles(vehicles,owner);
  const auto it=std::lower_bound(ids_.begin(),ids_.end(),id);
  if (it!=ids_.end() && *it==id) ids_.erase(it); else ids_.insert(it,id);
}
namespace {
bool accepts(const EntitySnapshot& entity,const UnitTypeDef& type,CommandKind kind) {
  switch(kind) {
    case CommandKind::Move: return type.groundMobile;
    case CommandKind::AttackEntity: case CommandKind::AttackGround: return !type.primary.empty();
    case CommandKind::Deploy: return !type.deploysInto.empty() || !type.undeploysInto.empty();
    case CommandKind::Harvest: case CommandKind::ReturnToRefinery: return entity.harvesting.has_value();
    case CommandKind::Stop: return true;
    default: return false;
  }
}
}
std::size_t issueSelectionCommand(World& world,const DefinitionRegistry& definitions,const UnitSelection& selection,
  CommandKind kind,TileCoord cell,EntityId target,std::uint64_t& sequence,std::uint32_t player) {
  const auto entities=world.snapshot();
  if (kind==CommandKind::AttackEntity) {
    const auto enemy=std::find_if(entities.begin(),entities.end(),[&](const auto& entity) {
      return entity.id==target && entity.owner!=player && entity.hp>0;
    });
    if (enemy==entities.end()) return 0;
  }
  std::vector<const EntitySnapshot*> actors;
  for(const auto& entity:entities) if(selection.contains(entity.id) && entity.owner==player && entity.hp>0 &&
      accepts(entity,definitions.unit(entity.type),kind)) actors.push_back(&entity);
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
