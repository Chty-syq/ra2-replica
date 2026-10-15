#pragma once
#include "simulation/world.h"
#include "building_system.h"
#include "rhino_unit.h"
#include "weapon_system.h"
#include <span>

namespace ra2 {
struct WorldView {
  std::vector<BuildingInstance> buildings;
  std::vector<EntityId> buildingIds;
  std::vector<RhinoUnitState> vehicles;
  std::vector<ProjectileInstance> projectiles;
  std::vector<CombatEffectInstance> effects;
};
// One-way, value-only adapter to the existing SHP/VXL rendering code.
WorldView makeWorldView(const World& world, const DefinitionRegistry& definitions, EntityId selected);
WorldView makeWorldView(const World& world, const DefinitionRegistry& definitions, std::span<const EntityId> selected);

[[nodiscard]] inline bool isFactoryExitVehicle(const RhinoUnitState& vehicle, const BuildingInstance& factory) {
  return factory.entityId != 0 && factory.state == BuildingState::Complete && factory.layeredFactory &&
         vehicle.exitFactoryId == factory.entityId && factory.exitingUnitId == vehicle.entityId;
}
}
