#pragma once
#include "simulation/world.h"
#include "building_system.h"
#include "rhino_unit.h"
#include "weapon_system.h"
#include "unit_view.h"
#include <span>

namespace ra2 {
struct WorldView {
  std::vector<BuildingInstance> buildings;
  std::vector<EntityId> buildingIds;
  std::vector<RhinoUnitState> vehicles;
  std::vector<InfantryVisual> infantry;
  std::vector<SelectableUnit> selectables;
  std::vector<ProjectileInstance> projectiles;
  std::vector<CombatEffectInstance> effects;
  std::vector<BeamSnapshot> beams;
  std::vector<BombVisual> bombs;
};
// One-way, value-only adapter to the existing SHP/VXL rendering code.
WorldView makeWorldView(const World& world, const DefinitionRegistry& definitions, EntityId selected, std::uint32_t viewer=0);
WorldView makeWorldView(const World& world, const DefinitionRegistry& definitions, std::span<const EntityId> selected, std::uint32_t viewer=0);

[[nodiscard]] inline bool isFactoryExitVehicle(const RhinoUnitState& vehicle, const BuildingInstance& factory) {
  return factory.entityId != 0 && factory.state == BuildingState::Complete && factory.layeredFactory &&
         vehicle.exitFactoryId == factory.entityId && factory.exitingUnitId == vehicle.entityId;
}
}
