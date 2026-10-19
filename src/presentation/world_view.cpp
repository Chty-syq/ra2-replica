#include "world_view.h"
#include "compat/rules_timing.h"
#include <algorithm>
#include <cmath>

namespace ra2 {
WorldView makeWorldView(const World& world, const DefinitionRegistry& definitions, EntityId selected, std::uint32_t viewer) {
  return makeWorldView(world,definitions,std::span<const EntityId>(&selected,selected?1:0),viewer);
}
WorldView makeWorldView(const World& world, const DefinitionRegistry& definitions, std::span<const EntityId> selected, std::uint32_t viewer) {
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
  const auto entities=world.snapshot();
  for (const auto& view : entities) {
    if(view.containedBy || view.parasiteHost)continue;
    if(view.leaping && definitions.unit(view.type).leap && definitions.unit(view.type).leap->naval)continue;
    const auto& type = definitions.unit(view.type);
    if(type.cloakable && view.submerged && !world.recognizes(view.id,viewer))continue;
    if(view.bomb) {
      const auto visible=view.bomb->owner==viewer || std::any_of(entities.begin(),entities.end(),[&](const auto& detector) {
        if(detector.owner!=viewer || detector.hp<=0 || detector.containedBy || detector.parasiteHost)return false;
        const auto range=definitions.unit(detector.type).bombSight;
        const auto x=detector.position.x-view.position.x,y=detector.position.y-view.position.y;
        return range>0 && x*x+y*y<=std::int64_t(range)*range;
      });
      if(visible) {
        const auto age=world.tick()-view.bomb->planted;
        const auto duration=std::max<Tick>(1,view.bomb->detonates-view.bomb->planted);
        const int frame=int(std::min<Tick>(5,age*6/duration))*2+int((age/definitions.ivanBomb().flickerFrames)&1);
        const auto alpha=world.interpolation();
        Position center{std::int64_t(view.previous.x+(view.position.x-view.previous.x)*alpha),std::int64_t(view.previous.y+(view.position.y-view.previous.y)*alpha)};
        if(type.category==TypeCategory::Building){center.x+=(type.foundationWidth-1)*500;center.y+=(type.foundationHeight-1)*500;}
        const auto height=view.previousHeightLeptons+(view.heightLeptons-view.previousHeightLeptons)*alpha+(type.category==TypeCategory::Building?type.buildingHeight:0);
        result.bombs.push_back({view.id,logical(center),float(height),frame});
      }
    }
    if (type.category == TypeCategory::Building) {
      BuildingInstance building;
      building.entityId = view.id;
      building.owner=view.owner;building.frozen=view.frozen;
      building.powered=view.powered;building.wallConnections=view.wallConnections;building.firingStartedFrame=view.firingStarted;
      building.repairing=view.repairing;building.repairPaused=view.repairPaused;building.repairStartedFrame=view.repairStarted;
      building.selling=view.activity==Activity::Selling;
      building.garrisoned=view.cargo && !view.cargo->passengers.empty();
      building.neutral=type.garrisonable && !building.garrisoned;
      building.exitingUnitId = view.exitingUnit;
      building.factoryExitActive = view.factoryPhase == FactoryExitPhase::Preparing ||
                                   view.factoryPhase == FactoryExitPhase::Driving ||
                                   view.factoryPhase == FactoryExitPhase::Clearing;
      building.factoryCycleStartTicks = milliseconds(view.factoryCycleStarted);
      building.visualFrame = view.frozen?view.frozenSince:world.tick();
      building.factoryCycleStartedFrame = view.factoryCycleStarted;
      building.animationStartedFrame = view.stateStarted + view.stateDuration;
      building.animationSpeed = view.animationSpeed;
      building.productionAnimationSpeed = view.productionAnimationSpeed;
      building.assetId = type.id;
      building.placement = {view.cell, type.foundationWidth, type.foundationHeight};
      building.hp = view.hp; building.maxHp = view.maxHp;
      building.state = view.activity == Activity::Constructing ? BuildingState::Constructing :
                       (view.activity == Activity::Transforming || view.activity==Activity::Selling) ? BuildingState::Packing : BuildingState::Complete;
      building.stateStartTicks = milliseconds(view.stateStarted+(view.frozen?world.tick()-view.frozenSince:0));
      building.stateDurationMs = milliseconds(view.stateDuration);
      const auto profile = "Building." + type.id;
      building.layeredFactory = definitions.replica().boolean(profile, "LayeredFactory");
      building.productionSplit = definitions.replica().boolean(profile, "ProductionSplit");
      building.deferOverUnit = definitions.replica().boolean(profile, "DeferOverUnit");
      building.turretHeadingRadians = heading(view.previousTurretHeading, view.turretHeading);
      building.turretTargetHeadingRadians = building.turretHeadingRadians;
      result.buildingIds.push_back(view.id);
      result.buildings.push_back(building);
    } else if(view.infantry || type.spriteVehicle) {
      const auto foot=view.infantry.value_or(InfantrySnapshot{0,InfantryPosture::Standing,view.spriteSequence,view.spriteStarted,true});
      const auto alpha=world.interpolation();
      InfantryVisual unit;
      unit.id=view.id;unit.type=view.type;unit.owner=view.owner;
      if(type.cloakable)unit.opacity=1.f-.6f*view.cloakStage/std::max(1,definitions.rules().integer("General","CloakingStages",9));
      unit.frozen=view.frozen;unit.reforming=view.chronoReady>world.tick();unit.spyDisguised=!view.spyDisguise.empty();unit.veteran=view.veteran;
      if(!view.spyDisguise.empty() && view.owner!=viewer && !world.recognizes(view.id,viewer)){unit.type=view.spyDisguise;unit.owner=view.disguiseOwner;}
      unit.airborne=view.airborne || view.leaping;unit.leaping=view.leaping;unit.vehicleSprite=type.spriteVehicle.has_value();
      unit.groundHeightLeptons=float(world.map().heightAt(view.position.x,view.position.y));
      unit.position={float((view.previous.x+(view.position.x-view.previous.x)*alpha)/1000.),
                     float((view.previous.y+(view.position.y-view.previous.y)*alpha)/1000.)};
      unit.heightLeptons=float(view.previousHeightLeptons+(view.heightLeptons-view.previousHeightLeptons)*alpha);
      const auto& display=definitions.unit(unit.type);
      const auto sequence=unit.type!=view.type?(view.activity==Activity::Moving?"WALK":"READY"):foot.sequence;
      auto seq=(display.infantry?*display.infantry:*display.spriteVehicle).sequence(sequence);
      if(seq.normalized)seq.delay=compat::normalizedAnimationDelay(seq.delay,view.animationSpeed);
      unit.frame=seq.frame(infantryFacing(view.heading),static_cast<int>((view.frozen?view.frozenSince:world.tick())-std::min(foot.animationStarted,view.frozen?view.frozenSince:world.tick())),foot.loop);
      if(view.leaping) {
        const auto angle=view.heading*radiansPerTurnUnit;
        const auto screenAngle=std::atan2((std::sin(angle)+std::cos(angle))*.5f,std::cos(angle)-std::sin(angle));
        unit.frame=(int(std::lround((screenAngle+1.57079632679f)*32/6.28318530718f))+32)%32;
      }
      unit.hp=view.hp;unit.maxHp=view.maxHp;unit.selected=std::find(selected.begin(),selected.end(),view.id)!=selected.end();
      if(!view.leaping)result.selectables.push_back({unit.id,view.owner,unit.position,unit.heightLeptons,unit.hp});
      result.infantry.push_back(std::move(unit));
    } else {
      RhinoUnitState vehicle;
      vehicle.entityId = view.id; vehicle.typeId = view.type;
      vehicle.aircraft=type.aircraft.has_value();vehicle.airborne=view.airborne;vehicle.groundHeightLeptons=world.map().heightAt(view.position.x,view.position.y);
      vehicle.animationFrame=type.aircraft?int((view.frozen?view.frozenSince:world.tick())%1024):0;
      vehicle.exitFactoryId = view.exitFactory;
      vehicle.owner = view.owner;vehicle.frozen=view.frozen;vehicle.submerged=view.submerged;
      if(type.cloakable)vehicle.opacity=1.f-.6f*view.cloakStage/std::max(1,definitions.rules().integer("General","CloakingStages",9));
      vehicle.turretVariant=type.chargeTurrets?view.chargeTurret:view.cargo?view.cargo->turret:0;
      if(type.missileLauncher || (type.navalSpawn && type.navalSpawn->alternateBody))vehicle.turretVariant=view.launcherEmpty?1:0;
      if(view.spawnedAircraft){vehicle.commandable=false;vehicle.disguiseTargetable=view.owner!=viewer;}
      if(view.missile){vehicle.commandable=false;vehicle.disguiseTargetable=view.owner!=viewer && !view.takingOff;}
      if(view.disguised) {
        vehicle.disguiseImage=view.disguise;
        const bool own=view.owner==viewer,detected=!own && world.recognizes(view.id,viewer);
        const bool firing=world.tick()<view.disguiseFireUntil;
        const bool exposed=world.tick()<view.disguiseExposedUntil;
        // Compatibility cadence, not a measured RA2 1.006 constant. Anchor it
        // to this disguise, then resume with a tree phase after a timed reveal.
        // A global frame modulo made newly stopped tanks blink halfway through
        // a phase, and could visually extend a 15-frame shot by another phase.
        constexpr Tick treeFrames=15,vehicleFrames=15;
        const auto start=std::max({view.disguiseStarted,view.disguiseFireUntil,view.disguiseExposedUntil});
        const auto age=world.tick()>=start?world.tick()-start:0;
        const bool ownerTree=age%(treeFrames+vehicleFrames)<treeFrames;
        vehicle.disguiseVisible=!firing && !exposed && !detected && (!own || ownerTree);
        vehicle.disguiseTargetable=own || detected || firing || exposed || definitions.rules().boolean("General","AttackCursorOnDisguise");
      }
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
      if(view.missile)vehicle.pitchDegrees=(view.previousMissilePitch+(view.missilePitch-view.previousMissilePitch)*world.interpolation())*360.f/65536.f;
      vehicle.hp = view.hp; vehicle.maxHp = view.maxHp;
      vehicle.selected = std::find(selected.begin(),selected.end(),view.id)!=selected.end();
      if (view.activity == Activity::Moving) {
        vehicle.path = {view.destination};
        vehicle.waypointVisibleUntilTicks = milliseconds(world.tick()) + 250;
      }
      if(!view.missile && !view.spawnedAircraft)result.selectables.push_back({vehicle.entityId,vehicle.owner,vehicle.tilePosition,vehicle.heightLeptons,vehicle.hp});
      result.vehicles.push_back(std::move(vehicle));
    }
  }
  for(const auto& wreck:world.aircraftWrecks()) {
    RhinoUnitState vehicle;vehicle.typeId=wreck.type;vehicle.owner=wreck.owner;vehicle.tilePosition=logical(wreck.position);
    vehicle.headingRadians=wreck.heading*radiansPerTurnUnit;vehicle.turretHeadingRadians=vehicle.headingRadians;
    vehicle.heightLeptons=wreck.previousHeight+(wreck.height-wreck.previousHeight)*world.interpolation();vehicle.groundHeightLeptons=world.map().heightAt(wreck.position.x,wreck.position.y);
    vehicle.airborne=true;vehicle.commandable=vehicle.disguiseTargetable=false;vehicle.hp=1;vehicle.maxHp=1;vehicle.pitchDegrees=definitions.unit(wreck.type).aircraft->balloon?0:15;
    result.vehicles.push_back(std::move(vehicle));
  }
  for(const auto& corpse:world.infantryCorpses()) {
    InfantryVisual unit;unit.type=corpse.type;unit.owner=corpse.owner;unit.position=logical(corpse.position);
    unit.heightLeptons=corpse.falling?float(corpse.previousHeightLeptons+(corpse.heightLeptons-corpse.previousHeightLeptons)*world.interpolation()):float(corpse.heightLeptons);unit.corpse=true;
    unit.airborne=definitions.unit(corpse.type).flight.has_value();
    unit.frame=definitions.unit(corpse.type).infantry->sequence(corpse.sequence).frame(infantryFacing(corpse.heading),
      static_cast<int>(world.tick()-corpse.started),false);result.infantry.push_back(std::move(unit));
  }
  for (const auto& shot : world.shots()) {
    if (shot.invisible) continue;
    ProjectileInstance projectile;
    const auto alpha=world.interpolation();
    projectile.simulated=true;
    projectile.image=shot.image;
    projectile.elapsedSeconds=elapsedSeconds(shot.started);
    projectile.startLogical=logical(shot.start); projectile.targetLogical=logical(shot.target);
    projectile.logicalPosition={static_cast<float>((shot.previous.x+(shot.position.x-shot.previous.x)*alpha)/1000.0),
                                static_cast<float>((shot.previous.y+(shot.position.y-shot.previous.y)*alpha)/1000.0)};
    projectile.heightLeptons=static_cast<float>(shot.previousHeightLeptons+(shot.heightLeptons-shot.previousHeightLeptons)*alpha);
    projectile.velocityLogical=logical({shot.position.x-shot.previous.x,shot.position.y-shot.previous.y});
    projectile.verticalVelocityLeptons=static_cast<float>(shot.heightLeptons-shot.previousHeightLeptons);
    result.projectiles.push_back(projectile);
  }
  result.beams=world.beams();
  for(const auto& host:entities)if(host.grappled) {
    const auto parasite=std::find_if(entities.begin(),entities.end(),[&](const auto& e){return e.id==host.infestedBy;});if(parasite==entities.end())continue;
    const int direction=((host.heading+4096)/8192+3)&7;
    CombatEffectInstance visual;visual.animation="SQDG";visual.remapOwner=parasite->owner;visual.groundLayer=true;
    const auto age=world.tick()-parasite->stateStarted;// Native SQDG is three blocks of 8 directions x 10 frames, not 8 x 30.
    const int local=age<10?int(age):int((age-10)%20);
    visual.animationFrame=(age<10?0:local<10?80:160)+direction*10+local%10;
    visual.logicalPosition=logical(host.position);visual.heightLeptons=float(host.heightLeptons);result.effects.push_back(std::move(visual));
  }
  for (const auto& effect : world.effects()) {
    CombatEffectInstance visual;
    visual.logicalPosition = logical(effect.position);
    visual.kind = effect.explosion ? CombatEffectKind::Explosion : CombatEffectKind::Muzzle;
    visual.heightLeptons = static_cast<float>(effect.heightLeptons);
    // Event animations begin on their creation frame, like beams. Unlike
    // moving projectiles they have no previous pose to interpolate from;
    // subtracting a frame held frame zero twice and delayed their expiry.
    // Use discrete game frames: interpolation==1 in uncapped mode means
    // "current pose", not an extra earned animation frame.
    const auto elapsed=world.tick()-effect.started;
    visual.elapsedSeconds = static_cast<float>(elapsed)/hz;
    const auto& animation=definitions.effectAnimation(effect.animation);
    const auto delay=animation.normalized?compat::normalizedAnimationDelay(animation.frameDelay,effect.animationSpeed):animation.frameDelay;
    visual.animation=animation.image;
    visual.animationFrame=animation.frame(delay>0?static_cast<int>(elapsed/delay):0);
    // Keep original art blend, screen offset and ground layer for water effects.
    visual.alpha=1.f-animation.translucency/100.f;
    visual.yDrawOffset=animation.yDrawOffset;visual.groundLayer=animation.groundLayer;
    const auto end=animation.end>=0?animation.end+1:animation.frameCount;
    if ((!end || visual.animationFrame<end) && elapsed<effect.duration) result.effects.push_back(visual);
  }
  return result;
}
}
