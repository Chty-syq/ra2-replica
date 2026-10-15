#include "world_view.h"
#include "compat/rules_timing.h"
#include <algorithm>
#include <cmath>

namespace ra2 {
WorldView makeWorldView(const World& world, const DefinitionRegistry& definitions, EntityId selected) {
  return makeWorldView(world,definitions,std::span<const EntityId>(&selected,selected?1:0));
}
WorldView makeWorldView(const World& world, const DefinitionRegistry& definitions, std::span<const EntityId> selected) {
  WorldView result;
  constexpr auto hz = kGameFramesPerSecond;
  const auto milliseconds = gameMilliseconds;
  const auto logical = [](Position p) { return Vec2{static_cast<float>(p.x) / 1000, static_cast<float>(p.y) / 1000}; };
  constexpr float radiansPerTurnUnit = 6.28318530718f / 65536;
  const auto heading = [&](int previous, int current) {
    const auto delta = ((current - previous + 98304) % 65536) - 32768;
    return static_cast<float>(previous + delta * world.interpolation()) * radiansPerTurnUnit;
  };
  const auto elapsedSeconds = [&](Tick started) {
    return static_cast<float>(std::max(0.0, static_cast<double>(world.tick() - started) - 1.0 + world.interpolation()) / hz);
  };
  for (const auto& view : world.snapshot()) {
    const auto& type = definitions.unit(view.type);
    if (type.category == TypeCategory::Building) {
      BuildingInstance building;
      building.entityId = view.id;
      building.exitingUnitId = view.exitingUnit;
      building.factoryExitActive = view.factoryPhase == FactoryExitPhase::Preparing ||
                                   view.factoryPhase == FactoryExitPhase::Driving ||
                                   view.factoryPhase == FactoryExitPhase::Clearing;
      building.factoryCycleStartTicks = milliseconds(view.factoryCycleStarted);
      building.visualFrame = world.tick();
      building.factoryCycleStartedFrame = view.factoryCycleStarted;
      building.animationStartedFrame = view.stateStarted + view.stateDuration;
      building.animationSpeed = view.animationSpeed;
      building.productionAnimationSpeed = view.productionAnimationSpeed;
      building.assetId = type.id;
      building.placement = {view.cell, type.foundationWidth, type.foundationHeight};
      building.hp = view.hp; building.maxHp = view.maxHp;
      building.state = view.activity == Activity::Constructing ? BuildingState::Constructing :
                       view.activity == Activity::Transforming ? BuildingState::Packing : BuildingState::Complete;
      building.stateStartTicks = milliseconds(view.stateStarted);
      building.stateDurationMs = milliseconds(view.stateDuration);
      const auto profile = "Building." + type.id;
      building.layeredFactory = definitions.replica().boolean(profile, "LayeredFactory");
      building.productionSplit = definitions.replica().boolean(profile, "ProductionSplit");
      building.deferOverUnit = definitions.replica().boolean(profile, "DeferOverUnit");
      building.turretHeadingRadians = heading(view.previousTurretHeading, view.turretHeading);
      building.turretTargetHeadingRadians = building.turretHeadingRadians;
      result.buildingIds.push_back(view.id);
      result.buildings.push_back(building);
    } else {
      RhinoUnitState vehicle;
      vehicle.entityId = view.id; vehicle.typeId = view.type;
      vehicle.exitFactoryId = view.exitFactory;
      vehicle.owner = view.owner;
      const auto alpha = world.interpolation();
      vehicle.tilePosition = {static_cast<float>((view.previous.x + (view.position.x - view.previous.x) * alpha) / 1000.0),
                              static_cast<float>((view.previous.y + (view.position.y - view.previous.y) * alpha) / 1000.0)};
      vehicle.occupiedCell = view.cell;
      vehicle.moveTarget = view.destination;
      vehicle.headingRadians = heading(view.previousHeading, view.heading);
      vehicle.turretHeadingRadians = heading(view.previousTurretHeading, view.turretHeading);
      vehicle.heightLeptons=static_cast<float>(view.previousHeightLeptons+(view.heightLeptons-view.previousHeightLeptons)*alpha);
      const auto cos=std::cos(vehicle.headingRadians),sin=std::sin(vehicle.headingRadians);
      vehicle.pitchDegrees=std::atan((view.slopeX*cos+view.slopeY*sin)/256.0f)*180.0f/3.14159265359f;
      vehicle.rollDegrees=std::atan((view.slopeY*cos-view.slopeX*sin)/256.0f)*180.0f/3.14159265359f;
      vehicle.hp = view.hp; vehicle.maxHp = view.maxHp;
      vehicle.selected = std::find(selected.begin(),selected.end(),view.id)!=selected.end();
      if (view.activity == Activity::Moving) {
        vehicle.path = {view.destination};
        vehicle.waypointVisibleUntilTicks = milliseconds(world.tick()) + 250;
      }
      result.vehicles.push_back(std::move(vehicle));
    }
  }
  for (const auto& shot : world.shots()) {
    if (shot.invisible) continue;
    ProjectileInstance projectile;
    const auto alpha=world.interpolation();
    projectile.simulated=true;
    projectile.elapsedSeconds=elapsedSeconds(shot.started);
    projectile.startLogical=logical(shot.start); projectile.targetLogical=logical(shot.target);
    projectile.logicalPosition={static_cast<float>((shot.previous.x+(shot.position.x-shot.previous.x)*alpha)/1000.0),
                                static_cast<float>((shot.previous.y+(shot.position.y-shot.previous.y)*alpha)/1000.0)};
    projectile.heightLeptons=static_cast<float>(shot.previousHeightLeptons+(shot.heightLeptons-shot.previousHeightLeptons)*alpha);
    projectile.velocityLogical=logical({shot.position.x-shot.previous.x,shot.position.y-shot.previous.y});
    projectile.verticalVelocityLeptons=static_cast<float>(shot.heightLeptons-shot.previousHeightLeptons);
    result.projectiles.push_back(projectile);
  }
  for (const auto& effect : world.effects()) {
    CombatEffectInstance visual;
    visual.logicalPosition = logical(effect.position);
    visual.kind = effect.explosion ? CombatEffectKind::Explosion : CombatEffectKind::Muzzle;
    visual.heightLeptons = static_cast<float>(effect.heightLeptons);
    visual.elapsedSeconds = elapsedSeconds(effect.started);
    const auto& animation=definitions.effectAnimation(effect.animation);
    const auto delay=animation.normalized?compat::normalizedAnimationDelay(animation.frameDelay,effect.animationSpeed):animation.frameDelay;
    const auto elapsed=std::max(0.0,static_cast<double>(world.tick()-effect.started)-1+world.interpolation());
    visual.animation=animation.image;
    visual.animationFrame=animation.start+(delay>0?static_cast<int>(elapsed/delay):0);
    // Translucent=yes is the stock 50% blend; frame pacing is in game frames.
    visual.alpha=animation.translucent?0.5f:1.0f;
    const auto end=animation.end>=0?animation.end+1:animation.frameCount;
    if ((!end || visual.animationFrame<end) && elapsed<effect.duration) result.effects.push_back(visual);
  }
  return result;
}
}
