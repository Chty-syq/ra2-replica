#pragma once
#include "simulation/world.h"
#include "rhino_unit.h"
#include "presentation/unit_view.h"
#include <optional>
#include <span>

namespace ra2 {
struct SelectionRect {
  float left=0,top=0,right=0,bottom=0;
  static SelectionRect between(Vec2 a,Vec2 b);
  bool contains(Vec2 point) const;
};
struct SelectionRelease { Vec2 point; SelectionRect rectangle; bool dragged=false,additive=false; };
class SelectionGesture {
public:
  void begin(Vec2 point,bool additive=false);
  void update(Vec2 point);
  std::optional<SelectionRelease> release(Vec2 point);
  void cancel();
  bool active() const { return active_; }
  bool dragging() const { return dragging_; }
  SelectionRect rectangle() const { return SelectionRect::between(start_,current_); }
private:
  Vec2 start_{},current_{};
  bool active_=false,dragging_=false,additive_=false;
};

class UnitSelection {
public:
  EntityId primary() const { return ids_.empty()?0:ids_.front(); }
  bool contains(EntityId id) const;
  std::span<const EntityId> ids() const { return ids_; }
  std::size_t size() const { return ids_.size(); }
  void clear() { ids_.clear(); }
  void selectOne(EntityId id);
  void prune(std::span<const EntitySnapshot> entities);
  void selectBox(SelectionRect rectangle,std::span<const RhinoUnitState> vehicles,
                 Vec2 origin,float tileWidth,float tileHeight,bool additive=false,std::uint32_t owner=0);
  void selectBox(SelectionRect rectangle,std::span<const SelectableUnit> units,
                 Vec2 origin,float tileWidth,float tileHeight,bool additive=false,std::uint32_t owner=0);
  void toggleUnit(EntityId id,std::span<const SelectableUnit> units,std::uint32_t owner=0);
  void toggleVehicle(EntityId id,std::span<const RhinoUnitState> vehicles,std::uint32_t owner=0);
private:
  void retainVehicles(std::span<const RhinoUnitState> vehicles,std::uint32_t owner);
  std::vector<EntityId> ids_;
};

// Group movement submits one intent for authoritative matching in World.
// Other orders share an execution frame and use distinct sequence numbers.
std::size_t issueSelectionCommand(World& world,const DefinitionRegistry& definitions,const UnitSelection& selection,
  CommandKind kind,TileCoord cell,EntityId target,std::uint64_t& sequence,std::uint32_t player=0);
}
