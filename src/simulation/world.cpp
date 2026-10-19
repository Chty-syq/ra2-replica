#include "world.h"
#include "leap.h"
#include "kinematics.h"
#include "traffic.h"
#include "flight.h"
#include "missile.h"
#include "naval_aircraft.h"
#include "aircraft.h"
#include "compat/rules_timing.h"
#include "foundation.h"
#include "pathfinding.h"
#include <entt/entt.hpp>
#include <algorithm>
#include <array>
#include <deque>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace ra2 {
namespace {
using CellKey = std::pair<int, int>;
CellKey key(TileCoord cell) { return {cell.x, cell.y}; }
Position position(TileCoord cell) { return {static_cast<std::int64_t>(cell.x) * 1000, static_cast<std::int64_t>(cell.y) * 1000}; }
// Bounded scenario coordinates make squared distances and footprint offsets safe.
bool validCell(TileCoord cell) { return cell.x >= -100000 && cell.x <= 100000 && cell.y >= -100000 && cell.y <= 100000; }
std::int64_t square(std::int64_t value) { return value * value; }
std::int64_t distanceSquared(Position a, Position b) { return square(a.x - b.x) + square(a.y - b.y); }
std::int64_t integerSqrt(std::int64_t value) {
  std::uint64_t n = static_cast<std::uint64_t>(value), result = 0, bit = 1ULL << 62;
  while (bit > n) bit >>= 2;
  while (bit) { if (n >= result + bit) { n -= result + bit; result = (result >> 1) + bit; } else result >>= 1; bit >>= 2; }
  return static_cast<std::int64_t>(result);
}
int direction(Position delta) {
  return motion::direction(delta.x,delta.y);
}
TileCoord cellAt(Position p) {
  const auto nearest=[](std::int64_t n) { return static_cast<int>((n>=-500?n+500:n-499)/1000); };
  return {nearest(p.x),nearest(p.y)};
}
motion::Vector fixed(Position p, int height=0) { return {p.x*motion::scale,p.y*motion::scale,height*motion::lepton}; }
Position planar(motion::Vector p) { return {p.x/motion::scale,p.y/motion::scale}; }
struct Rotation {
  int target = 0, delta = 0, duration = 0;
  Tick started = 0;
  void reset(int heading) { target = heading; delta = duration = 0; started = 0; }
  int update(int current, int desired, int rot, Tick now) {
    // Finish this frame's progress toward the old bearing before retargeting.
    // Otherwise a moving target that changes bearing every tick freezes a turret.
    if (target!=desired && duration>0 && now>started) {
      const int remaining=std::max(0,duration-static_cast<int>(now-started));
      current=(target-remaining*delta/duration+65536)%65536;
    }
    if (target != desired || (duration == 0 && current != desired)) {
      target = desired;
      delta = ((desired-current+98304) % 65536)-32768;
      duration = compat::rotationFrames(delta,rot);
      started = now;
    }
    const auto elapsed = now-started;
    if (duration == 0 || elapsed >= static_cast<Tick>(duration)) return target;
    const int remaining = duration-static_cast<int>(elapsed);
    return (target-remaining*delta/duration+65536) % 65536;
  }
};
struct Identity { EntityId id; std::uint32_t owner; const UnitTypeDef* type; };
struct Health { int hp; };
// Resolved before entity removal; also prevents a chain explosion from firing
// a second death weapon or leaving a walking corpse of a suicide attacker.
struct DeathResolution { bool detonated=false; };
using AttachedBomb=BombSnapshot;
struct C4Charge {Tick detonates=0;};
struct SpyDisguise {std::string type;std::uint32_t owner=0;};
struct ChronoRecovery {Tick ready=0;};
struct TemporalVictim {int remaining=0;Tick started=0;};
struct TemporalChannel {EntityId target=0;std::string weapon;};
struct Erased {};
struct Veteran {};
struct Submerge {Tick surfacedUntil=0;int stage=0;};
struct ShipyardRepair {EntityId dock=0;Tick next=0;};
struct Transform {
  Position previous, current; TileCoord cell;
  int heading = 0, turret = 0, previousHeading = 0, previousTurret = 0;
  Rotation bodyTurn{}, turretTurn{};
  GameSpeed animationSpeed{};
};
struct Mobile {
  TileCoord destination{};
  std::deque<TileCoord> path;
  std::optional<TileCoord> reserved;
  std::int64_t segmentProgress = 0, speedRemainder = 0;
  motion::DriveSegment track;
  bool driving = false;
  std::vector<TileCoord> sweptReservations;
  bool requested = false, exactTarget = false;
  Tick retry = 0;
  // Player intent survives temporary yielding; path may lead to a passing bay.
  bool cooperative = false, yielding = false;
  EntityId yieldFor = 0;
  EntityId blockedBy = 0;
  TileCoord yieldOrigin{};
  Tick waitSince = 0, yieldUntil = 0;
  bool laneWaiting=false;
  std::uint64_t pendingGroup=0;
  std::uint64_t flowGroup=0;
};
struct Infantry {
  int subCell=0, targetSubCell=-1, death=1;
  TileCoord destination{}, next{};
  std::deque<TileCoord> path;
  bool requested=false, firing=false, fired=false;
  Position segmentStart{};
  std::int64_t progress=0, length=0, remainder=0;
  Tick retry=0, postureUntil=0, proneUntil=0, fireAt=0, attackUntil=0;
  InfantryPosture posture=InfantryPosture::Standing;
  std::string sequence="READY";
  Tick animationStarted=0;
  bool loop=true;
};
using Airborne=flight::State;
struct Camouflage {
  std::string image;
  Tick started=0,visibleUntil=0,exposedUntil=0;
  bool active=true;
};
struct BuildingInteraction {
  EntityId target=0;
  std::uint32_t targetOwner=0;
  BuildingInteractionKind kind=BuildingInteractionKind::Capture;
  TileCoord approach{};
  Position entryStart{},entryEnd{};
  Tick started=0,duration=0,retry=0;
  bool planned=false,entering=false,consumed=false;
};
struct Cargo {
  std::vector<EntityId> passengers;
  std::uint32_t originalOwner=0;
  bool unloading=false;
  Tick retry=0;
};
struct Contained { EntityId host=0; };
struct FlowProfile { const UnitTypeDef* type; traffic::FlowField field; };
struct FlowGroup {
  std::vector<EntityId> members;
  TileCoord center{},minimum{},maximum{};
  int dockingRadius=0;
  std::uint64_t topology=0;
  std::map<std::string,FlowProfile> profiles;
};
struct FuturePose { Position position; TileCoord cell; };
bool motionConflict(FuturePose a,FuturePose b,FuturePose aNext,FuturePose bNext) {
  if(aNext.cell==bNext.cell) return true;
  const Position r{a.position.x-b.position.x,a.position.y-b.position.y},
    v{aNext.position.x-a.position.x-bNext.position.x+b.position.x,
      aNext.position.y-a.position.y-bNext.position.y+b.position.y};
  const auto length=square(v.x)+square(v.y);
  const auto fraction=length?std::clamp<std::int64_t>(-(r.x*v.x+r.y*v.y)*1024/length,0,1024):0;
  return square(r.x+v.x*fraction/1024)+square(r.y+v.y*fraction/1024)<square(500);
}
struct TrafficLane { std::vector<TileCoord> cells; int direction=0; bool draining=false; int admitted=0; Tick approachUntil=0; };
struct FormationJob {
  Command command;
  std::vector<EntityId> actors;
  std::vector<TileCoord> slots;
  std::vector<std::vector<traffic::Cost>> costs;
  TileCoord sourceCenter{};
};
struct Armament { bool active = false; EntityId target = 0; Position ground; Tick nextShot = 0; bool automatic = false; int burstRemaining=0; Tick burstNext=0; std::string weapon;EntityId windupTarget=0;Tick firedAt=0,chargeStarted=0,chargeUntil=0;bool autoChargeSuppressed=false; };
struct Infested {EntityId parasite=0;};
struct SpriteAnimation {bool moving=false;Tick started=0;};
struct CoilCharger { EntityId target=0; };
struct MissileLauncher { EntityId child=0;Tick ready=0; };
struct SpawnedMissile { EntityId parent=0,target=0;missile::State flight;bool detonated=false,cancelled=false; };
struct NavalSpawner {std::vector<NavalSpawnSlot> slots;EntityId target=0;Tick launchNext=0;Position ground;bool groundFire=false;};
struct NavalAircraft {EntityId parent=0,target=0;int slot=0;NavalSpawnPhase phase=NavalSpawnPhase::Attacking;naval_aircraft::State flight;bool recovered=false;Tick launched=0;Position ground;bool groundFire=false;};
struct Pilot {aircraft::State flight;AircraftMission mission=AircraftMission::Landed;Position destination;EntityId airport=0;int slot=-1,ammo=-1,reload=0;Tick repairNext=0;};
struct SonicWave {Position start,end;EntityId source=0,target=0;std::uint32_t owner=0;Tick started=0;std::int64_t travelled=0;std::string sourceType;bool directDelivered=false;};
struct Structure { Tick started = 0, complete = 0; };
struct BuildingRepair { std::uint32_t owner=0;Tick started=0,next=0;bool paused=false; };
struct BuildingSale { std::uint32_t owner=0;Tick started=0,complete=0; };
struct ProductionItem { std::string type; int remaining = 0, total = 0, paid = 0; };
struct Factory {
  std::deque<ProductionItem> queue;
  EntityId exitingUnit = 0;
  FactoryExitPhase phase = FactoryExitPhase::Idle;
  Tick phaseStarted = 0, cycleStarted = 0;
  GameSpeed animationSpeed{};
  std::optional<TileCoord> rally;
};
struct FactoryExit {
  EntityId factory;
  std::vector<TileCoord> route;
  std::optional<Command> deferred;
};
struct Deploy { const UnitTypeDef* target; Tick started = 0, complete = 0; TileCoord cell{}; };
struct Shot {
  ShotSnapshot view; const WeaponDef* weapon; EntityId source; std::uint32_t owner;
  motion::Vector location, velocity;
  EntityId targetEntity = 0;
  bool scatter=false;
  int firepower=1000;
};
}
struct World::Impl : HarvestHost {
  explicit Impl(const DefinitionRegistry& defs) : definitions(defs), harvesting(defs), clock(defs.profile().defaultSpeed) {}
  const DefinitionRegistry& definitions;
  HarvestSystem harvesting;
  GameClock clock;
  bool presentCurrentFrame = false;
  entt::registry registry;
  std::map<EntityId, entt::entity> entities;
  // Buildings retain their footprint while an authorized vehicle crosses it.
  // Other vehicles/path searches see the union, never an emptied factory mask.
  std::map<CellKey, EntityId> structures, occupied, reservations;
  using FootSlots=std::array<EntityId,3>;
  std::map<CellKey,FootSlots> footOccupied, footReservations;
  std::map<CellKey,FootSlots> airOccupied, airReservations;
  std::vector<InfantryCorpse> corpses;
  std::vector<AircraftWreck> airWrecks;
  std::vector<BeamSnapshot> beams;
  std::vector<SonicWave> sonicWaves;
  radiation::Field radiationField;
  std::set<CellKey> blockedTerrain;
  std::map<CellKey,TerrainSurface> terrain;
  MapGrid grid{64, 64};
  std::map<std::uint32_t, int> money;
  std::map<std::uint32_t,Tick> outages;
  std::map<std::uint32_t,EspionageSnapshot> espionage;
  std::map<std::uint32_t, std::uint64_t> sequences;
  std::vector<Command> commands;
  std::vector<Shot> projectiles;
  std::vector<EffectSnapshot> effects;
  Tick now = 0;
  EntityId nextId = 1;
  std::uint64_t nextShot = 1;
  std::uint64_t scatterState=0x8c032fc1;
  std::string message;
  std::map<CellKey,TrafficLane> trafficLanes;
  std::deque<FormationJob> formations;
  std::map<std::uint64_t,FlowGroup> flowGroups;
  std::uint64_t nextFlowGroup=1;
  // Rebuilt from the current poses/paths before every movement frame. These are
  // predictions, not authoritative future occupancy or persistent reservations.
  std::map<EntityId,std::vector<FuturePose>> forecasts;
  std::map<EntityId,EntityId> forecastHolds;
  std::set<EntityId> laneApproachHolds;
  // Derived from immutable definitions; excluded from replay state. Avoid
  // repeatedly normalising the same land/speed strings in search hot loops.
  mutable std::map<const UnitTypeDef*,std::map<std::string,int,std::less<>>> terrainMultipliers;
  MovementDiagnostics movementStats;
  std::size_t assignmentBudget=8192;

  int& funds(std::uint32_t player) { return money.try_emplace(player, definitions.profile().startingCredits).first->second; }
  PowerSnapshot power(std::uint32_t owner) const {
    PowerSnapshot result;
    if(const auto it=outages.find(owner);it!=outages.end() && it->second>now)result.outageUntil=it->second;
    for(const auto& [id,entity]:entities) {
      (void)id;const auto& unit=registry.get<Identity>(entity);const int hp=registry.get<Health>(entity).hp;
      if(unit.owner!=owner || unit.type->category!=TypeCategory::Building || hp<=0 || !complete(entity))continue;
      if(unit.type->power>0)result.produced+=int(std::int64_t(unit.type->power)*hp/unit.type->strength);
      else result.consumed-=unit.type->power;
    }
    return result;
  }
  void refresh(TileCoord cell) {
    auto surface=terrain.contains(key(cell))?terrain.at(key(cell)):TerrainSurface{};
    if(const auto s=structures.find(key(cell));s!=structures.end())if(const auto e=entities.find(s->second);e!=entities.end())
      if(registry.get<Identity>(e->second).type->wall)surface.wallHeightLeptons=std::max(surface.wallHeightLeptons,128);
    grid.setSurface(cell,surface);
    grid.setOccupied(std::vector<TileCoord>{cell},surface.wallHeightLeptons>0 || surface.cliff ||
      structures.contains(key(cell)) || occupied.contains(key(cell)) || reservations.contains(key(cell)) || hasFoot(cell));
  }
  bool solidFree(TileCoord cell, EntityId self = 0, EntityId passage = 0) const {
    if (!validCell(cell) || !grid.cell(cell).buildable || grid.cell(cell).surface.wallHeightLeptons>0 || grid.cell(cell).surface.cliff) return false;
    const auto occupiedIt = occupied.find(key(cell)), reservedIt = reservations.find(key(cell));
    const auto structureIt = structures.find(key(cell));
    return (structureIt == structures.end() || structureIt->second == self || structureIt->second == passage) &&
           (occupiedIt == occupied.end() || occupiedIt->second == self) && (reservedIt == reservations.end() || reservedIt->second == self);
  }
  bool hasFoot(TileCoord cell) const { return footOccupied.contains(key(cell)) || footReservations.contains(key(cell)); }
  bool canCrush(EntityId crusher,EntityId victim) const {
    const auto a=entities.find(crusher), b=entities.find(victim);
    if(a==entities.end() || b==entities.end()) return false;
    const auto& from=registry.get<Identity>(a->second); const auto& to=registry.get<Identity>(b->second);
    const auto* foot=registry.try_get<Infantry>(b->second);
    return !frozen(b->second) && !limbo(b->second) && from.type->crusher && foot && !registry.any_of<Contained,Airborne>(b->second) && to.owner!=from.owner && to.type->infantry->crushable &&
      (foot->posture!=InfantryPosture::Deployed || to.type->infantry->deployedCrushable);
  }
  bool footBlocks(TileCoord cell,EntityId self=0) const {
    for(const auto* slots:{&footOccupied,&footReservations}) if(const auto it=slots->find(key(cell));it!=slots->end())
      for(const auto id:it->second) if(id && id!=self && !canCrush(self,id)) return true;
    return false;
  }
  bool free(TileCoord cell,EntityId self=0,EntityId passage=0) const {
    return solidFree(cell,self,passage) && !footBlocks(cell,self);
  }
  int availableFootSlot(TileCoord cell,EntityId self,std::uint32_t owner,int preferred=-1) const {
    if(!solidFree(cell,self)) return -1;
    FootSlots used{};
    for(const auto* slots:{&footOccupied,&footReservations}) if(const auto it=slots->find(key(cell));it!=slots->end())
      for(int i=0;i<3;++i) if(const auto id=it->second[i];id && id!=self) {
        if(registry.get<Identity>(entities.at(id)).owner!=owner) return -1;
        used[i]=id;
      }
    if(preferred>=0 && !used[preferred]) return preferred;
    for(int i=0;i<3;++i) if(!used[i]) return i;
    return -1;
  }
  static Position footPosition(TileCoord cell,int subCell) {
    // Three fixed-point anchors. Exact vanilla offsets remain a calibration item.
    constexpr std::array<Position,3> offsets{{{-250,-250},{-250,250},{250,-250}}};
    const auto center=position(cell); const auto offset=offsets.at(subCell);
    return {center.x+offset.x,center.y+offset.y};
  }
  int availableAirSlot(TileCoord cell,EntityId self,int preferred=-1) const {
    if(!validCell(cell))return -1;FootSlots used{};
    for(const auto* slots:{&airOccupied,&airReservations})if(const auto it=slots->find(key(cell));it!=slots->end())
      for(int i=0;i<3;++i)if(it->second[i] && it->second[i]!=self)used[i]=it->second[i];
    if(preferred>=0 && !used[preferred])return preferred;
    for(int i=0;i<3;++i)if(!used[i])return i;
    return -1;
  }
  void releaseAir(EntityId id,bool occupiedToo=true) {
    for(auto* slots:{&airOccupied,&airReservations}) {
      if(!occupiedToo && slots==&airOccupied)continue;
      for(auto it=slots->begin();it!=slots->end();) {
        for(auto& owner:it->second)if(owner==id)owner=0;
        if(std::all_of(it->second.begin(),it->second.end(),[](auto owner){return !owner;}))it=slots->erase(it);else ++it;
      }
    }
  }
  bool limbo(entt::entity entity) const {
    const auto* l=registry.try_get<leap::State>(entity);return l && l->phase!=leap::Phase::Ground;
  }
  EntityId infestor(EntityId victim) const {
    const auto host=entities.find(victim);if(host==entities.end())return 0;
    const auto* link=registry.try_get<Infested>(host->second);if(!link)return 0;
    const auto actor=entities.find(link->parasite);if(actor==entities.end() || registry.get<Health>(actor->second).hp<=0)return 0;
    const auto* state=registry.try_get<leap::State>(actor->second);
    return state && state->phase==leap::Phase::Infesting && state->victim==victim?link->parasite:0;
  }
  bool grappled(entt::entity entity) const {
    const auto id=infestor(registry.get<Identity>(entity).id);return id && registry.get<Identity>(entities.at(id)).type->leap->naval;
  }
  bool leapTarget(entt::entity source,entt::entity target) const {
    const auto& from=registry.get<Identity>(source);const auto& to=registry.get<Identity>(target);
    if(!from.type->leap || frozen(target) || limbo(target) || from.owner==to.owner || airTarget(target) || registry.any_of<Contained,FactoryExit>(target) || registry.get<Health>(target).hp<=0)return false;
    if(from.type->leap->naval)return to.type->category==TypeCategory::Vehicle && to.type->naval && !to.type->organic && to.type->parasiteable && waterAt(registry.get<Transform>(target).cell) && !infestor(to.id);
    if(to.type->category==TypeCategory::Infantry)return true;
    return !from.type->leap->infantryOnly && to.type->category==TypeCategory::Vehicle && !to.type->naval && to.type->parasiteable && !infestor(to.id);
  }
  int height(entt::entity entity) const {
    if(const auto* l=registry.try_get<leap::State>(entity);l && l->phase==leap::Phase::Flying)return static_cast<int>(l->position.z/motion::lepton);
    if(const auto* pilot=registry.try_get<Pilot>(entity))return int(pilot->flight.position.z/motion::lepton);
    if(const auto* plane=registry.try_get<NavalAircraft>(entity))return static_cast<int>(plane->flight.position.z/motion::lepton);
    if(const auto* rocket=registry.try_get<SpawnedMissile>(entity))return static_cast<int>(rocket->flight.position.z/motion::lepton);
    if(const auto* air=registry.try_get<Airborne>(entity))return air->height;
    const auto& p=registry.get<Transform>(entity).current;return grid.heightAt(p.x,p.y);
  }
  bool airTarget(entt::entity entity) const {
    if(const auto* pilot=registry.try_get<Pilot>(entity))return pilot->mission!=AircraftMission::Landed && height(entity)>grid.heightAt(registry.get<Transform>(entity).current.x,registry.get<Transform>(entity).current.y)+32;
    return registry.any_of<Airborne,SpawnedMissile,NavalAircraft>(entity);
  }
  void releaseFoot(EntityId id,bool includeOccupied=true) {
    std::set<CellKey> changed;
    for(auto* slots:{&footOccupied,&footReservations}) {
      if(!includeOccupied && slots==&footOccupied) continue;
      for(auto it=slots->begin();it!=slots->end();) {
        for(auto& owner:it->second) if(owner==id) {owner=0;changed.insert(it->first);}
        if(std::all_of(it->second.begin(),it->second.end(),[](auto owner){return !owner;})) it=slots->erase(it); else ++it;
      }
    }
    for(const auto c:changed) refresh({c.first,c.second});
  }
  void footAnimation(Infantry& foot,std::string sequence,bool loop=true) {
    if(foot.sequence!=sequence || foot.loop!=loop) {foot.sequence=std::move(sequence);foot.animationStarted=now;foot.loop=loop;}
  }
  int footDuration(const Identity& identity,std::string_view name) const {
    const auto& seq=identity.type->infantry->sequence(name); return std::max(1,seq.count*seq.delay);
  }
  void haltFoot(entt::entity entity,bool cancelAttack=true) {
    auto* foot=registry.try_get<Infantry>(entity); if(!foot) return;
    foot->requested=false;
    // Finish a committed stride, then stop at its reserved anchor without teleporting.
    if(foot->targetSubCell>=0) foot->path={foot->next}; else foot->path.clear();
    if(cancelAttack) {foot->firing=false;foot->fired=false;foot->attackUntil=0;}
  }
  std::vector<TileCoord> footprint(const UnitTypeDef& type, TileCoord cell) const {
    if (type.category != TypeCategory::Building) return {cell};
    return foundationTiles({cell, type.foundationWidth, type.foundationHeight});
  }
  void release(EntityId id) {
    releaseAir(id);
    releaseFoot(id);
    std::set<CellKey> changed;
    for (auto* cells : {&structures, &occupied, &reservations}) {
      for (auto it = cells->begin(); it != cells->end();) {
        if (it->second == id) { changed.insert(it->first); it = cells->erase(it); }
        else ++it;
      }
    }
    // Parallel segments may share clearance cells. Removing the representative
    // owner must not expose another moving vehicle's sweep to construction.
    for(const auto& [peer,entity]:entities) if(peer!=id)
      if(const auto* m=registry.try_get<Mobile>(entity)) for(const auto cell:m->sweptReservations)
        if(changed.contains(key(cell))) reservations.try_emplace(key(cell),peer);
    for(const auto& cell:changed) refresh({cell.first,cell.second});
  }
  bool playable(const UnitTypeDef& type) {
    if (type.strength < 1 || type.strength > 10000000 || type.cost < 0 || type.cost > 10000000) return false;
    if (type.category == TypeCategory::Building) return true;
    if (!type.unsupported.empty()) { message = type.id + ": unsupported " + type.unsupported.front(); return false; }
    return type.groundMobile || type.flight.has_value() || type.aircraft.has_value();
  }
  void components(entt::entity entity, const UnitTypeDef& type, TileCoord cell, bool constructing) {
    registry.remove<Mobile, Infantry, Armament, Structure, Factory, Deploy, FactoryExit, BuildingInteraction, Cargo, Contained, Airborne, Camouflage, BuildingRepair, BuildingSale,MissileLauncher,SpawnedMissile,CoilCharger,leap::State,SpriteAnimation,Infested,Submerge,ShipyardRepair,NavalSpawner,NavalAircraft,Pilot>(entity);
    if(type.infantry) registry.emplace<Infantry>(entity).destination=cell;
    else if (type.groundMobile) registry.emplace<Mobile>(entity, cell);
    if(type.flight) {auto& air=registry.emplace<Airborne>(entity);air.destination=cell;air.height=air.previousHeight=grid.heightAt(position(cell).x,position(cell).y);}
    if(type.aircraft) {
      auto& pilot=registry.emplace<Pilot>(entity);pilot.destination=position(cell);pilot.ammo=type.aircraft->ammo;
      pilot.flight.position=pilot.flight.previous=fixed(position(cell),grid.heightAt(position(cell).x,position(cell).y));
      if(type.aircraft->balloon)pilot.mission=AircraftMission::Holding;
    }
    if(type.cloakable)registry.emplace<Submerge>(entity,now+std::max(1,int(definitions.rules().number("General","CloakDelay",.02)*900)),0);
    if(type.missileLauncher)registry.emplace<MissileLauncher>(entity);
    if(type.navalSpawn)registry.emplace<NavalSpawner>(entity).slots.resize(type.navalSpawn->count);
    if(type.leap)registry.emplace<leap::State>(entity);
    if(type.spriteVehicle)registry.emplace<SpriteAnimation>(entity).started=now;
    if(type.disguiseWhenStill && !type.disguises.empty()) {
      const auto id=registry.get<Identity>(entity).id;
      auto& disguise=registry.emplace<Camouflage>(entity);
      disguise.image=type.disguises[(id*2654435761u)%type.disguises.size()];
      disguise.started=now;
    }
    if ((type.gunner && !type.weaponSlots.empty()) || (!type.primary.empty() && definitions.rules().contains(type.primary) && definitions.weapon(type.primary).supported)) registry.emplace<Armament>(entity);
    if(type.passengers>0 || (type.garrisonable && type.maxOccupants>0))registry.emplace<Cargo>(entity).originalOwner=registry.get<Identity>(entity).owner;
    if (type.category == TypeCategory::Building) {
      registry.emplace<Structure>(entity, now, constructing && !type.wall ? now + definitions.profile().buildupFrames : now);
      if (!type.factory.empty()) registry.emplace<Factory>(entity);
    }
  }
  EntityId spawn(const UnitTypeDef& type, TileCoord cell, std::uint32_t owner, bool constructing, EntityId passage = 0) {
    if (!validCell(cell) || !playable(type)) return 0;
    if(type.category==TypeCategory::Building && !buildingPlacement(type,cell)){message="Building terrain/footprint is invalid";return 0;}
    if(type.groundMobile && !type.infantry && travelSpeed(type,cell,cell)<=0){message="Unit cannot enter this terrain";return 0;}
    const auto cells = footprint(type, cell);
    const int footSlot=type.flight?availableAirSlot(cell,0):type.infantry?availableFootSlot(cell,0,owner):-1;
    if (!type.aircraft && (type.infantry ? (footSlot<0 || (!type.flight && travelSpeed(type,cell,cell)<=0)) : !std::all_of(cells.begin(), cells.end(), [&](auto c) { return free(c, 0, passage); }))) { message = "Placement blocked"; return 0; }
    const auto entity = registry.create();
    const auto id = nextId++;
    registry.emplace<Identity>(entity, id, owner, &type);
    registry.emplace<Health>(entity, type.strength);
    registry.emplace<Transform>(entity, position(cell), position(cell), cell).animationSpeed = clock.speed();
    components(entity, type, cell, constructing);
    entities.emplace(id, entity);
    if(type.infantry) {
      registry.get<Infantry>(entity).subCell=footSlot;
      auto& transform=registry.get<Transform>(entity); transform.previous=transform.current=footPosition(cell,footSlot);
      if(type.flight) {registry.get<Airborne>(entity).subCell=footSlot;airOccupied[key(cell)][footSlot]=id;}
      else {footOccupied[key(cell)][footSlot]=id; refresh(cell);}
    } else if(!type.aircraft) {
      auto& owners = type.category == TypeCategory::Building ? structures : occupied;
      for (const auto c : cells) { owners[key(c)] = id; refresh(c); }
    }
    if(type.aircraft && type.aircraft->fixedWing) {
      if(!assignAirport(entity)){registry.destroy(entity);entities.erase(id);message="No free compatible airport pad";return 0;}
      auto& pilot=registry.get<Pilot>(entity);const auto at=dockPosition(pilot.airport,pilot.slot);pilot.flight.position=pilot.flight.previous=at;
      pilot.flight.heading=((definitions.rules().integer("AudioVisual","PoseDir",2)+5)&7)*8192;
      auto& pose=registry.get<Transform>(entity);pose.current=pose.previous=planar(at);pose.cell=cellAt(pose.current);
      pose.heading=pose.turret=pose.previousHeading=pose.previousTurret=pilot.flight.heading;
    }
    if(type.aircraft && !type.aircraft->fixedWing && !type.aircraft->balloon) {
      if(!free(cell,id,passage) || waterAt(cell)){registry.destroy(entity);entities.erase(id);message="Helicopter requires a clear land tile";return 0;}
      occupied[key(cell)]=id;refresh(cell);
    }
    harvesting.add(id,type);
    return id;
  }
  bool frozen(entt::entity entity) const {return registry.all_of<TemporalVictim>(entity);}
  bool recovering(entt::entity entity) const {const auto* c=registry.try_get<ChronoRecovery>(entity);return c && now<c->ready;}
  bool swimming(entt::entity entity) const {
    const auto& identity=registry.get<Identity>(entity);
    return identity.type->infantry && identity.type->speedType=="AMPHIBIOUS" && canonicalId(grid.cell(registry.get<Transform>(entity).cell).surface.land)=="WATER";
  }
  bool veteranAbility(entt::entity entity,std::string_view ability) const {
    const auto& abilities=registry.get<Identity>(entity).type->veteranAbilities;
    return registry.all_of<Veteran>(entity) && std::find(abilities.begin(),abilities.end(),ability)!=abilities.end();
  }
  int veteranRof(entt::entity entity,const WeaponDef& weapon) const {
    return veteranAbility(entity,"ROF")?std::max(1,int(weapon.rof*definitions.rules().number("General","VeteranROF",.6))):weapon.rof;
  }
  bool waterAt(TileCoord cell) const {return canonicalId(grid.cell(cell).surface.land)=="WATER";}
  bool submerged(entt::entity entity) const {
    const auto* sub=registry.try_get<Submerge>(entity);
    return sub && registry.get<Identity>(entity).type->underwater && sub->stage>=definitions.rules().integer("General","CloakingStages",9);
  }
  bool buildingPlacement(const UnitTypeDef& type,TileCoord anchor) const {
    if(type.category!=TypeCategory::Building)return false;
    const auto cells=footprint(type,anchor);
    return std::all_of(cells.begin(),cells.end(),[&](auto cell){return free(cell) && definitions.buildingTerrainAllowed(type,grid.cell(cell).surface.land);});
  }
  bool complete(entt::entity entity) const {
    const auto* structure = registry.try_get<Structure>(entity);
    return !frozen(entity) && !registry.any_of<Deploy,BuildingSale>(entity) && (!structure || structure->complete <= now);
  }
  bool canRepair(EntityId id,std::uint32_t owner) const {
    const auto it=entities.find(id);if(it==entities.end())return false;
    const auto entity=it->second;const auto& identity=registry.get<Identity>(entity);const auto hp=registry.get<Health>(entity).hp;
    return identity.owner==owner && identity.type->category==TypeCategory::Building && identity.type->clickRepairable &&
      complete(entity) && hp>0 && (hp<identity.type->strength || registry.all_of<BuildingRepair>(entity));
  }
  bool canSell(EntityId id,std::uint32_t owner) const {
    const auto it=entities.find(id);if(it==entities.end())return false;
    const auto entity=it->second;const auto& identity=registry.get<Identity>(entity);
    if(identity.owner!=owner || !identity.type->sellable || !complete(entity) || registry.get<Health>(entity).hp<=0)return false;
    if(const auto* factory=registry.try_get<Factory>(entity);factory && factory->phase!=FactoryExitPhase::Idle && factory->phase!=FactoryExitPhase::Blocked)return false;
    if(const auto* cargo=registry.try_get<Cargo>(entity);cargo && !cargo->passengers.empty())return false;
    return true;
  }
  int repairCost(EntityId id) const {
    const auto it=entities.find(id);if(it==entities.end())return 0;
    const auto& type=*registry.get<Identity>(it->second).type;
    const auto amount=std::min(definitions.profile().repairStep,type.strength-registry.get<Health>(it->second).hp);
    return compat::buildingRepairCost(type.cost,type.strength,amount,definitions.profile().repairPercentMillionths);
  }
  int saleRefund(EntityId id) const {
    const auto it=entities.find(id);if(it==entities.end())return 0;
    const auto& type=*registry.get<Identity>(it->second).type;
    return compat::buildingRefund(type.cost,type.strength,registry.get<Health>(it->second).hp,definitions.profile().refundPercentMillionths);
  }
  void toggleRepair(entt::entity entity) {
    const auto& identity=registry.get<Identity>(entity);
    if(!canRepair(identity.id,identity.owner)){message="Cannot repair this building";return;}
    if(registry.all_of<BuildingRepair>(entity)){registry.remove<BuildingRepair>(entity);message="Repair cancelled";return;}
    registry.emplace<BuildingRepair>(entity,identity.owner,now,now,false);message="Repair started";
  }
  void sellBuilding(entt::entity entity) {
    const auto& identity=registry.get<Identity>(entity);
    if(!canSell(identity.id,identity.owner)){message="Cannot sell: check ownership, construction and factory exit";return;}
    if(auto* factory=registry.try_get<Factory>(entity)) {
      for(const auto& item:factory->queue)funds(identity.owner)=int(std::min<std::int64_t>(std::numeric_limits<int>::max(),std::int64_t(funds(identity.owner))+item.paid));
      factory->queue.clear();factory->rally.reset();factory->phase=FactoryExitPhase::Idle;
    }
    registry.remove<BuildingRepair>(entity);
    if(auto* arm=registry.try_get<Armament>(entity))*arm={};
    const bool buildup=!identity.type->wall && !definitions.art().string(identity.type->image,"Buildup").empty();
    registry.emplace<BuildingSale>(entity,identity.owner,now,now+(buildup?definitions.profile().buildupFrames:0));
    registry.get<Transform>(entity).animationSpeed=clock.speed();message="Selling "+identity.type->id;
  }
  void buildingServices() {
    for(const auto& [id,entity]:entities) {
      auto& health=registry.get<Health>(entity);const auto& identity=registry.get<Identity>(entity);
      if(frozen(entity))continue;
      if(auto* sale=registry.try_get<BuildingSale>(entity)) {
        if(health.hp>0 && now>=sale->complete) {
          const auto refund=saleRefund(id);
          funds(sale->owner)=int(std::min<std::int64_t>(std::numeric_limits<int>::max(),std::int64_t(funds(sale->owner))+refund));
          health.hp=0;message="Sold "+identity.type->id+" for $"+std::to_string(refund);
        }
        continue;
      }
      auto* repair=registry.try_get<BuildingRepair>(entity);if(!repair)continue;
      if(repair->owner!=identity.owner || !canRepair(id,identity.owner) || health.hp>=identity.type->strength) {registry.remove<BuildingRepair>(entity);continue;}
      if(now<repair->next)continue;
      const auto price=repairCost(id);
      if(funds(identity.owner)<price) {repair->paused=true;repair->next=now+1;continue;}
      funds(identity.owner)-=price;repair->paused=false;repair->next=now+definitions.profile().repairFrames;
      health.hp=std::min(identity.type->strength,health.hp+definitions.profile().repairStep);
      if(health.hp==identity.type->strength){registry.remove<BuildingRepair>(entity);message="Building repaired";}
    }
  }
  bool prerequisites(const UnitTypeDef& type, std::uint32_t owner) const {
    if (definitions.profile().sandbox) return true;
    const auto stolen=espionage.find(owner);
    if(definitions.rules().boolean(type.id,"RequiresStolenAlliedTech") && (stolen==espionage.end() || !stolen->second.alliedTech))return false;
    if(definitions.rules().boolean(type.id,"RequiresStolenSovietTech") && (stolen==espionage.end() || !stolen->second.sovietTech))return false;
    for (const auto& required : type.prerequisites) {
      auto alternatives = definitions.rules().list("General", "Prerequisite" + required);
      if (alternatives.empty()) alternatives.push_back(required);
      bool found = false;
      for (const auto& [id, entity] : entities) {
        (void)id;
        const auto& identity = registry.get<Identity>(entity);
        if (identity.owner != owner || !complete(entity)) continue;
        for (const auto& alternative : alternatives) if (identity.type->id == canonicalId(alternative)) found = true;
      }
      if (!found) return false;
    }
    return type.techLevel >= 0;
  }
  void cancelBuildingInteraction(entt::entity entity) {
    const auto* task=registry.try_get<BuildingInteraction>(entity);if(!task)return;
    if(task->entering && !task->consumed && !registry.all_of<Infantry>(entity)) {
      auto& pose=registry.get<Transform>(entity);pose.current=pose.previous=task->entryStart;
      registry.emplace_or_replace<Mobile>(entity,pose.cell);
    } else if(task->entering && !task->consumed) {
      auto& pose=registry.get<Transform>(entity);auto& foot=registry.get<Infantry>(entity);
      // Return smoothly to the retained outside slot; never strand a cancelled
      // engineer inside the target building or teleport it out of the artwork.
      foot.next=pose.cell;foot.targetSubCell=foot.subCell;foot.segmentStart=pose.current;
      foot.progress=0;foot.length=std::max<std::int64_t>(1,integerSqrt(distanceSquared(pose.current,footPosition(pose.cell,foot.subCell))));
      foot.path={pose.cell};foot.requested=false;
      footReservations[key(pose.cell)][foot.subCell]=registry.get<Identity>(entity).id;refresh(pose.cell);
    }
    registry.remove<BuildingInteraction>(entity);
  }
  void stop(entt::entity entity) {
    if(auto* pilot=registry.try_get<Pilot>(entity)) {
      pilot->destination=registry.get<Transform>(entity).current;
      if(registry.get<Identity>(entity).type->aircraft->fixedWing)returnAircraft(entity);
      else if(pilot->mission!=AircraftMission::Landed) {release(registry.get<Identity>(entity).id);pilot->mission=AircraftMission::Holding;landAircraft(entity);}
    }
    registry.remove<CoilCharger,TemporalChannel,ShipyardRepair>(entity);
    if(auto* launcher=registry.try_get<MissileLauncher>(entity);launcher && launcher->child)if(const auto it=entities.find(launcher->child);it!=entities.end()) {
      auto& rocket=registry.get<SpawnedMissile>(it->second);
      if(!rocket.flight.launched){rocket.cancelled=true;registry.get<Health>(it->second).hp=0;launcher->child=0;launcher->ready=now;if(auto* arm=registry.try_get<Armament>(entity))arm->nextShot=now;}
    }
    cancelBuildingInteraction(entity);
    haltFoot(entity);
    if(auto* air=registry.try_get<Airborne>(entity)) {
      releaseAir(registry.get<Identity>(entity).id,false);air->requested=false;air->path.clear();air->speed=0;
      air->destination=registry.get<Transform>(entity).cell;air->destinationSlot=air->nextSlot=-1;
    }
    auto& pose = registry.get<Transform>(entity);
    pose.bodyTurn.reset(pose.heading); pose.turretTurn.reset(pose.turret);
    if (auto* armament = registry.try_get<Armament>(entity)) { armament->active = false; armament->automatic = false; armament->target=0;armament->burstRemaining=0; }
    if(auto* cargo=registry.try_get<Cargo>(entity))cargo->unloading=false;
    if (auto* mobile = registry.try_get<Mobile>(entity)) {
      mobile->cooperative=false; mobile->yielding=false; mobile->yieldFor=0; mobile->blockedBy=0; mobile->waitSince=0;
      mobile->pendingGroup=0; mobile->laneWaiting=false; mobile->yieldOrigin={}; mobile->yieldUntil=0;
      mobile->flowGroup=0;
      mobile->requested = false;
      mobile->exactTarget = false;
      if (mobile->reserved) mobile->path = {*mobile->reserved}; else { mobile->path.clear(); mobile->driving=false; }
    }
  }
  void move(entt::entity entity, TileCoord target) {
    if(auto* pilot=registry.try_get<Pilot>(entity)) {
      if(!validCell(target))return;
      release(registry.get<Identity>(entity).id);pilot->destination=position(target);pilot->mission=AircraftMission::Moving;return;
    }
    if(auto* air=registry.try_get<Airborne>(entity)) {
      const auto id=registry.get<Identity>(entity).id;releaseAir(id,false);
      for(int radius=0;radius<=16;++radius)for(int y=-radius;y<=radius;++y)for(int x=-radius;x<=radius;++x) {
        if(std::max(std::abs(x),std::abs(y))!=radius)continue;
        const TileCoord cell{target.x+x,target.y+y};const auto slot=availableAirSlot(cell,id,air->subCell);if(slot<0)continue;
        air->destination=cell;air->destinationSlot=slot;air->requested=true;air->retry=now;air->path.clear();air->nextSlot=-1;
        airReservations[key(cell)][slot]=id;return;
      }
      return;
    }
    if(auto* foot=registry.try_get<Infantry>(entity)) {
      const auto& type=*registry.get<Identity>(entity).type;
      if(validCell(target) && type.deployFire && (foot->posture==InfantryPosture::Deployed || foot->posture==InfantryPosture::Deploying || foot->posture==InfantryPosture::Undeploying)) {
        if(foot->posture==InfantryPosture::Deployed)deploy(entity);
        foot->destination=target;foot->requested=true;return;
      }
      if(!validCell(target) || foot->posture==InfantryPosture::Deployed || foot->posture==InfantryPosture::Deploying || foot->posture==InfantryPosture::Undeploying) return;
      if(auto* arm=registry.try_get<Armament>(entity))arm->autoChargeSuppressed=false;
      foot->destination=target; foot->requested=true; foot->retry=now;
      if(foot->targetSubCell>=0) foot->path={foot->next}; else foot->path.clear();
      return;
    }
    if(auto* factory=registry.try_get<Factory>(entity)) { factory->rally=target; return; }
    auto* mobile = registry.try_get<Mobile>(entity);
    if (!mobile || !validCell(target)) return;
    mobile->destination = target;
    mobile->exactTarget = false;
    mobile->requested = true;
    mobile->retry = now;
    mobile->waitSince=0;
    if (mobile->reserved) mobile->path = {*mobile->reserved}; else mobile->path.clear();
  }
  void deploy(entt::entity entity) {
    if(registry.all_of<Cargo>(entity)) {unload(entity);return;}
    const auto& identity = registry.get<Identity>(entity);
    const auto& transform = registry.get<Transform>(entity);
    if(auto* foot=registry.try_get<Infantry>(entity)) {
      if(!identity.type->infantry->deployer || identity.type->secondary.empty() || foot->targetSubCell>=0) return;
      if(foot->posture==InfantryPosture::Deploying || foot->posture==InfantryPosture::Undeploying) return;
      const bool undeploy=foot->posture==InfantryPosture::Deployed;
      stop(entity); foot->posture=undeploy?InfantryPosture::Undeploying:InfantryPosture::Deploying;
      footAnimation(*foot,undeploy?"UNDEPLOY":"DEPLOY",false);
      foot->postureUntil=now+footDuration(identity,foot->sequence); return;
    }
    const auto& targetId = identity.type->category == TypeCategory::Building ? identity.type->undeploysInto : identity.type->deploysInto;
    if (targetId.empty() || !complete(entity)) return;
    if (const auto* mobile = registry.try_get<Mobile>(entity); mobile && (mobile->reserved || !mobile->path.empty())) { message = "Stop before deploying"; return; }
    if (const auto* factory = registry.try_get<Factory>(entity); factory && (!factory->queue.empty() || factory->phase != FactoryExitPhase::Idle)) { message = "Factory must be idle before transforming"; return; }
    const auto& target = definitions.unit(targetId);
    if (!playable(target)) return;
    // Mobile cells use their center; buildings use their northwest start.
    // Choose the lower central cell for even foundations, and reverse the same
    // conversion when packing so repeated deployments cannot drift.
    auto targetCell = transform.cell;
    if (identity.type->category == TypeCategory::Building) {
      targetCell.x += (identity.type->foundationWidth - 1) / 2;
      targetCell.y += (identity.type->foundationHeight - 1) / 2;
    }
    if (target.category == TypeCategory::Building) {
      targetCell.x -= (target.foundationWidth - 1) / 2;
      targetCell.y -= (target.foundationHeight - 1) / 2;
    }
    const auto cells = footprint(target, targetCell);
    if (!std::all_of(cells.begin(), cells.end(), [&](auto c) { return free(c, identity.id); })) { message = "Deployment footprint blocked"; return; }
    stop(entity);
    for (const auto cell : cells) { reservations[key(cell)] = identity.id; refresh(cell); }
    registry.emplace<Deploy>(entity, &target, now, now + definitions.profile().deployFrames, targetCell);
  }
  void produce(entt::entity entity, std::string_view typeId) {
    auto* factory = registry.try_get<Factory>(entity);
    if (!factory || !complete(entity) || factory->queue.size() >= 32) return;
    const auto& identity = registry.get<Identity>(entity);
    const auto& type = definitions.unit(typeId);
    if (!playable(type) || !definitions.factoryMatches(*identity.type,type) ||
        (!definitions.profile().sandbox && !definitions.allowedHouse(type,definitions.playerHouse(identity.owner))) ||
        !identity.type->factoryExit || type.speed <= 0 || !prerequisites(type, identity.owner)) { message = "Factory/technology cannot produce " + type.id; return; }
    if(type.aircraft && type.aircraft->fixedWing) {
      int reserved=int(factory->queue.size());
      for(const auto& [id,e]:entities){(void)id;if(const auto* p=registry.try_get<Pilot>(e);p && p->airport==identity.id && registry.get<Health>(e).hp>0)++reserved;}
      if(reserved>=int(identity.type->airport->offsets.size())){message="All four airport pads are reserved";return;}
    }
    const int cost = definitions.profile().sandbox ? 0 : type.cost;
    if (funds(identity.owner) < cost) { message = "Insufficient credits"; return; }
    funds(identity.owner) -= cost;
    const int duration = definitions.profile().productionFramesOverride > 0 ?
      definitions.profile().productionFramesOverride : type.productionFrames;
    factory->queue.push_back({type.id, std::max(1, duration), std::max(1, duration), cost});
    message = "Queued " + type.id;
  }
  void apply(const Command& command) {
    if (command.kind==CommandKind::GroupMove) { groupMove(command); return; }
    if (command.kind == CommandKind::PlaceBuilding) {
      const auto& type = definitions.unit(command.type);
      const int cost = definitions.profile().sandbox ? 0 : type.cost;
      if (type.category == TypeCategory::Building && prerequisites(type, command.player) && funds(command.player) >= cost &&
          spawn(type, command.cell, command.player, true)) funds(command.player) -= cost;
      return;
    }
    const auto it = entities.find(command.entity);
    if (it == entities.end()) return;
    const auto entity = it->second;
    if(registry.all_of<SpawnedMissile>(entity))return;
    if (limbo(entity) || frozen(entity) || grappled(entity) || registry.any_of<NavalAircraft,SpawnedMissile>(entity))return;
    if (registry.get<Identity>(entity).owner != command.player || registry.any_of<Deploy,Contained,BuildingSale>(entity) || registry.get<Health>(entity).hp<=0) return;
    if(command.kind==CommandKind::ToggleRepair){toggleRepair(entity);return;}
    if(command.kind==CommandKind::SellBuilding){sellBuilding(entity);return;}
    if(auto* mobile=registry.try_get<Mobile>(entity);mobile && command.kind!=CommandKind::Produce && command.kind!=CommandKind::CancelProduction)
      mobile->pendingGroup=0;
    if (auto* exit = registry.try_get<FactoryExit>(entity)) {
      // Leaving the building is mandatory. The last player intent executes after
      // clearance; Stop cancels that intent without stranding a unit inside.
      if (command.kind == CommandKind::Stop) {
        exit->deferred.reset();
        harvesting.stop(command.entity);
        message = "Deferred command cancelled; vehicle will finish leaving the factory";
        return;
      }
      else if (command.kind == CommandKind::Move || command.kind == CommandKind::AttackEntity ||
               command.kind == CommandKind::AttackGround || command.kind == CommandKind::Deploy ||
               command.kind == CommandKind::Harvest || command.kind == CommandKind::ReturnToRefinery || command.kind==CommandKind::Unload) exit->deferred = command;
      message = "Unit is leaving the factory; command deferred until clear";
      return;
    }
    if (command.kind == CommandKind::Move || command.kind == CommandKind::Stop || command.kind == CommandKind::Deploy ||
        command.kind == CommandKind::AttackEntity || command.kind == CommandKind::AttackGround) harvesting.stop(command.entity);
    switch (command.kind) {
      case CommandKind::Harvest:
        if (harvesting.harvest(command.entity,command.cell)) stop(entity);
        break;
      case CommandKind::ReturnToRefinery:
        if (harvesting.returnTo(command.entity,command.target,*this)) stop(entity);
        break;
      case CommandKind::Move:
        stop(entity); move(entity, command.cell);
        if (auto* mobile=registry.try_get<Mobile>(entity)) { mobile->cooperative=true; mobile->exactTarget=command.exactDestination; }
        break;
      case CommandKind::Stop:
        stop(entity);
        if(auto* arm=registry.try_get<Armament>(entity))arm->autoChargeSuppressed=true;
        break;
      case CommandKind::EnterBuilding:
        if(registry.all_of<Pilot>(entity) && registry.get<Identity>(entity).type->aircraft->fixedWing) {if(validAirport(entity,command.target)){stop(entity);returnAircraft(entity,command.target);}break;}
        if(registry.get<Identity>(entity).type->naval && entities.contains(command.target) && registry.get<Identity>(entities.at(command.target)).type->unitRepair)startShipyardRepair(entity,command.target);
        else enterBuilding(entity,command.target);
        break;
      case CommandKind::EnterTransport: enterBuilding(entity,command.target,true);break;
      case CommandKind::Unload: unload(entity);break;
      case CommandKind::Deploy: deploy(entity); break;
      case CommandKind::Produce: produce(entity, command.type); break;
      case CommandKind::CancelProduction:
        if (auto* factory = registry.try_get<Factory>(entity); factory && !factory->queue.empty()) {
          funds(command.player) += factory->queue.back().paid;
          factory->queue.pop_back();
          if (factory->queue.empty() && factory->phase == FactoryExitPhase::Blocked) {
            factory->phase = FactoryExitPhase::Idle; factory->phaseStarted = now;
          }
        }
        break;
      case CommandKind::AttackEntity:
        if(registry.get<Identity>(entity).type->canDisguise) {
          const auto victim=entities.find(command.target);
          if(victim!=entities.end() && victim->second!=entity && registry.get<Identity>(victim->second).type->infantry &&
              registry.get<Health>(victim->second).hp>0 && !registry.any_of<Contained,Erased>(victim->second) && !limbo(victim->second)) {
            stop(entity);const auto& other=registry.get<Identity>(victim->second);
            registry.emplace_or_replace<SpyDisguise>(entity,other.type->id,other.owner);message="Spy disguise acquired";
          }
          break;
        }
        [[fallthrough]];
      case CommandKind::AttackGround:
        if (auto* armament = registry.try_get<Armament>(entity); armament && validCell(command.cell)) {
          if(command.kind==CommandKind::AttackGround)if(const auto* weapon=activeWeapon(entity);weapon && (weapon->repairs || weapon->leap || weapon->plantsBomb || weapon->disarmsBomb || weapon->temporal || weapon->disguise))return;
          if(command.kind==CommandKind::AttackGround)if(const auto* weapon=activeWeapon(entity);weapon && definitions.projectile(weapon->projectile).waterBound && !waterAt(command.cell))return;
          stop(entity);
          armament->active = true;
          armament->autoChargeSuppressed=false;
          armament->target = command.kind == CommandKind::AttackEntity ? command.target : 0;
          armament->ground = position(command.cell);
          if(auto* pilot=registry.try_get<Pilot>(entity)) {
            release(command.entity);pilot->mission=AircraftMission::Attacking;
          }
        }
        break;
      default: break;
    }
  }
  int travelSpeed(const UnitTypeDef& type, TileCoord from, TileCoord to) const {
    const auto& surface=grid.cell(to).surface;
    const auto& origin=grid.cell(from).surface;
    if (surface.wallHeightLeptons || surface.cliff ||
        (std::abs(surface.heightLeptons-origin.heightLeptons)>128 &&
         !surface.slopeX && !surface.slopeY && !origin.slopeX && !origin.slopeY)) return 0;
    const auto resource=harvesting.resourceAt(to);
    const std::string_view land=resource && resource->amount>0 ? std::string_view{"TIBERIUM"} : std::string_view{surface.land};
    auto& multipliers=terrainMultipliers[&type];
    const auto found=multipliers.find(land);
    if(found!=multipliers.end()) return found->second;
    return multipliers.emplace(std::string(land),definitions.terrainSpeed(land,type.speedType)).first->second;
  }
  bool trafficParticipant(EntityId id) const {
    const auto it=entities.find(id); if(it==entities.end()) return false;
    const auto* m=registry.try_get<Mobile>(it->second);
    return m && !registry.any_of<FactoryExit,Deploy>(it->second) &&
      (m->cooperative || m->requested || m->reserved || !m->path.empty());
  }
  bool routeClear(TileCoord cell,EntityId self) const {
    if(!validCell(cell) || !grid.cell(cell).buildable || structures.contains(key(cell)) ||
       grid.cell(cell).surface.wallHeightLeptons || grid.cell(cell).surface.cliff) return false;
    if(footBlocks(cell,self)) return false;
    for(const auto* owners:{&occupied,&reservations}) {
      const auto it=owners->find(key(cell));
      if(it!=owners->end() && it->second!=self && !trafficParticipant(it->second)) return false;
    }
    return true;
  }
  int travelFrames(const UnitTypeDef& type,TileCoord from,TileCoord to) const {
    const int percent=travelSpeed(type,from,to);
    const auto speed=compat::movementLeptons(type.speed)*motion::lepton*percent/100;
    if(speed<=0) return 0;
    const auto length=(from.x!=to.x && from.y!=to.y)?1448154:1024000;
    return static_cast<int>((length+speed-1)/speed);
  }
  void groupMove(const Command& command) {
    std::vector<EntityId> sorted=command.members; std::sort(sorted.begin(),sorted.end());
    sorted.erase(std::unique(sorted.begin(),sorted.end()),sorted.end());
    for(const auto id:sorted)if(const auto it=entities.find(id);it!=entities.end() && registry.any_of<Airborne,Pilot>(it->second)) {
      Command moveCommand=command;moveCommand.kind=CommandKind::Move;moveCommand.entity=id;apply(moveCommand);
    }
    const bool mixed=std::any_of(sorted.begin(),sorted.end(),[&](auto id){const auto it=entities.find(id);return it!=entities.end() && registry.all_of<Mobile>(it->second);});
    std::map<CellKey,int> assigned;
    for(const auto id:sorted) {
      const auto it=entities.find(id); if(it==entities.end() || limbo(it->second) || !registry.all_of<Infantry>(it->second) || registry.any_of<Contained,Airborne>(it->second) || registry.get<Identity>(it->second).owner!=command.player) continue;
      bool done=false;
      for(int radius=mixed?2:0;radius<=16 && !done;++radius) for(int y=-radius;y<=radius && !done;++y) for(int x=-radius;x<=radius && !done;++x) {
        if(std::max(std::abs(x),std::abs(y))!=radius) continue;
        const TileCoord c{command.cell.x+x,command.cell.y+y};
        if(assigned[key(c)]>=3 || availableFootSlot(c,id,command.player)<0) continue;
        Command moveCommand=command;moveCommand.kind=CommandKind::Move;moveCommand.entity=id;moveCommand.cell=c;
        apply(moveCommand);++assigned[key(c)];done=true;
      }
    }
    std::vector<EntityId> actors=command.members;
    std::sort(actors.begin(),actors.end()); actors.erase(std::unique(actors.begin(),actors.end()),actors.end());
    std::erase_if(actors,[&](EntityId id) {
      const auto it=entities.find(id);
      return it==entities.end() || registry.get<Identity>(it->second).owner!=command.player ||
        !registry.all_of<Mobile>(it->second) || frozen(it->second) || grappled(it->second) || registry.all_of<Deploy>(it->second);
    });
    if(actors.empty()) return;
    // Make all members traversable before measuring paths, including newly
    // commanded stationary tanks. Their physical occupancy is never removed.
    for(const auto id:actors) {
      const auto e=entities.at(id);
      if(!registry.all_of<FactoryExit>(e)) { harvesting.stop(id); stop(e); }
      else registry.get<FactoryExit>(e).deferred.reset();
      auto& m=registry.get<Mobile>(e); m.cooperative=true; m.pendingGroup=command.sequence;
    }
    std::vector<TileCoord> slots;
    for(int radius=0;radius<=32 && slots.size()<actors.size()*2;++radius) {
      std::vector<TileCoord> ring;
      for(int y=-radius;y<=radius;++y) for(int x=-radius;x<=radius;++x) {
        if(std::max(std::abs(x),std::abs(y))!=radius) continue;
        const TileCoord c{command.cell.x+x,command.cell.y+y};
        if(!routeClear(c,0) || claimedGoal(c,actors)) continue;
        const auto at=occupied.find(key(c));
        if(at!=occupied.end() && !std::binary_search(actors.begin(),actors.end(),at->second)) continue;
        if(std::any_of(actors.begin(),actors.end(),[&](EntityId id) {
          return travelSpeed(*registry.get<Identity>(entities.at(id)).type,c,c)>0;
        }) && (at!=occupied.end() || std::any_of(actors.begin(),actors.end(),[&](EntityId id) {
          return !narrowNeighbours(c,*registry.get<Identity>(entities.at(id)).type).empty();
        }))) ring.push_back(c);
      }
      std::sort(ring.begin(),ring.end(),[&](TileCoord a,TileCoord b) {
        return std::tuple{distanceSquared(position(a),position(command.cell)),a.y,a.x}<
               std::tuple{distanceSquared(position(b),position(command.cell)),b.y,b.x};
      });
      slots.insert(slots.end(),ring.begin(),ring.end());
    }
    std::sort(slots.begin(),slots.end(),[&](TileCoord a,TileCoord b) {
      return std::tuple{distanceSquared(position(a),position(command.cell)),a.y,a.x}<
             std::tuple{distanceSquared(position(b),position(command.cell)),b.y,b.x};
    });
    if(translateFormation(command,actors,slots)) return;
    int minX=100000,maxX=-100000,minY=100000,maxY=-100000;
    for(const auto id:actors) {
      const auto e=entities.at(id); const auto* exit=registry.try_get<FactoryExit>(e);
      const auto c=exit?exit->route.back():registry.get<Mobile>(e).reserved.value_or(registry.get<Transform>(e).cell);
      minX=std::min(minX,c.x); maxX=std::max(maxX,c.x); minY=std::min(minY,c.y); maxY=std::max(maxY,c.y);
    }
    formations.push_back({command,std::move(actors),std::move(slots),{},{(minX+maxX)/2,(minY+maxY)/2}});
  }
  bool translateFormation(const Command& command,const std::vector<EntityId>& actors,const std::vector<TileCoord>& slots) {
    // Keep the parallel-route shortcut only when it already produces a compact
    // destination cluster. Loose starting formations must close their gaps.
    if(slots.size()<actors.size()) return false;
    int minX=100000,maxX=-100000,minY=100000,maxY=-100000;
    for(const auto id:actors) {
      const auto e=entities.at(id); const auto& m=registry.get<Mobile>(e);
      if(registry.all_of<FactoryExit>(e)) return false;
      const auto c=m.reserved.value_or(registry.get<Transform>(e).cell);
      minX=std::min(minX,c.x); maxX=std::max(maxX,c.x); minY=std::min(minY,c.y); maxY=std::max(maxY,c.y);
    }
    if(maxX-minX>24 || maxY-minY>24) return false;
    const TileCoord shift{command.cell.x-(minX+maxX)/2,command.cell.y-(minY+maxY)/2};
    if(std::max(std::abs(shift.x),std::abs(shift.y))*actors.size()>4096) return false;
    std::int64_t translatedDistance=0,compactDistance=0;
    for(std::size_t i=0;i<actors.size();++i) {
      const auto e=entities.at(actors[i]); const auto start=registry.get<Mobile>(e).reserved.value_or(registry.get<Transform>(e).cell);
      const TileCoord goal{start.x+shift.x,start.y+shift.y};
      if(std::find(slots.begin(),slots.end(),goal)==slots.end()) return false;
      translatedDistance+=distanceSquared(position(goal),position(command.cell));
      compactDistance+=distanceSquared(position(slots[i]),position(command.cell));
    }
    if(translatedDistance>compactDistance) return false;
    std::vector<std::deque<TileCoord>> paths;
    for(const auto id:actors) {
      const auto e=entities.at(id); const auto start=registry.get<Mobile>(e).reserved.value_or(registry.get<Transform>(e).cell);
      const auto& type=*registry.get<Identity>(e).type;
      const TileCoord goal{start.x+shift.x,start.y+shift.y};
      if(claimedGoal(goal,actors)) return false;
      const auto clear=[&](TileCoord c) {
        if(!routeClear(c,id)) return false;
        for(const auto* owners:{&occupied,&reservations}) if(const auto it=owners->find(key(c));it!=owners->end())
          if(!std::binary_search(actors.begin(),actors.end(),it->second)) return false;
        return true;
      };
      auto at=start; std::deque<TileCoord> path;
      while(at!=goal) {
        const int dx=goal.x-at.x,dy=goal.y-at.y;
        const TileCoord next{at.x+(std::abs(dx)>=std::abs(dy)?(dx>0)-(dx<0):0),
          at.y+(std::abs(dy)>=std::abs(dx)?(dy>0)-(dy<0):0)};
        if(!clear(next) || travelSpeed(type,at,next)<=0) return false;
        if(next.x!=at.x && next.y!=at.y && (!clear({next.x,at.y}) || !clear({at.x,next.y}) ||
            travelSpeed(type,at,{next.x,at.y})<=0 || travelSpeed(type,at,{at.x,next.y})<=0)) return false;
        path.push_back(next); at=next;
      }
      paths.push_back(std::move(path));
    }
    for(std::size_t i=0;i<actors.size();++i) {
      const auto e=entities.at(actors[i]); const auto c=registry.get<Mobile>(e).reserved.value_or(registry.get<Transform>(e).cell);
      Command individual{command.tick,command.player,command.sequence,CommandKind::Move,actors[i],0,{c.x+shift.x,c.y+shift.y}};
      individual.exactDestination=true; apply(individual);
      auto& m=registry.get<Mobile>(e); m.path=std::move(paths[i]); m.requested=false;
      if(m.reserved) m.path.push_front(*m.reserved);
    }
    return true;
  }
  bool claimedGoal(TileCoord cell,const std::vector<EntityId>& actors) const {
    for(const auto& [id,entity]:entities) {
      if(std::binary_search(actors.begin(),actors.end(),id)) continue;
      const auto* m=registry.try_get<Mobile>(entity);
      if(m && m->cooperative && !m->pendingGroup && m->destination==cell) return true;
    }
    return false;
  }
  void assignFormations() {
    while(!formations.empty()) {
      auto& job=formations.front(); const auto& actors=job.actors; const auto& slots=job.slots;
      auto& costs=job.costs; const auto& command=job.command;
      const auto current=[&](EntityId id) {
        const auto it=entities.find(id); if(it==entities.end()) return false;
        const auto* m=registry.try_get<Mobile>(it->second);
        return m && m->pendingGroup==command.sequence && registry.get<Identity>(it->second).owner==command.player;
      };
      for(std::size_t i=costs.size();i<actors.size();++i) {
        if(!current(actors[i]) || slots.empty()) {
          costs.emplace_back(slots.size()+actors.size(),traffic::unreachable/2); continue;
        }
        // Finish at least one bounded row at a time; large groups continue next
        // frame instead of losing members when the shared budget is exhausted.
        if(assignmentBudget<4096) return;
        const auto entity=entities.at(actors[i]); const auto& t=registry.get<Transform>(entity);
        const auto& m=registry.get<Mobile>(entity); const auto& type=*registry.get<Identity>(entity).type;
        const auto* exit=registry.try_get<FactoryExit>(entity);
        const auto start=exit?exit->route.back():m.reserved.value_or(t.cell);
        constexpr std::size_t allowance=4096;
        auto remaining=allowance;
        auto row=traffic::travelCosts(start,slots,[&](TileCoord a,TileCoord b) { return travelFrames(type,a,b); },
          [&](TileCoord c) { return routeClear(c,actors[i]); },remaining,
          static_cast<int>(1024000/std::max<std::int64_t>(1,compat::movementLeptons(type.speed)*motion::lepton)));
        assignmentBudget-=allowance-remaining; movementStats.assignmentExpanded+=allowance-remaining;
        for(std::size_t j=0;j<slots.size();++j) if(row[j]<traffic::unreachable) {
          const Position delta{position(slots[j]).x-position(start).x,position(slots[j]).y-position(start).y};
          const auto bearing=direction(delta);
          const auto turning=compat::rotationFrames(motion::angleDelta(t.heading,bearing),type.rot);
          // Preserve flanks/rows even when an obstacle prevents translating the
          // whole route. Distance-to-click alone collapses lanes into the centre.
          const TileCoord preferred{command.cell.x+std::clamp(start.x-job.sourceCenter.x,-12,12),
            command.cell.y+std::clamp(start.y-job.sourceCenter.y,-12,12)};
          row[j]=distanceSquared(position(slots[j]),position(preferred))/1000+(row[j]+turning)*32;
        }
        row.resize(slots.size()+actors.size(),traffic::unreachable/2);
        costs.push_back(std::move(row));
      }
      for(std::size_t i=0;i<actors.size();++i) if(!current(actors[i])) {
        std::fill(costs[i].begin(),costs[i].begin()+static_cast<std::ptrdiff_t>(slots.size()),traffic::unreachable);
        std::fill(costs[i].begin()+static_cast<std::ptrdiff_t>(slots.size()),costs[i].end(),0);
      }
      // Earlier formations in this same tick may already have claimed slots.
      for(std::size_t j=0;j<slots.size();++j) if(claimedGoal(slots[j],actors))
        for(auto& row:costs) row[j]=traffic::unreachable;
      // Compactness precedes travel preferences. Matching equal-radius slots
      // together still chooses the approach with the least lane swapping.
      std::vector<traffic::Cost> radii;
      for(const auto slot:slots) radii.push_back(distanceSquared(position(slot),position(command.cell)));
      const auto assignment=traffic::assignCompact(costs,radii);
      std::vector<EntityId> assigned;
      for(std::size_t i=0;i<actors.size();++i) {
        if(!current(actors[i])) continue;
        const auto column=assignment[i];
        if(column<0 || static_cast<std::size_t>(column)>=slots.size() || costs[i][column]>=traffic::unreachable/2 || !routeClear(slots[column],actors[i])) {
          // A bounded search must never silently substitute an unreachable slot.
          registry.get<Mobile>(entities.at(actors[i])).pendingGroup=0;
          message="No formation slot found within the search budget"; continue;
        }
        Command individual{command.tick,command.player,command.sequence,CommandKind::Move,actors[i],0,slots[column]};
        individual.exactDestination=true;
        apply(individual);
        if(!registry.all_of<FactoryExit>(entities.at(actors[i]))) assigned.push_back(actors[i]);
      }
      attachFlow(assigned,command.cell);
      formations.pop_front();
    }
  }
  void attachFlow(const std::vector<EntityId>& members,TileCoord center) {
    if(members.size()<2) return;
    FlowGroup group; group.members=members; group.center=center;
    group.minimum=group.maximum=center;
    int radius=0;
    for(const auto id:members) {
      const auto e=entities.at(id); const auto& m=registry.get<Mobile>(e);
      const auto start=m.reserved.value_or(registry.get<Transform>(e).cell);
      for(const auto c:{start,m.destination}) {
        group.minimum.x=std::min(group.minimum.x,c.x); group.minimum.y=std::min(group.minimum.y,c.y);
        group.maximum.x=std::max(group.maximum.x,c.x); group.maximum.y=std::max(group.maximum.y,c.y);
      }
      radius=std::max(radius,std::max(std::abs(m.destination.x-center.x),std::abs(m.destination.y-center.y)));
    }
    // Sparse sandbox coordinates are unbounded. Cap field area and retain A*
    // for exceptional long-distance orders instead of allocating a world grid.
    if(group.maximum.x-group.minimum.x>192 || group.maximum.y-group.minimum.y>192) return;
    group.minimum.x-=16; group.minimum.y-=16; group.maximum.x+=16; group.maximum.y+=16;
    group.dockingRadius=radius+4;
    const auto id=nextFlowGroup++;
    for(const auto actor:members) registry.get<Mobile>(entities.at(actor)).flowGroup=id;
    flowGroups.emplace(id,std::move(group));
  }
  std::uint64_t flowTopology() const {
    std::uint64_t hash=14695981039346656037ULL;
    const auto add=[&](std::uint64_t n) { hash^=n; hash*=1099511628211ULL; };
    for(const auto& c:blockedTerrain) { add(c.first); add(c.second); }
    for(const auto& [c,id]:structures) { add(c.first); add(c.second); add(id); }
    for(const auto& [c,s]:terrain) {
      add(c.first); add(c.second); for(const auto ch:s.land) add(ch);
      add(s.heightLeptons); add(s.wallHeightLeptons); add(s.cliff); add(s.slopeX); add(s.slopeY);
    }
    for(const auto& resource:harvesting.resources()) if(resource.amount>0) { add(resource.cell.x); add(resource.cell.y); }
    for(const auto& [id,e]:entities) if(registry.all_of<Mobile>(e) && !trafficParticipant(id)) {
      const auto c=registry.get<Transform>(e).cell; add(id); add(c.x); add(c.y);
    }
    return hash;
  }
  void updateFlows(std::size_t& budget) {
    if(flowGroups.empty()) return;
    auto flowBudget=std::min<std::size_t>(4096,budget/2);
    const auto topology=flowTopology();
    for(auto it=flowGroups.begin();it!=flowGroups.end();) {
      auto& group=it->second;
      std::erase_if(group.members,[&](EntityId id) {
        return !entities.contains(id) || !registry.all_of<Mobile>(entities.at(id)) ||
          registry.get<Mobile>(entities.at(id)).flowGroup!=it->first;
      });
      if(group.members.empty()) { it=flowGroups.erase(it); continue; }
      if(group.profiles.empty() || group.topology!=topology) {
        const bool changed=!group.profiles.empty();
        group.profiles.clear(); group.topology=topology;
        std::map<std::string,std::vector<TileCoord>> goals;
        for(const auto id:group.members) {
          const auto e=entities.at(id); const auto* type=registry.get<Identity>(e).type;
          auto& m=registry.get<Mobile>(e);
          if(changed && !m.yielding) {
            if(m.reserved) m.path={*m.reserved}; else m.path.clear();
            m.requested=true; m.retry=now;
          }
          goals[type->speedType].push_back(m.destination);
        }
        for(const auto id:group.members) {
          const auto* type=registry.get<Identity>(entities.at(id)).type;
          if(group.profiles.contains(type->speedType)) continue;
          group.profiles.emplace(type->speedType,FlowProfile{type,{group.minimum,group.maximum,goals.at(type->speedType)}});
          ++movementStats.flowBuilds;
        }
      }
      for(auto& [profile,p]:group.profiles) {
        (void)profile;
        const auto used=p.field.expand(flowBudget,[&](TileCoord a,TileCoord b) {
          const auto percent=travelSpeed(*p.type,a,b);
          return percent>0?(a.x!=b.x && a.y!=b.y?141400:100000)/percent:0;
        },[&](TileCoord c) { return routeClear(c,0); });
        budget-=used; flowBudget-=used; movementStats.flowExpanded+=used;
      }
      ++it;
    }
  }
  bool flowRoute(entt::entity entity,bool directOnly=false) {
    auto& m=registry.get<Mobile>(entity); const auto found=flowGroups.find(m.flowGroup);
    if(found==flowGroups.end() || m.yielding) return false;
    const auto& group=found->second; const auto& t=registry.get<Transform>(entity);
    const auto& identity=registry.get<Identity>(entity);
    // Goal line-of-sight is the open-ground fast path of the flow follower.
    // Steer toward the assigned flank, rather than merging all tanks into the
    // minimum of a regional field before they need to gather.
    std::deque<TileCoord> direct;
    auto cursor=t.cell;
    while(cursor!=m.destination && direct.size()<192) {
      const int dx=m.destination.x-cursor.x,dy=m.destination.y-cursor.y;
      const TileCoord next{cursor.x+(std::abs(dx)>=std::abs(dy)?(dx>0)-(dx<0):0),
        cursor.y+(std::abs(dy)>=std::abs(dx)?(dy>0)-(dy<0):0)};
      const auto clear=[&](TileCoord c) { return routeClear(c,identity.id) && travelSpeed(*identity.type,cursor,c)==100; };
      if(!clear(next) || (next.x!=cursor.x && next.y!=cursor.y && (!clear({next.x,cursor.y}) || !clear({cursor.x,next.y})))) break;
      direct.push_back(next); cursor=next;
    }
    if(cursor==m.destination) { m.path=std::move(direct); m.requested=false; m.flowGroup=0; return true; }
    if(directOnly) return false;
    const auto near=[&](TileCoord c) { return std::max(std::abs(c.x-group.center.x),std::abs(c.y-group.center.y))<=group.dockingRadius; };
    if(near(t.cell)) { m.flowGroup=0; ++movementStats.dockingSearches; return false; }
    const auto profile=group.profiles.find(identity.type->speedType); if(profile==group.profiles.end()) return false;
    const auto& field=profile->second.field;
    auto at=t.cell; auto heading=t.heading;
    constexpr std::array<TileCoord,8> offsets{{{1,0},{0,1},{-1,0},{0,-1},{1,1},{-1,1},{-1,-1},{1,-1}}};
    // Follow a short piece of the shared field, then sample again. The heading
    // preference preserves lanes among near-equal alternatives. Every step
    // strictly descends the integration field, so that preference cannot loop.
    for(int count=0;count<6 && !near(at);++count) {
      std::optional<TileCoord> best; std::tuple<traffic::Cost,std::int64_t,int,int> score{traffic::unreachable,0,0,0};
      for(const auto d:offsets) {
        const TileCoord next{at.x+d.x,at.y+d.y};
        const auto remaining=field.distance(next),current=field.distance(at);
        if(current==traffic::unreachable || remaining>=current || !routeClear(next,identity.id)) continue;
        const auto percent=travelSpeed(*identity.type,at,next); if(percent<=0) continue;
        if(d.x && d.y && (!routeClear({next.x,at.y},identity.id) || !routeClear({at.x,next.y},identity.id) ||
            travelSpeed(*identity.type,at,{next.x,at.y})<=0 || travelSpeed(*identity.type,at,{at.x,next.y})<=0)) continue;
        const auto bearing=direction({d.x,d.y});
        const auto candidate=std::tuple{remaining+(d.x && d.y?141400:100000)/percent+
          std::abs(motion::angleDelta(heading,bearing))/32,distanceSquared(position(next),position(m.destination)),next.y,next.x};
        if(candidate<score) { best=next; score=candidate; }
      }
      if(!best) break;
      heading=direction({best->x-at.x,best->y-at.y}); at=*best; m.path.push_back(at); ++movementStats.flowSteps;
    }
    if(m.path.empty()) return false;
    m.requested=false; return true;
  }
  std::vector<TileCoord> sweep(const motion::DriveSegment& track,TileCoord start,TileCoord next) const {
    std::vector<TileCoord> cells{start,next};
    const auto add=[&](TileCoord c) { if(std::find(cells.begin(),cells.end(),c)==cells.end()) cells.push_back(c); };
    for(const auto p:track.points) add(cellAt(planar(p)));
    if(start.x!=next.x && start.y!=next.y) { add({next.x,start.y}); add({start.x,next.y}); }
    return cells;
  }
  motion::DriveSegment routeTrack(Position from,TileCoord start,TileCoord next,std::optional<TileCoord> after,EntityId id,const UnitTypeDef& type) const {
    motion::DriveSegment track; const auto to=position(next); track.build(fixed(from),fixed(to));
    if(after) {
      const auto p=position(*after);
      const auto turn=std::abs(motion::angleDelta(direction({to.x-from.x,to.y-from.y}),direction({p.x-to.x,p.y-to.y})));
      if(turn>16 && turn<=16384 && routeClear(*after,id) && travelSpeed(type,start,*after)>0) {
        track.buildCorner(fixed(from),fixed(to),fixed(p));
        for(const auto c:sweep(track,start,next)) if(!routeClear(c,id) || travelSpeed(type,start,c)<=0) {
          track.build(fixed(from),fixed(to)); break;
        }
      }
    }
    return track;
  }
  std::vector<TileCoord> narrowNeighbours(TileCoord c,const UnitTypeDef& type) const {
    std::vector<TileCoord> result;
    for(const TileCoord d:{TileCoord{1,0},TileCoord{0,1},TileCoord{-1,0},TileCoord{0,-1}}) {
      const TileCoord n{c.x+d.x,c.y+d.y};
      if(validCell(n) && grid.cell(n).buildable && !structures.contains(key(n)) && travelSpeed(type,c,n)>0) result.push_back(n);
    }
    return result;
  }
  std::optional<std::pair<CellKey,int>> laneEntry(entt::entity entity,TileCoord next,std::optional<TileCoord> approach=std::nullopt) {
    const auto& type=*registry.get<Identity>(entity).type; const auto start=approach.value_or(registry.get<Transform>(entity).cell);
    // An unbranched chain has two neighbours per interior cell. Its endpoints
    // are open junctions/bays; dead ends do not require a direction lease.
    const auto neighbours=narrowNeighbours(next,type); if(neighbours.size()!=2) return std::nullopt;
    std::deque<TileCoord> chain{next};
    for(int side=0;side<2;++side) {
      auto previous=next,c=neighbours[side];
      for(int count=0;count<128;++count) {
        const auto exits=narrowNeighbours(c,type); if(exits.size()!=2) break;
        if(std::find(chain.begin(),chain.end(),c)!=chain.end()) return std::nullopt;
        if(side==0) chain.push_front(c); else chain.push_back(c);
        const auto onward=exits[0]==previous?exits[1]:exits[0]; previous=c; c=onward;
        if(count==127) return std::nullopt;
      }
    }
    if(chain.size()<2 || std::find(chain.begin(),chain.end(),start)!=chain.end()) return std::nullopt;
    if(key(chain.back())<key(chain.front())) std::reverse(chain.begin(),chain.end());
    const auto nearFront=distanceSquared(position(start),position(chain.front()));
    const auto nearBack=distanceSquared(position(start),position(chain.back()));
    const int desired=nearFront<nearBack?1:-1;
    const auto laneKey=key(chain.front());
    if(!trafficLanes.contains(laneKey)) trafficLanes.emplace(laneKey,TrafficLane{{chain.begin(),chain.end()},0,false});
    return std::pair{laneKey,desired};
  }
  void updateLanes() {
    for(auto it=trafficLanes.begin();it!=trafficLanes.end();) {
      bool occupiedLane=false;
      for(const auto c:it->second.cells) if(occupied.contains(key(c)) || reservations.contains(key(c))) { occupiedLane=true; break; }
      if(!occupiedLane && (it->second.admitted || now>=it->second.approachUntil)) it=trafficLanes.erase(it); else ++it;
    }
  }
  std::vector<entt::entity> trafficOrder() const {
    std::vector<entt::entity> order;
    for(const auto& [id,entity]:entities) { (void)id; if(registry.all_of<Mobile>(entity)) order.push_back(entity); }
    std::sort(order.begin(),order.end(),[&](auto a,auto b) {
      const auto priority=[&](auto e) {
        const auto& m=registry.get<Mobile>(e);
        return std::tuple{!registry.all_of<FactoryExit>(e),!m.reserved.has_value(),!m.yielding,
          m.waitSince?m.waitSince:now+1,registry.get<Identity>(e).id};
      };
      return priority(a)<priority(b);
    });
    return order;
  }
  bool yieldTo(entt::entity blocker,entt::entity requester,std::size_t& budget,int depth=0,std::set<EntityId>* visitedPtr=nullptr) {
    std::set<EntityId> localVisited;
    auto& visited=visitedPtr?*visitedPtr:localVisited;
    auto& m=registry.get<Mobile>(blocker); const auto& waiting=registry.get<Mobile>(requester);
    const auto& identity=registry.get<Identity>(blocker); const auto& peer=registry.get<Identity>(requester);
    const auto start=registry.get<Transform>(blocker).cell;
    if(!m.cooperative || m.pendingGroup || m.reserved || identity.owner!=peer.owner ||
       registry.any_of<FactoryExit,Deploy>(blocker) || !budget || depth>8 || !visited.insert(identity.id).second) return false;
    if(m.yielding && !m.path.empty() && (!m.waitSince || now-m.waitSince<24)) return false;
    std::set<CellKey> passage;
    passage.insert(key(registry.get<Transform>(requester).cell));
    auto previous=registry.get<Transform>(requester).cell;
    for(const auto c:waiting.path) {
      passage.insert(key(c));
      if(c.x!=previous.x && c.y!=previous.y) { passage.insert({c.x,previous.y}); passage.insert({previous.x,c.y}); }
      previous=c;
    }
    std::deque<TileCoord> queue{start}; std::map<CellKey,TileCoord> parents; parents[key(start)]=start;
    std::set<CellKey> finalSlots;
    for(const auto& [id,e]:entities) {
      (void)id;
      if(const auto* other=registry.try_get<Mobile>(e);other && other->cooperative && registry.get<Identity>(e).owner==identity.owner)
        { finalSlots.insert(key(other->destination)); if(other->yielding && !other->path.empty()) finalSlots.insert(key(other->path.back())); }
    }
    std::optional<TileCoord> bay;
    constexpr std::array<TileCoord,8> offsets{{{1,0},{0,1},{-1,0},{0,-1},{1,1},{-1,1},{-1,-1},{1,-1}}};
    std::size_t expanded=0;
    while(!queue.empty() && budget && expanded<512) {
      const auto c=queue.front(); queue.pop_front(); --budget; ++expanded;
      // A waiting bay must leave room to pass. Stopping inside a one-cell
      // corridor can retain its direction lease and deadlock both entrances.
      if(c!=start && !passage.contains(key(c)) && !finalSlots.contains(key(c)) &&
          narrowNeighbours(c,*identity.type).size()>2) { bay=c; break; }
      for(const auto d:offsets) {
        const TileCoord next{c.x+d.x,c.y+d.y};
        if(std::max(std::abs(next.x-start.x),std::abs(next.y-start.y))>12 || parents.contains(key(next)) ||
           !free(next,identity.id) || travelSpeed(*identity.type,c,next)<=0) continue;
        if(d.x && d.y && (!free({next.x,c.y},identity.id) || !free({c.x,next.y},identity.id) ||
          travelSpeed(*identity.type,c,{next.x,c.y})<=0 || travelSpeed(*identity.type,c,{c.x,next.y})<=0)) continue;
        parents[key(next)]=c; queue.push_back(next);
      }
    }
    if(!bay) {
      // A packed formation can require an outer neighbour to clear first.
      // Propagate the original waiter's priority, with bounded depth/work and
      // a visited set, instead of repeatedly exchanging two occupied slots.
      for(const auto d:offsets) {
        const auto at=occupied.find({start.x+d.x,start.y+d.y});
        if(at==occupied.end() || at->second==peer.id || at->second==identity.id) continue;
        const auto next=entities.at(at->second); const auto* p=registry.try_get<Mobile>(next);
        if(p && (p->path.empty() || (p->waitSince && now-p->waitSince>=16)) && yieldTo(next,requester,budget,depth+1,&visited)) return true;
      }
      return false;
    }
    std::deque<TileCoord> route;
    for(auto c=*bay;c!=start;c=parents.at(key(c))) route.push_front(c);
    m.path=std::move(route); m.requested=false; m.yielding=true; m.yieldFor=peer.id;
    m.yieldOrigin=start; m.yieldUntil=now+32; m.waitSince=0;
    ++movementStats.yields;
    return true;
  }
  void recoverTraffic(const std::vector<entt::entity>& order,std::size_t& budget) {
    for(const auto entity:order) {
      auto& m=registry.get<Mobile>(entity);
      if(m.yielding && m.path.empty() && !m.reserved && now>=m.yieldUntil) {
        bool needed=false;
        for(const auto& [id,peer]:entities) {
          if(peer==entity) continue;
          const auto* p=registry.try_get<Mobile>(peer);
          if(p && (id==m.yieldFor || (!p->yielding && registry.get<Identity>(peer).owner==registry.get<Identity>(entity).owner))) {
            if(id==m.yieldFor && !p->path.empty() &&
                distanceSquared(registry.get<Transform>(peer).current,position(m.yieldOrigin))<square(1500)) needed=true;
            auto previous=registry.get<Transform>(peer).cell;
            for(const auto c:p->path) {
              needed|=c==m.yieldOrigin || (c.x!=previous.x && c.y!=previous.y &&
                (TileCoord{c.x,previous.y}==m.yieldOrigin || TileCoord{previous.x,c.y}==m.yieldOrigin));
              previous=c;
            }
          }
        }
        if(!needed) { m.yielding=false; m.yieldFor=0; m.requested=true; m.retry=now; }
      }
      if(!m.waitSince || now-m.waitSince<24 || m.reserved || m.path.empty() || !budget) continue;
      const auto start=registry.get<Transform>(entity).cell,next=m.path.front();
      std::vector<TileCoord> blocked{next};
      if(next.x!=start.x && next.y!=start.y) { blocked.push_back({next.x,start.y}); blocked.push_back({start.x,next.y}); }
      bool yielded=false;
      std::set<EntityId> blockers;
      if(m.blockedBy && entities.contains(m.blockedBy)) blockers.insert(m.blockedBy);
      for(const auto c:blocked) if(const auto it=occupied.find(key(c));it!=occupied.end()) blockers.insert(it->second);
      for(const auto id:blockers) {
        if(id==registry.get<Identity>(entity).id) continue;
        const auto peer=entities.at(id); const auto* p=registry.try_get<Mobile>(peer);
        if(!p) continue;
        if(p->reserved || (!p->path.empty() && (!p->waitSince || now-p->waitSince<16))) { yielded=true; continue; }
        if(yieldTo(peer,entity,budget) || yieldTo(entity,peer,budget)) { yielded=true; break; }
      }
      // An actual stationary obstruction may have appeared after route creation.
      // Retry locally only after waiting, so transient traffic never triggers
      // alternating long detours every few frames.
      if(!yielded && !m.laneWaiting && now-m.waitSince>=64 && (now-m.waitSince)%32==0 && budget) {
        const auto& identity=registry.get<Identity>(entity);
        std::size_t expanded=0;
        const auto target=m.yielding?m.path.back():m.destination;
        const auto path=findTilePath(grid,start,target,std::min<std::size_t>(budget,1024),&expanded,
          [&](TileCoord a,TileCoord b) { return travelSpeed(*identity.type,a,b); },
          [&](TileCoord c) { return free(c,identity.id); },false);
        budget-=std::min(budget,expanded);
        if(path) m.path.assign(path->cells.begin(),path->cells.end());
      }
    }
  }
  void planTraffic(const std::vector<entt::entity>& order,std::size_t& budget) {
    // Visible goals need no integration work. Resolve these before building
    // shared fields, so small open-ground orders retain immediate departure.
    for(const auto e:order) {
      const auto& m=registry.get<Mobile>(e);
      if(m.flowGroup && m.path.empty() && !m.yielding) flowRoute(e,true);
    }
    updateFlows(budget);
    updateLanes();
    recoverTraffic(order,budget);
    for(const auto entity:order) {
      auto& m=registry.get<Mobile>(entity); const auto& t=registry.get<Transform>(entity);
      const auto& identity=registry.get<Identity>(entity);
      if(m.flowGroup && m.path.empty() && !m.yielding && t.cell!=m.destination) m.requested=true;
      if(registry.any_of<FactoryExit,Deploy>(entity) || !m.path.empty() || !m.requested || now<m.retry || !budget) continue;
      if(flowRoute(entity)) continue;
      std::size_t expanded=0;
      const auto path=findTilePath(grid,t.cell,m.destination,std::min<std::size_t>(budget,2048),&expanded,
        [&](TileCoord a,TileCoord b) { return travelSpeed(*identity.type,a,b); },
        [&](TileCoord c) { return routeClear(c,identity.id); },!m.exactTarget);
      budget-=std::min(budget,std::max<std::size_t>(1,expanded));
      if(path) {
        m.path.assign(path->cells.begin(),path->cells.end()); m.destination=path->target; m.requested=false;
        // While a distant field is still integrating, use a short A* prefix so
        // a group can depart immediately and join the shared field next time.
        if(m.flowGroup && m.path.size()>6) m.path.resize(6);
      }
      else { m.retry=now+definitions.profile().pathRetryFrames; if(!m.waitSince) m.waitSince=now; }
    }
  }
  bool prepareTrack(entt::entity entity, TileCoord next, const FactoryExit* exit) {
    auto& mobile=registry.get<Mobile>(entity);
    auto& transform=registry.get<Transform>(entity);
    const auto& identity=registry.get<Identity>(entity);
    const auto start=transform.cell;
    mobile.laneWaiting=false; mobile.blockedBy=0;
    if(!exit && !mobile.yielding && laneApproachHolds.contains(identity.id)) { mobile.laneWaiting=true; return false; }
    if(!exit) if(const auto hold=forecastHolds.find(identity.id);hold!=forecastHolds.end()) {
      mobile.blockedBy=hold->second; return false;
    }
    const auto entry=exit?std::nullopt:laneEntry(entity,next);
    if(entry) {
      auto& lane=trafficLanes.at(entry->first);
      if(lane.direction && lane.direction!=entry->second) { lane.draining=true; mobile.laneWaiting=true; return false; }
      if(lane.draining && lane.admitted>=3) { mobile.laneWaiting=true; return false; }
    }
    const bool diagonal=next.x!=start.x && next.y!=start.y;
    const auto clear=[&](TileCoord cell) {
      return (exit?free(cell,identity.id,exit->factory):routeClear(cell,identity.id)) && travelSpeed(*identity.type,start,cell)>0;
    };
    if (!clear(next) || (diagonal && (!clear({next.x,start.y}) || !clear({start.x,next.y})))) return false;
    const auto from=transform.current,to=position(next);
    const int desired=direction({to.x-from.x,to.y-from.y});
    mobile.track.build(fixed(from),fixed(to));
    if (!exit && mobile.path.size()>1) {
      const auto after=position(mobile.path[1]);
      const int onward=direction({after.x-to.x,after.y-to.y});
      const auto turn=std::abs(motion::angleDelta(desired,onward));
      if (turn>16 && turn<=16384 && clear(mobile.path[1]))
        mobile.track.buildCorner(fixed(from),fixed(to),fixed(after));
    }
    std::vector<TileCoord> swept;
    const auto add=[&](TileCoord c) { if (c!=start && std::find(swept.begin(),swept.end(),c)==swept.end()) swept.push_back(c); };
    for (const auto p:mobile.track.points) add(cellAt(planar(p)));
    if (diagonal) { add({next.x,start.y}); add({start.x,next.y}); }
    if (!std::all_of(swept.begin(),swept.end(),clear)) {
      // A bend is optional; never cut through a blocked neighbouring cell.
      mobile.track.build(fixed(from),fixed(to));
      mobile.driving=false; swept={next};
      if (diagonal) { add({next.x,start.y}); add({start.x,next.y}); }
    }
    mobile.blockedBy=exit?0:segmentBlocker(entity,mobile.track);
    if(mobile.blockedBy) return false;
    // Movement commits occupancy only after resolving this frame's proposals.
    // Swept cells still protect construction and terrain edits, but do not
    // prevent parallel vehicles from sharing a diagonal clearance cell.
    mobile.sweptReservations=exit?std::vector<TileCoord>{}:swept;
    mobile.reserved=next;
    ++movementStats.scheduledSegments;
    if(entry) {
      auto& lane=trafficLanes.at(entry->first); lane.direction=entry->second;
      lane.admitted=std::min(3,lane.admitted+1);
    }
    return true;
  }
  std::vector<FuturePose> forecast(entt::entity entity,int delay=0,int segmentLimit=4,
                                 const motion::DriveSegment* initial=nullptr) const {
    auto m=registry.get<Mobile>(entity); auto t=registry.get<Transform>(entity);
    const auto& identity=registry.get<Identity>(entity); const auto& type=*identity.type;
    if(initial) { m.track=*initial; m.reserved=m.path.front(); m.segmentProgress=0; }
    std::vector<FuturePose> result{{t.current,t.cell}}; int completed=0;
    // Use the same fixed-point speed, ROT, corner geometry and rotation clock
    // as the driver. A grid-step forecast would mis-time slow tanks and bends.
    for(int frame=0;frame<80;++frame) {
      if(frame>=delay && completed<segmentLimit) {
        std::int64_t allowance=-1;
        for(int substep=0;substep<16 && !m.path.empty();++substep) {
          const auto next=m.path.front();
          if(!m.reserved) {
            const auto after=m.path.size()>1?std::optional<TileCoord>{m.path[1]}:std::nullopt;
            m.track=routeTrack(t.current,t.cell,next,after,identity.id,type);
            m.reserved=next; m.segmentProgress=0;
          }
          if(!m.segmentProgress && t.heading!=m.track.startHeading) {
            t.heading=t.bodyTurn.update(t.heading,m.track.startHeading,type.rot,now+frame);
            if(t.heading!=m.track.startHeading) break;
          }
          if(allowance<0) {
            const auto speed=compat::movementLeptons(type.speed)*motion::lepton*
              travelSpeed(type,t.cell,cellAt(t.current))+m.speedRemainder;
            allowance=speed/100; m.speedRemainder=speed%100;
          }
          auto used=std::min(allowance,m.track.length()-m.segmentProgress);
          const auto turnBudget=type.rot==0?32768:std::clamp(type.rot,1,127)*256;
          if(std::abs(motion::angleDelta(t.heading,m.track.heading(m.segmentProgress+used)))>turnBudget) {
            std::int64_t low=0,high=used;
            while(high-low>1) {
              const auto mid=(low+high)/2;
              if(std::abs(motion::angleDelta(t.heading,m.track.heading(m.segmentProgress+mid)))<=turnBudget) low=mid;
              else high=mid;
            }
            used=low; allowance=used;
          }
          m.segmentProgress+=used; allowance-=used;
          t.current=planar(m.track.at(m.segmentProgress)); t.heading=m.track.heading(m.segmentProgress);
          t.bodyTurn.reset(t.heading);
          if(m.segmentProgress<m.track.length()) break;
          t.cell=next; m.reserved.reset(); m.path.pop_front(); m.segmentProgress=0;
          if(++completed>=segmentLimit || allowance==0) break;
        }
      }
      result.push_back({t.current,t.cell});
      if(initial && completed>=segmentLimit) break;
    }
    return result;
  }
  bool forecastConflict(const std::vector<FuturePose>& a,const std::vector<FuturePose>& b) {
    for(std::size_t frame=1;frame<std::min(a.size(),b.size());++frame) {
      ++movementStats.forecastChecks;
      if(motionConflict(a[frame-1],b[frame-1],a[frame],b[frame])) return true;
    }
    return false;
  }
  void planCrossings(const std::vector<entt::entity>& order) {
    forecasts.clear(); forecastHolds.clear();
    laneApproachHolds.clear();
    // Request the direction lease up to three cells before entering a narrow
    // chain. Opposing queues wait outside its mouth and let the current batch
    // drain. An unused approach lease expires, so a blocked winner cannot own
    // an empty corridor indefinitely.
    for(const auto e:order) {
      const auto& m=registry.get<Mobile>(e);
      if(m.yielding || registry.any_of<FactoryExit,Deploy>(e)) continue;
      auto previous=registry.get<Transform>(e).cell; int steps=0;
      for(const auto c:m.path) {
        if(++steps>3) break;
        const auto entry=laneEntry(e,c,previous); previous=c;
        if(!entry) continue;
        auto& lane=trafficLanes.at(entry->first);
        if(!lane.direction) { lane.direction=entry->second; lane.approachUntil=now+96; }
        if(lane.direction!=entry->second) {
          lane.draining=true; laneApproachHolds.insert(registry.get<Identity>(e).id);
        } else if(lane.draining && lane.admitted>=3) laneApproachHolds.insert(registry.get<Identity>(e).id);
        break;
      }
    }
    for(const auto e:order) {
      const auto id=registry.get<Identity>(e).id;
      forecasts.emplace(id,forecast(e,laneApproachHolds.contains(id) && !registry.get<Mobile>(e).reserved?80:0));
    }
    std::size_t attempts=0;
    for(std::size_t i=0;i<order.size() && attempts<256;++i) {
      const auto e=order[i]; const auto& m=registry.get<Mobile>(e); const auto& t=registry.get<Transform>(e);
      const auto id=registry.get<Identity>(e).id;
      if(m.reserved || m.path.empty() || m.yielding || laneApproachHolds.contains(id) || distanceSquared(t.current,position(m.destination))<square(4000) ||
          registry.any_of<FactoryExit,Deploy>(e)) continue;
      std::vector<EntityId> crossing;
      const auto ownDirection=direction({m.path.front().x*1000LL-t.current.x,m.path.front().y*1000LL-t.current.y});
      for(std::size_t j=0;j<i;++j) {
        const auto peer=order[j]; const auto& p=registry.get<Transform>(peer); const auto& other=registry.get<Mobile>(peer);
        if(other.path.empty() || distanceSquared(t.current,p.current)>square(6000)) continue;
        const auto peerDirection=direction({other.path.front().x*1000LL-p.current.x,other.path.front().y*1000LL-p.current.y});
        const auto angle=std::abs(motion::angleDelta(ownDirection,peerDirection));
        // Convoys can advance together. Head-on single-lane traffic uses the
        // corridor lease/passing-bay logic, not alternating launch delays.
        if(angle<12288 || angle>20480) continue;
        crossing.push_back(registry.get<Identity>(peer).id);
      }
      EntityId blocker=0;
      for(const auto peer:crossing) if(forecastConflict(forecasts.at(id),forecasts.at(peer))) { blocker=peer; break; }
      if(!blocker) continue;
      for(int delay=4;delay<=64 && attempts<256;delay+=4) {
        ++attempts; auto delayed=forecast(e,delay);
        if(std::any_of(crossing.begin(),crossing.end(),[&](EntityId peer) { return forecastConflict(delayed,forecasts.at(peer)); })) continue;
        forecasts.at(id)=std::move(delayed); forecastHolds[id]=blocker; ++movementStats.forecastHolds; break;
      }
    }
  }
  EntityId segmentBlocker(entt::entity entity,const motion::DriveSegment& track) {
    const auto& t=registry.get<Transform>(entity);
    const auto parallel=[&](const motion::DriveSegment& other) {
      if(track.startHeading!=other.startHeading || track.endHeading!=other.endHeading) return false;
      if(track.startHeading==track.endHeading) return true;
      const auto offset=track.points.front()-other.points.front();
      for(std::size_t i=0;i<track.points.size();++i)
        if(motion::length(track.points[i]-other.points[i]-offset)>2*motion::scale) return false;
      return true;
    };
    const auto samples=[](const motion::DriveSegment& route,std::int64_t progress) {
      std::vector<Position> points;
      for(auto at=progress;at<route.length();at+=80*motion::scale) points.push_back(planar(route.at(at)));
      points.push_back(planar(route.points.back())); return points;
    };
    const auto own=samples(track,0);
    std::optional<std::vector<FuturePose>> timedOwn;
    for(const auto& [id,peer]:entities) {
      if(peer==entity || !registry.all_of<Mobile>(peer)) continue;
      const auto& p=registry.get<Transform>(peer); const auto& m=registry.get<Mobile>(peer);
      if(distanceSquared(t.current,p.current)>square(4000)) continue;
      if(!registry.any_of<FactoryExit,Deploy>(peer)) {
        if(m.reserved && parallel(m.track)) continue;
        if(m.reserved && m.driving) {
          // A committed crossing is safe when the vehicles reach it at
          // different times. Do not reserve its whole geometric sweep forever.
          // Predict only the peer's committed segment, then hold its endpoint;
          // later segments have not earned right of way yet.
          const auto angle=std::abs(motion::angleDelta(track.startHeading,m.track.startHeading));
          if(angle>=4096 && angle<=28672) {
            if(!timedOwn) timedOwn=forecast(entity,0,1,&track);
            if(!forecastConflict(*timedOwn,forecast(peer,0,1))) continue;
          }
        }
        if(!m.reserved && !m.path.empty()) {
          const auto after=m.path.size()>1?std::optional<TileCoord>{m.path[1]}:std::nullopt;
          const auto proposed=routeTrack(p.current,p.cell,m.path.front(),after,id,*registry.get<Identity>(peer).type);
          if(parallel(proposed) && p.heading==t.heading) continue;
        }
      }
      const auto other=m.reserved?samples(m.track,m.segmentProgress):std::vector<Position>{p.current};
      for(const auto a:own) for(const auto b:other)
        if(distanceSquared(a,b)<square(502)) {
          // A stopped partial segment can be 500..502 subcells away from a
          // peer. Its already-safe initial pose must not prohibit retreating
          // away forever; continuous per-frame checks still enforce 500.
          if(a==t.current && b==p.current && distanceSquared(a,b)>=square(500)) continue;
          return id;
        }
    }
    return 0;
  }
  void movement(entt::entity entity) {
    if(grappled(entity))return;
    auto* mobile = registry.try_get<Mobile>(entity);
    if (!mobile || !complete(entity)) return;
    const auto* exit = registry.try_get<FactoryExit>(entity);
    if (exit) {
      const auto factory = entities.find(exit->factory);
      if (factory != entities.end() && registry.get<Factory>(factory->second).phase != FactoryExitPhase::Driving) return;
    }
    auto& transform = registry.get<Transform>(entity);
    const auto& identity = registry.get<Identity>(entity);
    if (mobile->path.empty()) { mobile->driving=false; return; }
    std::int64_t distanceBudget=-1;
    for (int segment=0;segment<16 && !mobile->path.empty();++segment) {
      const auto next=mobile->path.front();
      if (!mobile->reserved && !prepareTrack(entity,next,exit)) {
        mobile->driving=transform.current!=transform.previous;
        if (!exit && !mobile->driving && !mobile->waitSince) mobile->waitSince=now;
        return;
      }
      if (!mobile->segmentProgress && transform.heading!=mobile->track.startHeading) {
        transform.heading=transform.bodyTurn.update(transform.heading,mobile->track.startHeading,identity.type->rot,now);
        if (transform.heading!=mobile->track.startHeading) { mobile->driving=false; return; }
      }
      if (distanceBudget<0) {
        const auto speed=compat::movementLeptons(identity.type->speed)*motion::lepton*(veteranAbility(entity,"FASTER")?int(definitions.rules().number("General","VeteranSpeed",1.2)*1000+.5):1000)/1000*
                         travelSpeed(*identity.type,transform.cell,cellAt(transform.current))+mobile->speedRemainder;
        distanceBudget=speed/100; mobile->speedRemainder=speed%100;
      }
      auto used=std::min(distanceBudget,mobile->track.length()-mobile->segmentProgress);
      const int turnBudget=identity.type->rot==0?32768:std::clamp(identity.type->rot,1,127)*256;
      if (std::abs(motion::angleDelta(transform.heading,mobile->track.heading(mobile->segmentProgress+used)))>turnBudget) {
        std::int64_t low=0,high=used;
        while (high-low>1) {
          const auto mid=(low+high)/2;
          if (std::abs(motion::angleDelta(transform.heading,mobile->track.heading(mobile->segmentProgress+mid)))<=turnBudget) low=mid;
          else high=mid;
        }
        used=low; distanceBudget=used;
      }
      mobile->segmentProgress+=used; distanceBudget-=used;
      transform.current=planar(mobile->track.at(mobile->segmentProgress));
      transform.heading=mobile->track.heading(mobile->segmentProgress);
      transform.bodyTurn.reset(transform.heading); mobile->driving=used>0;
      if(used>0) { mobile->waitSince=0; mobile->blockedBy=0; }
      if (mobile->segmentProgress<mobile->track.length()) return;
      mobile->sweptReservations.clear();
      transform.cell=next; // A rounded corner can finish off-centre inside this cell.
      mobile->reserved.reset(); mobile->path.pop_front(); mobile->segmentProgress=0;
      if (distanceBudget==0) break;
    }
    if (mobile->path.empty()) {
      mobile->driving=false;
      // A short flow prefix is not the end of the player's movement order.
      if(mobile->flowGroup && transform.cell!=mobile->destination) mobile->requested=true;
    }
  }
  void advanceMovement(const std::vector<entt::entity>& order) {
    planCrossings(order);
    struct Proposal { entt::entity entity; Transform before; Mobile mobile; bool accepted=true; EntityId blocker=0; };
    std::vector<Proposal> proposals;
    for(const auto e:order) proposals.push_back({e,registry.get<Transform>(e),registry.get<Mobile>(e)});
    // Every proposal reads the same occupancy snapshot. In particular, a
    // follower can enter a cell in the frame that its leader leaves it.
    for(const auto e:order) movement(e);
    const auto conflict=[](Position a,Position b,Position aNext,Position bNext) {
      const Position r{a.x-b.x,a.y-b.y},v{aNext.x-a.x-bNext.x+b.x,aNext.y-a.y-bNext.y+b.y};
      const auto length=square(v.x)+square(v.y);
      const auto fraction=length?std::clamp<std::int64_t>(-(r.x*v.x+r.y*v.y)*1024/length,0,1024):0;
      // Half a cell of separation, checked over the entire frame, not only
      // its endpoints. The margin also covers the subcell rounding of bends.
      return square(r.x+v.x*fraction/1024)+square(r.y+v.y*fraction/1024)<square(500);
    };
    bool changed;
    do {
      changed=false;
      for(std::size_t i=0;i<proposals.size();++i) for(std::size_t j=i+1;j<proposals.size();++j) {
        auto& a=proposals[i]; auto& b=proposals[j];
        if(!a.accepted && !b.accepted) continue;
        const auto& at=a.accepted?registry.get<Transform>(a.entity):a.before;
        const auto& bt=b.accepted?registry.get<Transform>(b.entity):b.before;
        if(at.cell!=bt.cell && !conflict(a.before.current,b.before.current,at.current,bt.current)) continue;
        // Stable traffic priority resolves a genuine conflict. Recheck until
        // all accepted moves also avoid units whose proposal was stopped.
        const bool aAlone=at.cell!=b.before.cell && !conflict(a.before.current,b.before.current,at.current,b.before.current);
        const bool bAlone=bt.cell!=a.before.cell && !conflict(a.before.current,b.before.current,a.before.current,bt.current);
        if(a.accepted && (!aAlone || !b.accepted)) { a.accepted=false; a.blocker=registry.get<Identity>(b.entity).id; changed=true; }
        if(b.accepted && (!bAlone || (aAlone && a.accepted))) { b.accepted=false; b.blocker=registry.get<Identity>(a.entity).id; changed=true; }
      }
    } while(changed);
    for(auto& p:proposals) {
      if(!p.accepted) {
        registry.get<Transform>(p.entity)=p.before;
        auto& m=registry.get<Mobile>(p.entity); m=std::move(p.mobile); m.driving=false;
        m.blockedBy=p.blocker;
        if(!m.waitSince) m.waitSince=now;
        if(m.reserved && now-m.waitSince>=24 && !registry.any_of<FactoryExit,Deploy>(p.entity)) {
          // A stopped leader or opposing traffic can obstruct a partial
          // segment. Replan from the actual pose; never snap to a cell centre.
          m.reserved.reset(); m.segmentProgress=0; m.sweptReservations.clear();
        }
      }
      if(registry.get<Mobile>(p.entity).waitSince) ++movementStats.waitingUnits;
    }
    std::set<CellKey> changedCells;
    for(const auto& [cell,id]:occupied) { (void)id; changedCells.insert(cell); }
    occupied.clear();
    for(auto it=reservations.begin();it!=reservations.end();) {
      const auto e=entities.at(it->second);
      if(registry.all_of<Mobile>(e) && !registry.any_of<FactoryExit,Deploy>(e)) {
        changedCells.insert(it->first); it=reservations.erase(it);
      } else ++it;
    }
    for(const auto e:order) {
      const auto id=registry.get<Identity>(e).id;
      const auto c=registry.get<Transform>(e).cell; occupied[key(c)]=id; changedCells.insert(key(c));
      const auto& m=registry.get<Mobile>(e);
      for(const auto cell:m.sweptReservations) { reservations.try_emplace(key(cell),id); changedCells.insert(key(cell)); }
    }
    for(const auto& cell:changedCells) refresh({cell.first,cell.second});
  }
  int cargoUsed(entt::entity host) const {
    int result=0;const auto& type=*registry.get<Identity>(host).type;
    for(const auto id:registry.get<Cargo>(host).passengers)if(const auto it=entities.find(id);it!=entities.end())
      result+=type.garrisonable?1:registry.get<Identity>(it->second).type->size;
    return result;
  }
  int gunnerMode(entt::entity host) const {
    const auto* cargo=registry.try_get<Cargo>(host);
    if(cargo && !cargo->passengers.empty())if(const auto it=entities.find(cargo->passengers.front());it!=entities.end())return registry.get<Identity>(it->second).type->ifvMode;
    return 0;
  }
  int coilChargers(entt::entity target) const {
    const auto& host=registry.get<Identity>(target);
    if(!host.type->overpowerable || !complete(target) || registry.get<Health>(target).hp<=0)return 0;
    int count=0;
    for(const auto& [id,actor]:entities) {
      (void)id;const auto* charger=registry.try_get<CoilCharger>(actor);
      if(!charger || charger->target!=host.id || registry.get<Health>(actor).hp<=0 || registry.any_of<Contained,BuildingInteraction>(actor))continue;
      const auto& source=registry.get<Identity>(actor);const auto* arm=registry.try_get<Armament>(actor);const auto* foot=registry.try_get<Infantry>(actor);
      const auto* weapon=definitions.chargeWeapon(*source.type,*host.type);
      if(source.owner==host.owner && arm && arm->active && arm->target==host.id && foot && !foot->requested && foot->targetSubCell<0 &&
          weapon && distanceSquared(registry.get<Transform>(actor).current,registry.get<Transform>(target).current)<=square(weapon->rangeMilliCells))++count;
    }
    return count;
  }
  bool powered(entt::entity actor) const {
    const auto& identity=registry.get<Identity>(actor);
    return !identity.type->requiresPower() || power(identity.owner).online() || coilChargers(actor)>=2;
  }
  bool overpowered(entt::entity actor) const {
    const auto& identity=registry.get<Identity>(actor);
    return identity.type->overpowerable && coilChargers(actor)>=(power(identity.owner).online()?1:3);
  }
  const WeaponDef* activeWeapon(entt::entity actor,std::optional<bool> targetAir=std::nullopt) const {
    const auto& type=*registry.get<Identity>(actor).type;
    if(const auto* arm=registry.try_get<Armament>(actor);arm && arm->target)if(const auto it=entities.find(arm->target);it!=entities.end()) {
      const auto& host=registry.get<Identity>(it->second);
      if(host.owner==registry.get<Identity>(actor).owner)if(const auto* w=definitions.chargeWeapon(type,*host.type))return w;
    }
    if(overpowered(actor) && !type.secondary.empty()) {
      const auto& w=definitions.weapon(type.secondary);
      if(w.supported && (!targetAir.value_or(false) || definitions.projectile(w.projectile).antiAir))return &w;
    }
    if(type.garrisonable) {const auto* cargo=registry.try_get<Cargo>(actor);if(!cargo || cargo->passengers.empty())return nullptr;}
    bool airborne=targetAir.value_or(false);
    if(!targetAir)if(const auto* arm=registry.try_get<Armament>(actor);arm && arm->target)
      if(const auto it=entities.find(arm->target);it!=entities.end())airborne=airTarget(it->second);
    if(type.garrisonable && canonicalId(definitions.rules().string(definitions.playerHouse(registry.get<Identity>(actor).owner),"Side"))=="NOD") {
      if(type.secondary.empty())return nullptr;
      const auto& w=definitions.weapon(type.secondary);return w.supported && (airborne?definitions.projectile(w.projectile).antiAir:definitions.projectile(w.projectile).antiGround)?&w:nullptr;
    }
    const auto* foot=registry.try_get<Infantry>(actor);
    return definitions.weaponForTarget(type,airborne,foot && foot->posture==InfantryPosture::Deployed,gunnerMode(actor));
  }
  bool movingHost(entt::entity actor) const {
    if(const auto* p=registry.try_get<Pilot>(actor))return p->mission!=AircraftMission::Landed;
    const auto* mobile=registry.try_get<Mobile>(actor);
    return mobile && (mobile->requested || mobile->reserved || !mobile->path.empty());
  }
  std::vector<TileCoord> entryPerimeter(entt::entity host) const {
    const auto cells=footprint(*registry.get<Identity>(host).type,registry.get<Transform>(host).cell);
    std::vector<TileCoord> result;
    for(const auto cell:cells)for(const auto delta:{TileCoord{-1,0},{0,-1},{1,0},{0,1}}) {
      const TileCoord next{cell.x+delta.x,cell.y+delta.y};
      if(std::find(cells.begin(),cells.end(),next)==cells.end() && std::find(result.begin(),result.end(),next)==result.end())result.push_back(next);
    }
    return result;
  }
  // Stock spies have one infiltration effect, selected by the target building.
  // Radar shroud and superweapon countdowns are deliberately unavailable until
  // those systems exist; don't consume a spy for an effect that cannot happen.
  int spyEffect(const UnitTypeDef& type) const {
    if(type.power>0)return 1;
    if(definitions.rules().integer(type.id,"Storage")>0)return 2;
    if(type.factory=="INFANTRYTYPE")return 3;
    if(type.factory=="UNITTYPE" && !type.naval)return 4;
    if(type.id=="GATECH")return 5;
    if(type.id=="NATECH")return 6;
    return 0;
  }
  void infiltrate(entt::entity actor,entt::entity target) {
    const auto owner=registry.get<Identity>(actor).owner,victim=registry.get<Identity>(target).owner;
    auto& bonuses=espionage[owner];
    switch(spyEffect(*registry.get<Identity>(target).type)) {
      case 1:outages[victim]=now+std::max(1,definitions.rules().integer("General","SpyPowerBlackout",1000));message="Enemy power sabotaged";break;
      case 2:{const auto amount=std::int64_t(funds(victim))*std::clamp(definitions.rules().number("General","SpyMoneyStealPercent",.5),0.,1.);
        const int stolen=int(std::min<double>(amount,std::numeric_limits<int>::max()-funds(owner)));
        funds(victim)-=stolen;funds(owner)+=stolen;message="Enemy credits stolen: "+std::to_string(stolen);break;}
      case 3:bonuses.veteranInfantry=true;message="Veteran infantry training acquired";break;
      case 4:bonuses.veteranVehicles=true;message="Veteran vehicle training acquired";break;
      case 5:bonuses.alliedTech=true;message="Allied technology stolen";break;
      case 6:bonuses.sovietTech=true;message="Soviet technology stolen";break;
      default:break;
    }
  }
  void trained(EntityId id,std::uint32_t owner) {
    const auto entity=entities.at(id);const auto& type=*registry.get<Identity>(entity).type;
    const auto it=espionage.find(owner);if(it==espionage.end() || !type.trainable)return;
    if((type.infantry && it->second.veteranInfantry) || (type.category==TypeCategory::Vehicle && !type.naval && it->second.veteranVehicles))registry.emplace_or_replace<Veteran>(entity);
  }
  const WeaponDef* targetWeapon(entt::entity actor,entt::entity target) const {
    const auto& type=*registry.get<Identity>(actor).type;const auto& victim=*registry.get<Identity>(target).type;
    if(type.c4 && victim.category==TypeCategory::Building && !type.secondary.empty())return &definitions.weapon(type.secondary);
    if(!airTarget(target) && definitions.navalSecondary(type,victim,waterAt(registry.get<Transform>(target).cell),submerged(target)) && !type.secondary.empty())return &definitions.weapon(type.secondary);
    return activeWeapon(actor,airTarget(target));
  }
  void chronoStep(entt::entity entity) {
    auto& foot=registry.get<Infantry>(entity);auto& pose=registry.get<Transform>(entity);
    const auto& identity=registry.get<Identity>(entity);
    if(recovering(entity)) {footAnimation(foot,"READY");return;}
    if(const auto* task=registry.try_get<BuildingInteraction>(entity);task && task->entering)return;
    if(foot.firing && now>=foot.attackUntil)foot.firing=false;
    if(!foot.requested){if(!foot.firing)footAnimation(foot,"READY");return;}
    TileCoord destination=foot.destination;int slot=-1;
    // Resolve occupied destinations deterministically, without walking through
    // intervening walls or reserving the destination before the actual jump.
    for(int radius=0;radius<=4 && slot<0;++radius)for(int y=-radius;y<=radius && slot<0;++y)for(int x=-radius;x<=radius && slot<0;++x) {
      if(std::max(std::abs(x),std::abs(y))!=radius)continue;
      const TileCoord c{foot.destination.x+x,foot.destination.y+y};
      if(!validCell(c) || blockedTerrain.contains(key(c)) || travelSpeed(*identity.type,c,c)<=0)continue;
      const auto candidate=availableFootSlot(c,identity.id,identity.owner,foot.subCell);
      if(candidate>=0){destination=c;slot=candidate;}
    }
    if(slot<0){foot.retry=now+8;return;}
    const auto endpoint=footPosition(destination,slot);
    if(destination==pose.cell){foot.requested=false;foot.path.clear();return;}
    const auto& rules=definitions.rules();
    const auto distance=integerSqrt(distanceSquared(pose.current,endpoint)+square((grid.heightAt(endpoint.x,endpoint.y)-height(entity))*1000/256))*256/1000;
    const int minimum=std::max(1,rules.integer("General","ChronoMinimumDelay",16));
    const int delay=rules.boolean("General","ChronoTrigger",true) && distance>=rules.integer("General","ChronoRangeMinimum",0)?
      std::max(minimum,int(distance/std::max(1,rules.integer("General","ChronoDistanceFactor",48)))):minimum;
    effect(fixed(pose.current,height(entity)),false,canonicalId(rules.string("AudioVisual","WarpOut","WARPOUT")),identity.type->id);
    releaseFoot(identity.id);pose.heading=pose.turret=direction({endpoint.x-pose.current.x,endpoint.y-pose.current.y});
    pose.current=pose.previous=endpoint;pose.cell=destination;pose.previousHeading=pose.heading;pose.previousTurret=pose.turret;
    foot.subCell=slot;foot.targetSubCell=-1;foot.destination=destination;foot.path.clear();foot.requested=false;foot.firing=false;
    footOccupied[key(destination)][slot]=identity.id;refresh(destination);footAnimation(foot,"READY");
    registry.emplace_or_replace<ChronoRecovery>(entity,now+delay);
    effect(fixed(endpoint,height(entity)),false,canonicalId(rules.string("AudioVisual","WarpIn","WARPIN")),identity.type->id);
  }
  void eraseTemporal(entt::entity target) {
    const auto& identity=registry.get<Identity>(target);
    effect(fixed(bombPosition(target),height(target)),false,canonicalId(definitions.rules().string("AudioVisual","WarpAway","WARPAWAY")),identity.type->id);
    std::vector<entt::entity> pending{target};
    while(!pending.empty()) {
      const auto victim=pending.back();pending.pop_back();if(registry.all_of<Erased>(victim))continue;
      registry.emplace<Erased>(victim);registry.get<Health>(victim).hp=0;
      registry.remove<AttachedBomb,C4Charge,TemporalChannel>(victim);
      if(const auto* infested=registry.try_get<Infested>(victim);infested && entities.contains(infested->parasite))pending.push_back(entities.at(infested->parasite));
      if(auto* cargo=registry.try_get<Cargo>(victim)){for(const auto id:cargo->passengers)if(entities.contains(id))pending.push_back(entities.at(id));cargo->passengers.clear();}
    }
  }
  void temporalStep(bool advance) {
    std::map<EntityId,std::vector<entt::entity>> channels;
    for(const auto& [id,actor]:entities)if(const auto* channel=registry.try_get<TemporalChannel>(actor)) {
      (void)id;const auto target=entities.find(channel->target);const auto* arm=registry.try_get<Armament>(actor);
      const auto& weapon=definitions.weapon(channel->weapon);
      const bool valid=registry.get<Health>(actor).hp>0 && complete(actor) && !recovering(actor) && !limbo(actor) && !registry.any_of<Contained,FactoryExit>(actor) &&
        target!=entities.end() && registry.get<Health>(target->second).hp>0 && !registry.any_of<Contained,Erased>(target->second) && !limbo(target->second) &&
        registry.get<Identity>(target->second).type->warpable && arm && arm->active && arm->target==channel->target &&
        activeWeapon(actor,false) && activeWeapon(actor,false)->id==channel->weapon &&
        distanceSquared(registry.get<Transform>(actor).current,registry.get<Transform>(target->second).current)<=square(weapon.rangeMilliCells);
      if(!valid){registry.remove<TemporalChannel>(actor);continue;}
      channels[channel->target].push_back(actor);
    }
    for(const auto& [id,victim]:entities)if(auto* temporal=registry.try_get<TemporalVictim>(victim)) {
      const auto it=channels.find(id);
      if(it==channels.end()) {
        const auto duration=now-temporal->started;
        if(auto* foot=registry.try_get<Infantry>(victim)){foot->animationStarted+=duration;foot->fireAt+=duration;foot->attackUntil+=duration;if(foot->postureUntil)foot->postureUntil+=duration;if(foot->proneUntil)foot->proneUntil+=duration;}
        if(auto* structure=registry.try_get<Structure>(victim);structure && structure->complete>temporal->started){structure->started+=duration;structure->complete+=duration;}
        if(auto* factory=registry.try_get<Factory>(victim)){factory->phaseStarted+=duration;factory->cycleStarted+=duration;}
        if(auto* sale=registry.try_get<BuildingSale>(victim)){sale->started+=duration;sale->complete+=duration;}
        if(auto* deploy=registry.try_get<Deploy>(victim)){deploy->started+=duration;deploy->complete+=duration;}
        registry.remove<TemporalVictim>(victim);continue;
      }
      if(!advance)continue;
      int drain=0,count=0;
      for(const auto actor:it->second) {
        if(count++>=51)break; // Native TemporalClass helper chain cap.
        const auto& channel=registry.get<TemporalChannel>(actor);const auto& weapon=definitions.weapon(channel.weapon);
        drain+=weapon.damage;
        const auto& pose=registry.get<Transform>(actor);const auto& identity=registry.get<Identity>(actor);
        const auto start=muzzle(pose,*identity.type,pose.heading,0,identity.type->gunner?gunnerMode(actor):-1);
        const auto end=bombPosition(victim);
        beams.push_back({planar(start),end,int(start.z/motion::lepton),height(victim)+registry.get<Identity>(victim).type->buildingHeight,now,1,identity.owner,weapon.id});
        if(auto* foot=registry.try_get<Infantry>(actor)){foot->firing=true;foot->fired=true;foot->attackUntil=now+2;}
      }
      temporal->remaining-=drain;
      if(temporal->remaining<=0)eraseTemporal(victim);
    }
  }
  void c4Step() {
    for(const auto& [id,entity]:entities)if(const auto* charge=registry.try_get<C4Charge>(entity);charge && now>=charge->detonates && !frozen(entity) && registry.get<Health>(entity).hp>0) {
      (void)id;registry.remove<C4Charge>(entity);registry.get<Health>(entity).hp=0;
      const auto& warhead=definitions.warhead(canonicalId(definitions.rules().string("CombatDamage","C4Warhead","Super")));
      if(!warhead.animations.empty())effect(fixed(bombPosition(entity),height(entity)),true,warhead.animations.back(),registry.get<Identity>(entity).type->id);
    }
  }
  bool interactionTarget(entt::entity actor,const BuildingInteraction& task) const {
    const auto target=entities.find(task.target);if(target==entities.end())return false;
    const auto& identity=registry.get<Identity>(target->second);
    if(registry.get<Health>(target->second).hp<=0 || !complete(target->second) || registry.any_of<FactoryExit,Contained>(target->second))return false;
    const auto owner=registry.get<Identity>(actor).owner;
    if(task.kind==BuildingInteractionKind::Board || task.kind==BuildingInteractionKind::Garrison) {
      const auto* cargo=registry.try_get<Cargo>(target->second);if(!cargo || cargo->unloading)return false;
      const auto& passenger=*registry.get<Identity>(actor).type;
      if(task.kind==BuildingInteractionKind::Board)return (identity.type->groundMobile || (identity.type->aircraft && !identity.type->aircraft->fixedWing && !identity.type->aircraft->balloon)) && identity.owner==owner &&
        (passenger.infantry || (identity.type->naval && !passenger.naval && passenger.groundMobile) || passenger.spriteVehicle) &&
        (!registry.all_of<Cargo>(actor) || registry.get<Cargo>(actor).passengers.empty()) &&
        passenger.size<=identity.type->sizeLimit && cargoUsed(target->second)+passenger.size<=identity.type->passengers &&
        (!identity.type->gunner || (passenger.ifvMode<static_cast<int>(identity.type->weaponSlots.size()) &&
          definitions.weapon(identity.type->weaponSlots[passenger.ifvMode]).supported));
      return passenger.occupier && identity.type->garrisonable && (cargo->passengers.empty() || identity.owner==owner) &&
        cargoUsed(target->second)<identity.type->maxOccupants && std::int64_t(registry.get<Health>(target->second).hp)*compat::kRatioScale>
          std::int64_t(identity.type->strength)*definitions.profile().conditionRedMillionths;
    }
    if(identity.type->category!=TypeCategory::Building || identity.owner!=task.targetOwner)return false;
    if(task.kind==BuildingInteractionKind::Infiltrate)return identity.owner!=owner && identity.type->spyable && spyEffect(*identity.type)!=0;
    return task.kind==BuildingInteractionKind::Capture?identity.owner!=owner && identity.type->capturable:
      identity.owner==owner && identity.type->repairable && registry.get<Health>(target->second).hp<identity.type->strength;
  }
  void enterBuilding(entt::entity actor,EntityId target,bool transport=false) {
    const auto& type=*registry.get<Identity>(actor).type;
    if(limbo(actor) || (!type.infantry && !(transport && type.groundMobile && (!type.naval || type.hover))) || registry.any_of<Contained,Airborne>(actor) || !entities.contains(target))return;
    if(const auto* foot=registry.try_get<Infantry>(actor);foot && (foot->posture==InfantryPosture::Deployed || foot->posture==InfantryPosture::Deploying || foot->posture==InfantryPosture::Undeploying))return;
    const auto targetEntity=entities.at(target);const auto& identity=registry.get<Identity>(targetEntity);
    BuildingInteraction task;task.target=target;task.targetOwner=identity.owner;
    if(transport)task.kind=BuildingInteractionKind::Board;
    else if(type.occupier && identity.type->garrisonable)task.kind=BuildingInteractionKind::Garrison;
    else if(type.spy)task.kind=BuildingInteractionKind::Infiltrate;
    else if(type.engineer)task.kind=identity.owner==registry.get<Identity>(actor).owner?BuildingInteractionKind::Repair:BuildingInteractionKind::Capture;
    else return;
    if(type.spy && !transport && identity.type->spyable && !spyEffect(*identity.type)){message="Radar shroud / superweapon infiltration is not available yet";return;}
    if(!interactionTarget(actor,task)){message="Unit cannot enter this target (capacity, size, ownership or capability)";return;}
    stop(actor);registry.emplace<BuildingInteraction>(actor,task);
    if(transport && registry.all_of<Pilot>(targetEntity)) {stop(targetEntity);landAircraft(targetEntity);}
    message=task.kind==BuildingInteractionKind::Capture?"Engineer moving to capture":task.kind==BuildingInteractionKind::Repair?"Engineer moving to repair":"Infantry approaching entrance";
  }
  void prepareSpriteBoard(entt::entity actor,BuildingInteraction& task,std::size_t& budget) {
    auto& pose=registry.get<Transform>(actor);const auto& identity=registry.get<Identity>(actor);
    const auto target=entities.at(task.target);
    if(task.entering) {
      if(movingHost(target)){cancelBuildingInteraction(actor);return;}
      const auto elapsed=std::min(now-task.started,task.duration);
      pose.current={task.entryStart.x+(task.entryEnd.x-task.entryStart.x)*static_cast<std::int64_t>(elapsed)/static_cast<std::int64_t>(task.duration),
                    task.entryStart.y+(task.entryEnd.y-task.entryStart.y)*static_cast<std::int64_t>(elapsed)/static_cast<std::int64_t>(task.duration)};return;
    }
    auto& mobile=registry.get<Mobile>(actor);if(mobile.reserved || !mobile.path.empty())return;
    const auto perimeter=entryPerimeter(target);
    if(task.planned && std::find(perimeter.begin(),perimeter.end(),task.approach)==perimeter.end()){task.planned=false;mobile.requested=false;}
    if(task.planned && pose.cell==task.approach) {
      if(movingHost(target))return;
      task.entryStart=pose.current;task.entryEnd=registry.get<Transform>(target).current;task.started=now;task.entering=true;
      const auto distance=integerSqrt(distanceSquared(task.entryEnd,task.entryStart));
      task.duration=std::max<Tick>(definitions.profile().engineerEntryFrames,(distance*256+identity.type->speed*1000-1)/(std::max(1,compat::movementLeptons(identity.type->speed))*1000));
      pose.heading=pose.turret=direction({task.entryEnd.x-pose.current.x,task.entryEnd.y-pose.current.y});
      registry.remove<Mobile>(actor);return;
    }
    if(now<task.retry || !budget)return;task.retry=now+8;
    auto candidates=perimeter;std::stable_sort(candidates.begin(),candidates.end(),[&](auto a,auto b){return distanceSquared(position(a),pose.current)<distanceSquared(position(b),pose.current);});
    for(const auto cell:candidates) {
      if(!budget)break;if(!free(cell,identity.id))continue;std::size_t expanded=0;
      const auto path=findTilePath(grid,pose.cell,cell,std::min<std::size_t>(budget,1024),&expanded,
        [&](auto a,auto b){return travelSpeed(*identity.type,a,b);},[&](auto c){return free(c,identity.id);});
      budget-=std::min(budget,expanded);if(!path || path->target!=cell)continue;
      task.planned=true;task.approach=cell;mobile.path.assign(path->cells.begin(),path->cells.end());mobile.destination=cell;mobile.requested=false;return;
    }
  }
  void prepareBuildingInteractions(std::size_t& budget) {
    for(const auto& [id,actor]:entities) {
      auto* task=registry.try_get<BuildingInteraction>(actor);if(!task || registry.get<Health>(actor).hp<=0 || frozen(actor) || recovering(actor))continue;
      if(!interactionTarget(actor,*task)){cancelBuildingInteraction(actor);continue;}
      if(!registry.all_of<Infantry>(actor)){prepareSpriteBoard(actor,*task,budget);continue;}
      auto& foot=registry.get<Infantry>(actor);auto& pose=registry.get<Transform>(actor);
      if(task->entering) {
        if(movingHost(entities.at(task->target))) {cancelBuildingInteraction(actor);continue;}
        const auto elapsed=std::min(now-task->started,task->duration);
        const auto duration=static_cast<std::int64_t>(task->duration);
        pose.current={task->entryStart.x+(task->entryEnd.x-task->entryStart.x)*static_cast<std::int64_t>(elapsed)/duration,
                      task->entryStart.y+(task->entryEnd.y-task->entryStart.y)*static_cast<std::int64_t>(elapsed)/duration};
        footAnimation(foot,"WALK");continue;
      }
      if(foot.targetSubCell>=0 || foot.postureUntil)continue;
      const auto target=entities.at(task->target);const auto& building=registry.get<Transform>(target);
      const auto cells=footprint(*registry.get<Identity>(target).type,building.cell);
      const auto perimeter=entryPerimeter(target);
      if(task->planned && std::find(perimeter.begin(),perimeter.end(),task->approach)==perimeter.end()) {haltFoot(actor);task->planned=false;}
      if(task->planned && !foot.requested && foot.path.empty()) {
        if(pose.cell==task->approach) {
          if(movingHost(target))continue;
          const auto nearest=*std::min_element(cells.begin(),cells.end(),[&](auto a,auto b) {
            return distanceSquared(position(a),pose.current)<distanceSquared(position(b),pose.current);
          });
          task->entryStart=pose.current;task->entryEnd=position(nearest);task->started=now;task->entering=true;
          const auto distance=integerSqrt(distanceSquared(task->entryStart,task->entryEnd));
          const auto speed=std::max(1,compat::movementLeptons(registry.get<Identity>(actor).type->speed));
          task->duration=std::max<Tick>(definitions.profile().engineerEntryFrames,(distance*256+speed*1000-1)/(speed*1000));
          pose.heading=direction({task->entryEnd.x-pose.current.x,task->entryEnd.y-pose.current.y});pose.turret=pose.heading;
          foot.firing=false;footAnimation(foot,"WALK");continue;
        }
        task->planned=false;
      }
      if(task->planned || now<task->retry || !budget)continue;
      std::vector<TileCoord> candidates;
      for(const auto next:perimeter)if(availableFootSlot(next,id,registry.get<Identity>(actor).owner)>=0)candidates.push_back(next);
      std::stable_sort(candidates.begin(),candidates.end(),[&](auto a,auto b){return distanceSquared(position(a),pose.current)<distanceSquared(position(b),pose.current);});
      task->retry=now+8;
      const auto& type=*registry.get<Identity>(actor).type;
      for(const auto candidate:candidates) {
        if(type.teleporter && travelSpeed(type,candidate,candidate)>0) {
          task->approach=candidate;task->planned=true;foot.destination=candidate;foot.requested=pose.cell!=candidate;break;
        }
        if(!budget)break;std::size_t expanded=0;
        const auto path=findTilePath(grid,pose.cell,candidate,std::min<std::size_t>(budget,1024),&expanded,
          [&](auto a,auto b){return travelSpeed(type,a,b);},
          [&](auto cell){return availableFootSlot(cell,id,registry.get<Identity>(actor).owner)>=0;});
        budget-=std::min(budget,expanded);
        if(!path || path->target!=candidate)continue;
        task->approach=candidate;task->planned=true;
        foot.path.assign(path->cells.begin(),path->cells.end());foot.destination=candidate;foot.requested=!foot.path.empty();break;
      }
    }
  }
  void completeBuildingInteractions() {
    for(const auto& [id,actor]:entities) {
      (void)id;auto* task=registry.try_get<BuildingInteraction>(actor);
      if(!task || !task->entering || now-task->started<task->duration || registry.get<Health>(actor).hp<=0 || frozen(actor) || recovering(actor))continue;
      if(!interactionTarget(actor,*task)){cancelBuildingInteraction(actor);continue;}
      const auto target=entities.at(task->target);auto& identity=registry.get<Identity>(target);
      if(task->kind==BuildingInteractionKind::Board || task->kind==BuildingInteractionKind::Garrison) {
        const auto host=task->target;const bool garrison=task->kind==BuildingInteractionKind::Garrison;
        stop(actor);release(id);
        if(auto* foot=registry.try_get<Infantry>(actor))*foot=Infantry{};
        registry.remove<Mobile>(actor);
        registry.emplace<Contained>(actor,host);registry.get<Cargo>(target).passengers.push_back(id);
        if(garrison)identity.owner=registry.get<Identity>(actor).owner;
        if(auto* arm=registry.try_get<Armament>(target))*arm={};
        message=garrison?"Building garrisoned":"Passenger boarded";continue;
      }
      if(task->kind==BuildingInteractionKind::Infiltrate) {
        infiltrate(actor,target);task=registry.try_get<BuildingInteraction>(actor);
        task->consumed=true;registry.get<Health>(actor).hp=0;continue;
      }
      if(task->kind==BuildingInteractionKind::Repair)registry.get<Health>(target).hp=identity.type->strength;
      else {
        const auto oldOwner=identity.owner;identity.owner=registry.get<Identity>(actor).owner;
        if(auto* cargo=registry.try_get<Cargo>(target);cargo && !cargo->passengers.empty()) {
          cargo->originalOwner=identity.owner;cargo->unloading=true;cargo->retry=now;
        }
        if(auto* factory=registry.try_get<Factory>(target)) {
          for(const auto& item:factory->queue)funds(oldOwner)=static_cast<int>(std::min<std::int64_t>(std::numeric_limits<int>::max(),std::int64_t(funds(oldOwner))+item.paid));
          factory->queue.clear();factory->rally.reset();
          if(factory->phase==FactoryExitPhase::Blocked)factory->phase=FactoryExitPhase::Idle;
        }
        if(auto* arm=registry.try_get<Armament>(target))*arm={};
        harvesting.captured(task->target,*this);
      }
      message=task->kind==BuildingInteractionKind::Capture?"Building captured":"Building repaired";
      task->consumed=true;registry.get<Health>(actor).hp=0;
    }
  }
  void unload(entt::entity host) {
    auto* cargo=registry.try_get<Cargo>(host);if(!cargo || cargo->passengers.empty())return;
    stop(host);if(registry.all_of<Pilot>(host))landAircraft(host);cargo->unloading=true;cargo->retry=now;message="Unloading passengers";
  }
  bool unloadOne(entt::entity host) {
    auto& cargo=registry.get<Cargo>(host);if(cargo.passengers.empty())return true;
    const auto id=cargo.passengers.front();const auto actor=entities.at(id);
    const auto& identity=registry.get<Identity>(actor);
    for(const auto cell:entryPerimeter(host)) {
      const auto slot=identity.type->infantry?availableFootSlot(cell,id,identity.owner):-1;
      if((identity.type->infantry?slot<0:!free(cell,id)) || travelSpeed(*identity.type,cell,cell)<=0)continue;
      registry.remove<Contained>(actor);
      auto& pose=registry.get<Transform>(actor);pose.cell=cell;pose.previous=pose.current=identity.type->infantry?footPosition(cell,slot):position(cell);pose.animationSpeed=clock.speed();
      if(auto* foot=registry.try_get<Infantry>(actor)) {
        *foot=Infantry{};foot->subCell=slot;foot->destination=cell;foot->animationStarted=now;footOccupied[key(cell)][slot]=id;
      } else {registry.emplace_or_replace<Mobile>(actor,cell);occupied[key(cell)]=id;}
      refresh(cell);cargo.passengers.erase(cargo.passengers.begin());
      if(!identity.type->infantry) {
        const auto center=registry.get<Transform>(host).cell;
        move(actor,{cell.x+2*(cell.x-center.x),cell.y+2*(cell.y-center.y)});
      }
      if(auto* arm=registry.try_get<Armament>(host))*arm={};
      if(cargo.passengers.empty()) {
        cargo.unloading=false;
        if(registry.get<Identity>(host).type->garrisonable)registry.get<Identity>(host).owner=cargo.originalOwner;
      }
      return true;
    }
    return false;
  }
  void cargoStep() {
    for(const auto& [id,host]:entities) {
      auto* cargo=registry.try_get<Cargo>(host);if(!cargo)continue;
      const auto& type=*registry.get<Identity>(host).type;const int hp=registry.get<Health>(host).hp;
      if(hp<=0) {
        continue; // resolveDeaths handles destruction before gunner mode is lost.
      }
      const auto red=definitions.profile().conditionRedMillionths;
      if(type.garrisonable && hp*std::int64_t(compat::kRatioScale)<=type.strength*static_cast<std::int64_t>(red))cargo->unloading=true;
      for(const auto passenger:cargo->passengers) {
        const auto actor=entities.at(passenger);auto& pose=registry.get<Transform>(actor);const auto& carrier=registry.get<Transform>(host);
        pose.cell=carrier.cell;pose.previous=carrier.previous;pose.current=carrier.current;
        if(registry.get<Identity>(actor).owner!=registry.get<Identity>(host).owner)cargo->unloading=true;
      }
      if(cargo->unloading && now>=cargo->retry && !movingHost(host) && !registry.all_of<FactoryExit>(host)) {
        if(!unloadOne(host))message="Unload exit blocked; passengers retained";
        cargo->retry=now+definitions.profile().unloadFrames;
        if(cargo->passengers.empty())cargo->unloading=false;
      }
    }
  }
  bool validAirport(entt::entity plane,EntityId dock) const {
    const auto it=entities.find(dock);if(it==entities.end())return false;
    const auto& source=registry.get<Identity>(plane);const auto& host=registry.get<Identity>(it->second);
    const auto& docks=source.type->aircraft->docks;
    return host.owner==source.owner && host.type->airport && registry.get<Health>(it->second).hp>0 && complete(it->second) &&
      std::find(docks.begin(),docks.end(),host.type->id)!=docks.end();
  }
  int freeDock(EntityId airport,EntityId self=0) const {
    const auto it=entities.find(airport);if(it==entities.end())return -1;
    const auto& config=*registry.get<Identity>(it->second).type->airport;
    for(int slot=0;slot<int(config.offsets.size());++slot) {
      bool used=false;for(const auto& [id,e]:entities)if(id!=self && registry.get<Health>(e).hp>0)
        if(const auto* p=registry.try_get<Pilot>(e);p && p->airport==airport && p->slot==slot){used=true;break;}
      if(!used)return slot;
    }
    return -1;
  }
  motion::Vector dockPosition(EntityId airport,int slot) const {
    const auto host=entities.at(airport);const auto& pose=registry.get<Transform>(host);const auto& type=*registry.get<Identity>(host).type;
    const auto& offset=type.airport->offsets.at(slot);
    // DockingOffset is a native world-space offset from the foundation center.
    return fixed({pose.current.x+(type.foundationWidth-1)*500,pose.current.y+(type.foundationHeight-1)*500},grid.heightAt(pose.current.x,pose.current.y))+
      motion::Vector{offset[0]*motion::lepton,offset[1]*motion::lepton,offset[2]*motion::lepton};
  }
  bool assignAirport(entt::entity plane,EntityId requested=0) {
    auto& pilot=registry.get<Pilot>(plane);
    if(!requested && pilot.airport && validAirport(plane,pilot.airport))return true;
    std::vector<std::pair<std::int64_t,EntityId>> choices;
    for(const auto& [id,e]:entities)if((!requested || requested==id) && validAirport(plane,id) && freeDock(id,registry.get<Identity>(plane).id)>=0)
      choices.emplace_back(distanceSquared(registry.get<Transform>(plane).current,registry.get<Transform>(e).current),id);
    std::sort(choices.begin(),choices.end());
    if(choices.empty()){if(!requested){pilot.airport=0;pilot.slot=-1;}return false;}
    pilot.airport=choices.front().second;pilot.slot=freeDock(pilot.airport,registry.get<Identity>(plane).id);return true;
  }
  void returnAircraft(entt::entity plane,EntityId dock=0) {
    auto& pilot=registry.get<Pilot>(plane);
    if(!assignAirport(plane,dock)) {pilot.mission=AircraftMission::Holding;return;}
    pilot.mission=AircraftMission::Returning;
    if(auto* arm=registry.try_get<Armament>(plane))*arm={};
  }
  bool landingSite(TileCoord cell,EntityId id) const {
    return validCell(cell) && free(cell,id) && !waterAt(cell) && !grid.cell(cell).surface.cliff && !grid.cell(cell).surface.wallHeightLeptons;
  }
  void landAircraft(entt::entity plane) {
    auto& pilot=registry.get<Pilot>(plane);const auto& identity=registry.get<Identity>(plane);
    if(identity.type->aircraft->fixedWing){returnAircraft(plane);return;}
    if(identity.type->aircraft->balloon)return;
    const auto here=registry.get<Transform>(plane).cell;
    for(int radius=0;radius<=8;++radius)for(int y=-radius;y<=radius;++y)for(int x=-radius;x<=radius;++x) {
      if(std::max(std::abs(x),std::abs(y))!=radius)continue;
      const TileCoord cell{here.x+x,here.y+y};if(!landingSite(cell,identity.id))continue;
      pilot.destination=position(cell);pilot.mission=AircraftMission::Landing;
      reservations[key(cell)]=identity.id;refresh(cell);return;
    }
    pilot.mission=AircraftMission::Holding;message="Helicopter waiting for a clear landing site";
  }
  void aircraftStep() {
    for(const auto& [id,entity]:entities) {
      auto* pilot=registry.try_get<Pilot>(entity);if(!pilot || registry.get<Health>(entity).hp<=0 || frozen(entity))continue;
      const auto& type=*registry.get<Identity>(entity).type;const auto& config=*type.aircraft;auto& pose=registry.get<Transform>(entity);
      auto* arm=registry.try_get<Armament>(entity);
      pilot->flight.previous=pilot->flight.position;
      if(const auto* exit=registry.try_get<FactoryExit>(entity))if(const auto factory=entities.find(exit->factory);factory!=entities.end())
        if(registry.get<Factory>(factory->second).phase==FactoryExitPhase::Preparing)continue;
      if(config.fixedWing && (!pilot->airport || !validAirport(entity,pilot->airport))) {
        if(!assignAirport(entity)) {
          if(pilot->mission==AircraftMission::Landed || pilot->ammo==0 || pilot->mission==AircraftMission::Returning || pilot->mission==AircraftMission::Landing){registry.get<Health>(entity).hp=0;continue;}
        } else if(pilot->mission==AircraftMission::Landed)pilot->mission=AircraftMission::Returning;
      }
      if(config.fixedWing && pilot->mission==AircraftMission::Landed) {
        if(pilot->ammo<config.ammo) {
          // Native ReloadRate counts powered game frames, not render time.
          if(power(registry.get<Identity>(entity).owner).online() && pilot->reload>0)--pilot->reload;
          if(pilot->reload==0){++pilot->ammo;pilot->reload=config.reloadFrames;}
        }
        if(now>=pilot->repairNext && power(registry.get<Identity>(entity).owner).online()) {
          auto& hp=registry.get<Health>(entity).hp;const int heal=std::min(definitions.profile().repairStep,type.strength-hp);
          const int price=compat::buildingRepairCost(type.cost,type.strength,heal,definitions.profile().repairPercentMillionths);
          auto& credits=funds(registry.get<Identity>(entity).owner);
          if(heal>0 && credits>=price){hp+=heal;credits-=price;}pilot->repairNext=now+definitions.profile().repairFrames;
        }
        if(arm && arm->active && pilot->ammo>0)pilot->mission=AircraftMission::Attacking;
      }
      if(arm && arm->active && (!config.fixedWing || pilot->ammo>0) && pilot->mission!=AircraftMission::Returning && pilot->mission!=AircraftMission::Landing)
        pilot->mission=AircraftMission::Attacking;
      if(pilot->mission==AircraftMission::Attacking) {
        if(!arm || !arm->active || (arm->target && (!entities.contains(arm->target) || registry.get<Health>(entities.at(arm->target)).hp<=0))) {
          if(arm){arm->active=false;arm->target=0;arm->burstRemaining=0;}
          if(config.fixedWing)returnAircraft(entity);else pilot->mission=AircraftMission::Holding;
        } else {
          const auto target=arm->target?registry.get<Transform>(entities.at(arm->target)).current:arm->ground;
          pilot->destination=target;
          const auto* weapon=arm->target?targetWeapon(entity,entities.at(arm->target)):activeWeapon(entity,false);
          if(!weapon || (arm->target && (registry.get<Identity>(entities.at(arm->target)).owner==registry.get<Identity>(entity).owner ||
              !definitions.targetDomainAllowed(type,airTarget(entities.at(arm->target)),waterAt(cellAt(target)),submerged(entities.at(arm->target)))))) {
            arm->active=false;arm->target=0;arm->burstRemaining=0;
            if(config.fixedWing)returnAircraft(entity);else pilot->mission=AircraftMission::Holding;
          }
        }
      }
      if(pilot->mission==AircraftMission::Holding && !config.fixedWing && !config.balloon) {
        const auto* cargo=registry.try_get<Cargo>(entity);if(cargo && cargo->unloading && now%16==0)landAircraft(entity);
      }
      if(pilot->mission==AircraftMission::Landed)continue;
      if(config.fixedWing && pilot->ammo==0 && (!arm || !arm->burstRemaining))returnAircraft(entity);
      auto target=fixed(pilot->destination);int altitude=flightSurface(pose.cell)+config.movement.height;
      bool landing=false;int radius=config.fixedWing?512:16;
      if(pilot->mission==AircraftMission::Returning || (config.fixedWing && pilot->mission==AircraftMission::Landing)) {
        if(!pilot->airport || !validAirport(entity,pilot->airport)){returnAircraft(entity);if(!pilot->airport){registry.get<Health>(entity).hp=0;continue;}}
        target=dockPosition(pilot->airport,pilot->slot);pilot->destination=planar(target);
        if(distanceSquared(pose.current,planar(target))<square(1500))pilot->mission=AircraftMission::Landing;
        landing=pilot->mission==AircraftMission::Landing;
        if(landing){altitude=int(target.z/motion::lepton);radius=8;}
      } else if(pilot->mission==AircraftMission::Landing) {
        const auto cell=cellAt(pilot->destination);
        if(!landingSite(cell,id)){release(id);pilot->mission=AircraftMission::Holding;landAircraft(entity);continue;}
        const bool above=distanceSquared(pose.current,pilot->destination)<square(100);
        landing=true;radius=8;altitude=above?grid.heightAt(pilot->destination.x,pilot->destination.y):altitude;
      }
      bool travel=true;
      if(pilot->mission==AircraftMission::Holding && !config.fixedWing){target=pilot->flight.position;travel=false;}
      if(pilot->mission==AircraftMission::Attacking && !config.fixedWing && arm && arm->active) {
        const auto* weapon=activeWeapon(entity,false);const int range=config.balloon?400:weapon?weapon->rangeMilliCells:0;
        if(distanceSquared(pose.current,pilot->destination)<=square(range)){target=pilot->flight.position;travel=false;}
      }
      if(pilot->mission!=AircraftMission::Landing)release(id);
      const bool arrived=aircraft::advance(pilot->flight,config,target,altitude,radius,landing);
      pose.previous=planar(pilot->flight.previous);pose.current=planar(pilot->flight.position);pose.cell=cellAt(pose.current);pose.heading=pose.turret=pilot->flight.heading;
      bool landingAligned=true;
      if(config.fixedWing && arrived && pilot->mission==AircraftMission::Landing) {
        const int desired=((definitions.rules().integer("AudioVisual","PoseDir",2)+5)&7)*8192;
        const int delta=motion::angleDelta(pilot->flight.heading,desired),turn=config.movement.turnRate*256;
        pilot->flight.heading=(pilot->flight.heading+std::clamp(delta,-turn,turn)+65536)%65536;
        pose.heading=pose.turret=pilot->flight.heading;landingAligned=std::abs(delta)<=turn;
      }
      if(arrived && landingAligned && pilot->mission==AircraftMission::Landing && std::abs(int(pilot->flight.position.z/motion::lepton)-altitude)<=1) {
        pilot->flight.position=target;pilot->flight.speedMilli=0;pose.current=planar(target);pose.cell=cellAt(pose.current);pilot->mission=AircraftMission::Landed;
        pilot->reload=config.reloadFrames;
        if(!config.fixedWing){reservations.erase(key(pose.cell));occupied[key(pose.cell)]=id;refresh(pose.cell);}
      } else if(arrived && pilot->mission==AircraftMission::Moving) {
        if(config.fixedWing)pilot->mission=AircraftMission::Holding;
        else if(!config.balloon)landAircraft(entity);
        else pilot->mission=AircraftMission::Holding;
      }
      if(pilot->mission==AircraftMission::Holding && config.fixedWing) {
        // Hold a small circuit around the last player waypoint until ordered home.
        const auto vector=motion::unitVector((int(now%240)*65536/240+int(id%8)*8192)&65535);
        pilot->destination={pose.current.x+vector.x*4000/65536,pose.current.y+vector.y*4000/65536};
      }
      if(!travel)pilot->flight.speedMilli=0;
    }
    for(auto& wreck:airWrecks) {
      wreck.previousHeight=wreck.height;wreck.height-=wreck.speed;wreck.speed=std::min(100,wreck.speed+1);
      if(wreck.height<=grid.heightAt(wreck.position.x,wreck.position.y)) {
        effect(fixed(wreck.position,grid.heightAt(wreck.position.x,wreck.position.y)),true,waterAt(cellAt(wreck.position))?"H2O_EXP1":"TWLT070",wreck.type,wreck.heading);
      }
    }
    std::erase_if(airWrecks,[&](const auto& w){return w.height<=grid.heightAt(w.position.x,w.position.y);});
  }
  void beginAirProduction(entt::entity factoryEntity) {
    const auto factoryId=registry.get<Identity>(factoryEntity).id;const auto owner=registry.get<Identity>(factoryEntity).owner;
    const auto typeId=registry.get<Factory>(factoryEntity).queue.front().type;const auto& type=definitions.unit(typeId);
    if(type.aircraft->fixedWing) {
      const int slot=freeDock(factoryId);if(slot<0){registry.get<Factory>(factoryEntity).phase=FactoryExitPhase::Blocked;return;}
      const auto at=dockPosition(factoryId,slot);
      const auto id=spawn(type,cellAt(planar(at)),owner,false,factoryId);if(!id)return;
      auto& pilot=registry.get<Pilot>(entities.at(id));pilot.airport=factoryId;pilot.slot=slot;pilot.flight.position=pilot.flight.previous=at;
      auto& pose=registry.get<Transform>(entities.at(id));pose.current=pose.previous=planar(at);pose.cell=cellAt(pose.current);pose.heading=pose.turret=pilot.flight.heading=((definitions.rules().integer("AudioVisual","PoseDir",2)+5)&7)*8192;
      pose.previousHeading=pose.heading;pose.previousTurret=pose.turret;trained(id,owner);
    } else {
      const auto anchor=registry.get<Transform>(factoryEntity).cell;const auto offset=registry.get<Identity>(factoryEntity).type->factoryExit->path.front();
      const TileCoord cell{anchor.x+offset.x,anchor.y+offset.y};
      if(!free(cell,0,factoryId)){registry.get<Factory>(factoryEntity).phase=FactoryExitPhase::Blocked;return;}
      const auto id=spawn(type,cell,owner,false,factoryId);if(!id)return;trained(id,owner);
      const auto rally=registry.get<Factory>(factoryEntity).rally.value_or(TileCoord{cell.x+3,cell.y+3});move(entities.at(id),rally);
      registry.emplace<FactoryExit>(entities.at(id),factoryId,std::vector<TileCoord>{cell},std::nullopt);
      auto& factory=registry.get<Factory>(factoryEntity);factory.queue.pop_front();factory.exitingUnit=id;
      factory.cycleStarted=factory.phaseStarted=now;factory.animationSpeed=clock.speed();
      factory.phase=registry.get<Identity>(factoryEntity).type->factoryExit->prepareFrames?FactoryExitPhase::Preparing:FactoryExitPhase::Driving;
      return;
    }
    auto& factory=registry.get<Factory>(factoryEntity);factory.queue.pop_front();factory.phase=FactoryExitPhase::Idle;factory.phaseStarted=factory.cycleStarted=now;
  }
  int flightSurface(TileCoord cell) const {
    const auto p=position(cell);int result=grid.heightAt(p.x,p.y);
    if(const auto at=structures.find(key(cell));at!=structures.end())result+=registry.get<Identity>(entities.at(at->second)).type->buildingHeight;
    return result;
  }
  void flightStep(std::size_t& budget) {
    for(const auto& [id,entity]:entities) {
      auto* air=registry.try_get<Airborne>(entity);if(!air || registry.get<Health>(entity).hp<=0)continue;
      auto& pose=registry.get<Transform>(entity);auto& foot=registry.get<Infantry>(entity);const auto& type=*registry.get<Identity>(entity).type;
      const auto& definition=*type.flight;
      if(foot.firing && now>=foot.attackUntil)foot.firing=false;
      if(air->requested && air->path.empty() && pose.cell!=air->destination && now>=air->retry && budget) {
        std::size_t expanded=0;
        const auto path=findTilePath(grid,pose.cell,air->destination,std::min<std::size_t>(budget,1024),&expanded,
          [](TileCoord,TileCoord){return 100;},[&](TileCoord c){return availableAirSlot(c,id)>=0;},false);
        budget-=std::min(budget,expanded);air->retry=now+8;
        if(path)air->path.assign(path->cells.begin(),path->cells.end());
      }
      const auto next=air->path.empty()?air->destination:air->path.front();
      const int slot=next==air->destination?air->destinationSlot:availableAirSlot(next,id,air->subCell);
      const bool canMove=air->requested && !foot.firing && slot>=0 && (pose.cell==air->destination || !air->path.empty()) && availableAirSlot(next,id,slot)==slot;
      const auto target=slot>=0?footPosition(next,slot):pose.current;
      const int surface=std::max(flightSurface(pose.cell),canMove?flightSurface(next):flightSurface(pose.cell));
      const auto advanced=flight::advance(*air,definition,{pose.current.x,pose.current.y,0},{target.x,target.y,0},pose.heading,surface+definition.height,canMove);
      const Position candidate{advanced.position.x,advanced.position.y};const auto cell=cellAt(candidate);
      const int available=availableAirSlot(cell,id,cell==next?slot:air->subCell);
      if(available>=0) {
        if(cell!=pose.cell || available!=air->subCell) {
          auto found=airOccupied.find(key(pose.cell));if(found!=airOccupied.end()) {
            found->second[air->subCell]=0;
            if(std::all_of(found->second.begin(),found->second.end(),[](auto owner){return !owner;}))airOccupied.erase(found);
          }
          airOccupied[key(cell)][available]=id;air->subCell=available;foot.subCell=available;pose.cell=cell;
        }
        pose.current=candidate;
        if(canMove) {pose.heading=advanced.heading;pose.turret=pose.heading;pose.bodyTurn.reset(pose.heading);}
        if(advanced.arrived && air->requested) {
          if(!air->path.empty())air->path.pop_front();
          if(air->path.empty() && pose.cell==air->destination && pose.current==footPosition(air->destination,air->destinationSlot)) {
            air->requested=false;air->speed=0;releaseAir(id,false);air->destinationSlot=-1;
          }
        }
      } else {air->path.clear();air->retry=now+8;}
      if(!foot.firing)footAnimation(foot,pose.current!=pose.previous?"FLY":"HOVER");
    }
  }
  void fallingCorpses() {
    for(auto& corpse:corpses)if(corpse.falling) {
      corpse.previousHeightLeptons=corpse.heightLeptons;
      const auto floor=flightSurface(cellAt(corpse.position));corpse.heightLeptons=std::max(floor,corpse.heightLeptons-corpse.crashSpeed);
      if(corpse.heightLeptons<=floor) {
        corpse.falling=false;corpse.sequence="AIRDEATHFINISH";corpse.started=now;
        const auto& seq=definitions.unit(corpse.type).infantry->sequence(corpse.sequence);corpse.duration=std::max(1,seq.count*seq.delay);
      } else if(corpse.sequence=="AIRDEATHSTART") {
        const auto& seq=definitions.unit(corpse.type).infantry->sequence(corpse.sequence);
        if(now-corpse.started>=static_cast<Tick>(seq.count*seq.delay)) {corpse.sequence="AIRDEATHFALLING";corpse.started=now;}
      }
    }
  }
  void infantryStep(entt::entity entity,std::size_t& budget) {
    auto* foot=registry.try_get<Infantry>(entity);
    if(!foot || limbo(entity) || frozen(entity) || registry.get<Health>(entity).hp<=0 || registry.any_of<Contained,Airborne>(entity)) return;
    if(registry.get<Identity>(entity).type->teleporter) {chronoStep(entity);return;}
    if(const auto* task=registry.try_get<BuildingInteraction>(entity);task && task->entering)return;
    const auto& identity=registry.get<Identity>(entity); auto& pose=registry.get<Transform>(entity);
    if(foot->postureUntil && now>=foot->postureUntil) {
      switch(foot->posture) {
        case InfantryPosture::GoingProne:foot->posture=InfantryPosture::Prone;break;
        case InfantryPosture::GettingUp:case InfantryPosture::Undeploying:foot->posture=InfantryPosture::Standing;break;
        case InfantryPosture::Deploying:foot->posture=InfantryPosture::Deployed;break;
        default:break;
      }
      foot->postureUntil=0;
    }
    if(foot->postureUntil) return;
    if(identity.type->deployFire && foot->posture==InfantryPosture::Deployed && foot->requested) {
      const auto target=foot->destination;deploy(entity);foot->destination=target;foot->requested=true;return;
    }
    if(foot->firing && now>=foot->attackUntil) foot->firing=false;
    if(foot->posture==InfantryPosture::Prone && now>=foot->proneUntil && !foot->firing && foot->targetSubCell<0) {
      foot->posture=InfantryPosture::GettingUp;footAnimation(*foot,"UP",false);
      foot->postureUntil=now+footDuration(identity,"UP");return;
    }
    if(foot->firing) return;
    if(foot->posture==InfantryPosture::Deployed) {footAnimation(*foot,"DEPLOYED");return;}
    const auto passable=[&](TileCoord c){return availableFootSlot(c,identity.id,identity.owner)>=0;};
    if(foot->targetSubCell<0 && foot->path.empty() && foot->requested && now>=foot->retry && budget) {
      std::size_t expanded=0;
      const auto path=findTilePath(grid,pose.cell,foot->destination,std::min<std::size_t>(budget,1024),&expanded,
        [&](TileCoord a,TileCoord b){return travelSpeed(*identity.type,a,b);},passable);
      budget-=std::min(budget,expanded);foot->retry=now+8;
      if(path) {foot->path.assign(path->cells.begin(),path->cells.end());foot->destination=path->target;if(foot->path.empty())foot->requested=false;}
    }
    std::int64_t allowance=-1;
    for(int step=0;step<16 && !foot->path.empty();++step) {
      if(foot->targetSubCell<0) {
        const auto next=foot->path.front();const int slot=availableFootSlot(next,identity.id,identity.owner,foot->subCell);
        const bool diagonal=next.x!=pose.cell.x && next.y!=pose.cell.y;
        if(slot<0 || travelSpeed(*identity.type,pose.cell,next)<=0 ||
            (diagonal && (!passable({next.x,pose.cell.y}) || !passable({pose.cell.x,next.y})))) {
          foot->path.clear();foot->retry=now+8;break;
        }
        foot->next=next;foot->targetSubCell=slot;foot->segmentStart=pose.current;foot->progress=0;
        const auto end=footPosition(next,slot);foot->length=std::max<std::int64_t>(1,integerSqrt(distanceSquared(end,pose.current)));
        footReservations[key(next)][slot]=identity.id;refresh(next);
        pose.heading=direction({end.x-pose.current.x,end.y-pose.current.y});pose.turret=pose.heading;pose.bodyTurn.reset(pose.heading);
      }
      if(allowance<0) {
        const auto stance=foot->posture==InfantryPosture::Prone && identity.type->infantry->crawls?67:100;
        const auto speed=std::int64_t(compat::movementLeptons(identity.type->speed))*(veteranAbility(entity,"FASTER")?int(definitions.rules().number("General","VeteranSpeed",1.2)*1000+.5):1000)*
          travelSpeed(*identity.type,pose.cell,foot->next)*stance+foot->remainder;
        constexpr std::int64_t denominator=256*100*100;
        allowance=speed/denominator;foot->remainder=speed%denominator;
      }
      const auto used=std::min(allowance,foot->length-foot->progress);foot->progress+=used;allowance-=used;
      const auto end=footPosition(foot->next,foot->targetSubCell);
      pose.current={foot->segmentStart.x+(end.x-foot->segmentStart.x)*foot->progress/foot->length,
                    foot->segmentStart.y+(end.y-foot->segmentStart.y)*foot->progress/foot->length};
      if(foot->progress<foot->length) break;
      releaseFoot(identity.id);pose.cell=foot->next;foot->subCell=foot->targetSubCell;foot->targetSubCell=-1;
      footOccupied[key(pose.cell)][foot->subCell]=identity.id;refresh(pose.cell);foot->path.pop_front();
      if(foot->path.empty() && pose.cell==foot->destination) foot->requested=false;
      if(!allowance) break;
    }
    const bool walking=pose.current!=pose.previous;
    footAnimation(*foot,swimming(entity)?(walking?"SWIM":"TREAD"):foot->posture==InfantryPosture::Prone?
      (walking && identity.type->infantry->crawls?"CRAWL":"PRONE"):(walking?"WALK":"READY"));
  }
  void crushInfantry() {
    for(const auto& [id,entity]:entities) {
      const auto& identity=registry.get<Identity>(entity);if(!identity.type->crusher)continue;
      const auto& tank=registry.get<Transform>(entity);if(tank.current==tank.previous)continue;
      for(const auto& [victim,other]:entities) if(canCrush(id,victim)) {
        const auto point=registry.get<Transform>(other).current;
        const Position delta{tank.current.x-tank.previous.x,tank.current.y-tank.previous.y};
        const auto length=distanceSquared(tank.current,tank.previous);
        const auto t=std::clamp<std::int64_t>(((point.x-tank.previous.x)*delta.x+(point.y-tank.previous.y)*delta.y)*1024/length,0,1024);
        if(distanceSquared(point,{tank.previous.x+delta.x*t/1024,tank.previous.y+delta.y*t/1024})<square(380)) {
          registry.get<Health>(other).hp=0;registry.get<Infantry>(other).death=2;
        }
      }
    }
  }
  motion::Vector muzzle(const Transform& transform, const UnitTypeDef& type, int facing,int occupant=0,int weaponSlot=-1,bool alternate=false,int altitude=0) const {
    if(type.garrisonable && !type.garrisonMuzzles.empty()) {
      const auto pixel=type.garrisonMuzzles[occupant%type.garrisonMuzzles.size()];
      // MuzzleFlash offsets use native 60x30 sprite pixels from the building
      // draw center (half a tile above its starting-cell center).
      const auto y=pixel[1]*1000LL/30-500+type.garrisonMuzzleHeight*1000LL/256;
      const Position point{transform.current.x+pixel[0]*1000LL/60+y,transform.current.y-pixel[0]*1000LL/60+y};
      return fixed(point,grid.heightAt(transform.current.x,transform.current.y)+type.garrisonMuzzleHeight);
    }
    auto flh=weaponSlot>=0 && weaponSlot<static_cast<int>(type.weaponFireFlh.size())?type.weaponFireFlh[weaponSlot]:type.primaryFireFlh;
    if(alternate)flh[1]=-flh[1];
    const auto& surface=grid.cell(cellAt(transform.current)).surface;
    return fixed(transform.current,grid.heightAt(transform.current.x,transform.current.y)+altitude)+
      motion::surfaceFlh(transform.heading,facing,flh,type.isTilter?surface.slopeX:0,type.isTilter?surface.slopeY:0);
  }
  void effect(motion::Vector p, bool explosion, std::string animation, std::string source={}, int facing=0) {
    if (animation.empty()) return;
    EffectSnapshot view{};
    view.position=planar(p); view.started=now; view.explosion=explosion; view.sourceType=std::move(source); view.facing=facing;
    view.heightLeptons=static_cast<int>(p.z/motion::lepton);
    view.animation=std::move(animation); view.animationSpeed=clock.speed();
    const auto& art=definitions.effectAnimation(view.animation);
    const auto delay=art.normalized?compat::normalizedAnimationDelay(art.frameDelay,view.animationSpeed):art.frameDelay;
    const auto end=art.end>=0?art.end+1:(art.frameCount>0?art.frameCount:128);
    view.duration=static_cast<Tick>(std::max(1,end-art.start+art.extraFrames()))*std::max(1,delay);
    effects.push_back(std::move(view));
  }
  std::uint32_t scatterRandom() {
    scatterState^=scatterState<<13;scatterState^=scatterState>>7;scatterState^=scatterState<<17;
    return static_cast<std::uint32_t>(scatterState>>16);
  }
  bool recognizes(entt::entity target,std::uint32_t player) const {
    const auto* disguise=registry.try_get<Camouflage>(target);
    const auto& identity=registry.get<Identity>(target);
    if(identity.owner==player)return true;
    if(submerged(target)) {
      const auto p=registry.get<Transform>(target).current;
      for(const auto& [id,observer]:entities) {
        (void)id;const auto& unit=registry.get<Identity>(observer);
        if(unit.owner==player && unit.type->sensorsRange>0 && registry.get<Health>(observer).hp>0 && !limbo(observer) && !registry.all_of<Contained>(observer) && complete(observer) &&
            distanceSquared(p,registry.get<Transform>(observer).current)<=square(unit.type->sensorsRange))return true;
      }
      return false;
    }
    const bool spy=registry.all_of<SpyDisguise>(target);
    if(!spy && (!disguise || !disguise->active || now<disguise->exposedUntil))return true;
    const auto p=registry.get<Transform>(target).current;
    for(const auto& [id,observer]:entities) {
      (void)id;const auto& unit=registry.get<Identity>(observer);
      if(unit.owner==player && unit.type->detectDisguise && registry.get<Health>(observer).hp>0 &&
          !limbo(observer) && !registry.all_of<Contained>(observer) && complete(observer) &&
          distanceSquared(p,registry.get<Transform>(observer).current)<=square(unit.type->detectDisguiseRange))return true;
    }
    return false;
  }
  void disguiseStep() {
    for(const auto& [id,entity]:entities)if(auto* disguise=registry.try_get<Camouflage>(entity)) {
      (void)id;const auto& pose=registry.get<Transform>(entity);const auto* mobile=registry.try_get<Mobile>(entity);
      const bool active=complete(entity) && !registry.all_of<FactoryExit>(entity) &&
        (!mobile || (!mobile->requested && !mobile->reserved && mobile->path.empty())) && pose.current==pose.previous;
      if(active && !disguise->active)disguise->started=now;
      disguise->active=active;
      if(!disguise->active)continue;
      const auto duration=std::clamp(definitions.rules().integer("General","InfantryBlinkDisguiseTime",20),0,900);
      if(!duration)continue;
      const auto owner=registry.get<Identity>(entity).owner;
      for(const auto& [otherId,other]:entities) {
        (void)otherId;
        if(registry.all_of<Infantry>(other) && !registry.any_of<Contained,Airborne>(other) && registry.get<Health>(other).hp>0 &&
            registry.get<Identity>(other).owner!=owner && distanceSquared(pose.current,registry.get<Transform>(other).current)<=square(1000))
          disguise->exposedUntil=now+duration;
      }
    }
  }
  void radiationStep() {
    const auto& rules=definitions.radiation();radiationField.expire(now,rules);
    if(radiationField.sites().empty() || now%rules.applicationDelay)return;
    const auto& warhead=definitions.warhead(rules.warhead);
    for(const auto& [id,entity]:entities) {
      (void)id;const auto& type=*registry.get<Identity>(entity).type;
      if(limbo(entity) || submerged(entity) || frozen(entity) || type.immuneToRadiation || type.category==TypeCategory::Building || airTarget(entity) || registry.all_of<Contained>(entity) || registry.get<Health>(entity).hp<=0)continue;
      const auto level=radiationField.level(registry.get<Transform>(entity).cell,now,rules);
      const auto damage=std::int64_t(level)*rules.damageFactorMillionths*warhead.versesPercent[type.armor]/100000000;
      auto& hp=registry.get<Health>(entity).hp;hp=static_cast<int>(std::max<std::int64_t>(0,hp-damage));
      if(damage>0)if(auto* foot=registry.try_get<Infantry>(entity))foot->death=warhead.infantryDeath;
    }
  }
  void deployedRadiation(entt::entity entity,Infantry& foot,Armament& arm) {
    const auto& identity=registry.get<Identity>(entity);const auto& weapon=definitions.weapon(identity.type->secondary);
    if(!weapon.areaFire || !weapon.radiationLevel)return;
    const auto& pose=registry.get<Transform>(entity);const auto& warhead=definitions.warhead(weapon.warhead);
    if(!foot.firing && radiationField.sourceLevel(pose.cell,warhead.spreadMilliCells,now,definitions.radiation())>=weapon.radiationLevel/3)return;
    if(foot.firing && foot.fired)return;
    if(!foot.firing) {
      if(now+identity.type->infantry->fireUp<arm.nextShot)return;
      foot.firing=true;foot.fired=false;footAnimation(foot,"DEPLOYEDFIRE",false);foot.animationStarted=now;
      foot.fireAt=now+identity.type->infantry->fireUp;
      foot.attackUntil=std::max(now+footDuration(identity,"DEPLOYEDFIRE"),foot.fireAt+1);
    }
    if(now<foot.fireAt)return;
    radiationField.refresh(pose.cell,warhead.spreadMilliCells,weapon.radiationLevel,now,definitions.radiation());
    arm.weapon=weapon.id;arm.firedAt=now;arm.nextShot=now+weapon.rof;foot.fired=true;
  }
  entt::entity createNavalChild(entt::entity parent,const UnitTypeDef& type,int slot,Position target,EntityId targetId,bool rocket) {
    const auto& owner=registry.get<Identity>(parent);const auto& origin=registry.get<Transform>(parent);
    const auto e=registry.create();const auto id=nextId++;
    registry.emplace<Identity>(e,id,owner.owner,&type);registry.emplace<Health>(e,type.strength);
    auto& pose=registry.emplace<Transform>(e);pose=origin;
    auto flh=owner.type->primaryFireFlh;
    if(owner.type->id=="DRED")flh[1]=(slot?1:-1)*std::abs(flh[1]);
    const auto offset=motion::rotateFlh(origin.heading,flh);
    const auto start=fixed(origin.current,height(parent))+offset;
    pose.previous=pose.current=planar(start);pose.cell=cellAt(pose.current);pose.animationSpeed=clock.speed();
    if(rocket) {
      auto& child=registry.emplace<SpawnedMissile>(e);child.parent=owner.id;child.target=targetId;
      child.flight.position=child.flight.previous=start;child.flight.target=fixed(target,grid.heightAt(target.x,target.y));
      child.flight.heading=origin.heading;child.flight.pitch=child.flight.previousPitch=definitions.missileFor(type.id).initialPitch;child.flight.started=now;
    } else {
      auto& child=registry.emplace<NavalAircraft>(e);child.parent=owner.id;child.target=targetId;child.slot=slot;child.launched=now;child.ground=target;child.groundFire=!targetId;
      child.flight.position=child.flight.previous=start;child.flight.heading=origin.heading;
    }
    entities.emplace(id,e);return e;
  }
  void launchNavalSpawn(entt::entity parent,Armament& arm,const WeaponDef& weapon) {
    auto* spawns=registry.try_get<NavalSpawner>(parent);if(!spawns || now<arm.nextShot)return;
    spawns->target=arm.target;arm.weapon=weapon.id;arm.firedAt=now;arm.nextShot=now+weapon.rof;
  }
  void navalAircraftStep() {
    // Stable slots are owned by the ship, including cooldowns with no live actor.
    for(const auto& [id,parent]:entities)if(auto* manager=registry.try_get<NavalSpawner>(parent)) {
      const auto& identity=registry.get<Identity>(parent);const auto& config=*identity.type->navalSpawn;
      auto* arm=registry.try_get<Armament>(parent);
      const bool active=registry.get<Health>(parent).hp>0 && !frozen(parent) && !grappled(parent) && arm && arm->active && (arm->target?entities.contains(arm->target):identity.type->id=="CARRIER" || identity.type->id=="DRED");
      manager->target=active?arm->target:0;manager->groundFire=active && !arm->target;if(active)manager->ground=arm->ground;
      for(std::size_t slot=0;slot<manager->slots.size();++slot) {
        auto& node=manager->slots[slot];
        if(node.unit && !entities.contains(node.unit)){node.unit=0;node.phase=NavalSpawnPhase::Regenerating;node.ready=now+config.regenerateFrames;}
        if(node.unit || now<node.ready)continue;
        node.phase=NavalSpawnPhase::Docked;
        if(!active || now<manager->launchNext || movingHost(parent))continue;
        const auto victim=manager->target?entities.at(manager->target):entt::null;const auto* weapon=manager->target?targetWeapon(parent,victim):activeWeapon(parent,false);
        if(!weapon || !weapon->spawner || (manager->target && (airTarget(victim) || !recognizes(victim,identity.owner) || registry.get<Health>(victim).hp<=0)))continue;
        const auto point=manager->target?registry.get<Transform>(victim).current:manager->ground;
        const auto distance=distanceSquared(registry.get<Transform>(parent).current,point);
        if(distance>square(weapon->rangeMilliCells) || distance<square(weapon->minimumRangeMilliCells))continue;
        const auto child=createNavalChild(parent,definitions.unit(config.type),int(slot),point,manager->target,config.missile);
        node.unit=registry.get<Identity>(child).id;node.phase=NavalSpawnPhase::Attacking;manager->launchNext=now+10;
      }
      (void)id;
    }
    for(const auto& [id,entity]:entities)if(auto* plane=registry.try_get<NavalAircraft>(entity);plane && registry.get<Health>(entity).hp>0) {
      const auto parent=entities.find(plane->parent);
      if(parent==entities.end() || registry.get<Health>(parent->second).hp<=0 || registry.get<Identity>(parent->second).owner!=registry.get<Identity>(entity).owner) {registry.get<Health>(entity).hp=0;continue;}
      auto& manager=registry.get<NavalSpawner>(parent->second);auto& node=manager.slots.at(plane->slot);
      const auto& type=*registry.get<Identity>(entity).type;const auto& config=*registry.get<Identity>(parent->second).type->navalSpawn;
      if(plane->phase==NavalSpawnPhase::Attacking) {
        const auto target=entities.find(manager.target);
        if(!manager.groundFire && (target==entities.end() || registry.get<Health>(target->second).hp<=0 || limbo(target->second) || airTarget(target->second) || !recognizes(target->second,registry.get<Identity>(entity).owner)))plane->phase=NavalSpawnPhase::Returning;
        else {
          plane->target=manager.target;plane->groundFire=manager.groundFire;plane->ground=manager.ground;const auto point=manager.groundFire?manager.ground:registry.get<Transform>(target->second).current;
          const auto& weapon=definitions.weapon(type.primary);
          if(!definitions.targetDomainAllowed(type,false,waterAt(cellAt(point)),!manager.groundFire && submerged(target->second)))plane->phase=NavalSpawnPhase::Returning;
          else {
            const auto advanced=naval_aircraft::advance(plane->flight,fixed(point),type.speed,type.rot,type.flightLevel,192);
            if(advanced && now-plane->launched>15) {
              // One native Ammo per sortie. The aircraft never deals launcher Damage=1.
              Shot shot{};shot.weapon=&weapon;shot.source=id;shot.owner=registry.get<Identity>(entity).owner;shot.targetEntity=plane->target;
              shot.location=plane->flight.position;shot.view.id=nextShot++;shot.view.started=now;shot.view.start=shot.view.previous=shot.view.position=planar(shot.location);
              shot.view.heightLeptons=shot.view.previousHeightLeptons=int(shot.location.z/motion::lepton);shot.view.target=point;shot.view.targetHeightLeptons=manager.groundFire?grid.heightAt(point.x,point.y):height(target->second);
              shot.view.weaponId=weapon.id;shot.view.sourceType=type.id;const auto& projectile=definitions.projectile(weapon.projectile);shot.view.image=projectile.image;
              const auto delta=fixed(point,shot.view.targetHeightLeptons)-shot.location;const auto length=std::max<std::int64_t>(1,motion::length(delta));
              if(projectile.homing){const auto speed=weapon.speed*motion::lepton;shot.velocity={delta.x*speed/length,delta.y*speed/length,delta.z*speed/length};}
              else {const auto frames=std::max<std::int64_t>(1,motion::sqrt(std::max<std::int64_t>(1,2*(shot.location.z/motion::lepton-shot.view.targetHeightLeptons)/std::max(1,definitions.profile().gravity))));shot.velocity={delta.x/frames,delta.y/frames,0};}
              projectiles.push_back(std::move(shot));plane->phase=NavalSpawnPhase::Returning;
            }
          }
        }
      }
      if(plane->phase==NavalSpawnPhase::Returning) {
        const auto& dock=registry.get<Transform>(parent->second);const auto distance=distanceSquared(planar(plane->flight.position),dock.current);
        const int altitude=distance<square(800)?height(parent->second):type.flightLevel;
        const bool near=naval_aircraft::advance(plane->flight,fixed(dock.current),type.speed,type.rot,altitude,64);
        if(near && plane->flight.position.z<=std::int64_t(height(parent->second)+20)*motion::lepton) {
          plane->recovered=true;registry.emplace_or_replace<Erased>(entity);registry.get<Health>(entity).hp=0;
          node.unit=0;node.phase=NavalSpawnPhase::Reloading;node.ready=now+config.reloadFrames;
        }
      }
      auto& pose=registry.get<Transform>(entity);pose.previous=planar(plane->flight.previous);pose.current=planar(plane->flight.position);pose.cell=cellAt(pose.current);pose.heading=pose.turret=plane->flight.heading;
      if(node.unit)node.phase=plane->phase;
    }
  }
  void sonicStep() {
    if(sonicWaves.empty())return;
    const auto& weapon=definitions.weapon("SONICZAP");
    const int width=std::clamp(definitions.replica().integer("NavalCombat","SonicWidthLeptons",128),1,512)*1000/256;
    std::erase_if(sonicWaves,[&](auto& wave) {
      const auto delta=Position{wave.end.x-wave.start.x,wave.end.y-wave.start.y};const auto length=std::max<std::int64_t>(1,integerSqrt(square(delta.x)+square(delta.y)));
      const auto previous=wave.travelled;wave.travelled+=weapon.speed*1000/256;
      const Position a{wave.start.x+delta.x*previous/length,wave.start.y+delta.y*previous/length},b{wave.start.x+delta.x*std::min(wave.travelled,length)/length,wave.start.y+delta.y*std::min(wave.travelled,length)/length};
      if(!waterAt(cellAt(b)))return true;
      for(const auto& [id,target]:entities) {
        const auto& identity=registry.get<Identity>(target);if(id==wave.source || (identity.type->typeImmune && identity.type->id==wave.sourceType) || airTarget(target) || limbo(target) || registry.all_of<Contained>(target) || registry.get<Health>(target).hp<=0)continue;
        const auto p=registry.get<Transform>(target).current;
        if(!waterAt(cellAt(p)))continue;
        const auto step=Position{b.x-a.x,b.y-a.y};const auto squareLength=std::max<std::int64_t>(1,square(step.x)+square(step.y));
        const auto t=std::clamp<std::int64_t>(((p.x-a.x)*step.x+(p.y-a.y)*step.y)*1024/squareLength,0,1024);
        if(distanceSquared(p,{a.x+step.x*t/1024,a.y+step.y*t/1024})>square(width))continue;
        Shot hit{};hit.weapon=&definitions.weapon("@SONICAMBIENT");hit.source=wave.source;hit.owner=wave.owner;hit.targetEntity=id;hit.location=fixed(p,height(target));hit.view.target=p;hit.view.sourceType=wave.sourceType;
        impact(hit);
        if(id==wave.target && !wave.directDelivered){hit.weapon=&weapon;impact(hit);wave.directDelivered=true;}
      }
      return wave.travelled>=length;
    });
  }

  void launchMissile(entt::entity parent,Armament& arm,const WeaponDef& weapon,Position target,int facing) {
    auto* launcher=registry.try_get<MissileLauncher>(parent);if(!launcher || launcher->child || now<launcher->ready || now<arm.nextShot)return;
    const auto& source=registry.get<Identity>(parent);const auto& type=definitions.unit(source.type->missileLauncher->type);
    const auto entity=registry.create();const auto id=nextId++;const auto start=muzzle(registry.get<Transform>(parent),*source.type,facing);
    registry.emplace<Identity>(entity,id,source.owner,&type);registry.emplace<Health>(entity,type.strength);
    auto& pose=registry.emplace<Transform>(entity);pose.previous=pose.current=planar(start);pose.cell=cellAt(pose.current);pose.heading=pose.previousHeading=pose.turret=pose.previousTurret=facing;
    pose.animationSpeed=clock.speed();auto& rocket=registry.emplace<SpawnedMissile>(entity);rocket.parent=source.id;rocket.target=arm.target;
    rocket.flight.position=rocket.flight.previous=start;rocket.flight.target=fixed(target,grid.heightAt(target.x,target.y));rocket.flight.heading=facing;
    rocket.flight.pitch=rocket.flight.previousPitch=definitions.missile().initialPitch;rocket.flight.started=now;
    entities.emplace(id,entity);launcher->child=id;
    arm.weapon=weapon.id;arm.firedAt=now;arm.nextShot=now+weapon.rof;
  }
  void missileStep() {
    for(const auto& [id,entity]:entities)if(auto* rocket=registry.try_get<SpawnedMissile>(entity);rocket && registry.get<Health>(entity).hp>0) {
      (void)id;const auto& identity=registry.get<Identity>(entity);const auto parent=entities.find(rocket->parent);
      if(!rocket->flight.launched) {
        const auto* arm=parent==entities.end()?nullptr:registry.try_get<Armament>(parent->second);
        const bool valid=parent!=entities.end() && registry.get<Health>(parent->second).hp>0 && !movingHost(parent->second) && arm && arm->active && arm->target==rocket->target &&
          (!rocket->target || (entities.contains(rocket->target) && registry.get<Health>(entities.at(rocket->target)).hp>0));
        if(!valid){rocket->cancelled=true;registry.get<Health>(entity).hp=0;continue;}
      }
      const auto advanced=missile::advance(rocket->flight,definitions.missileFor(identity.type->id),identity.type->speed,now);
      if(identity.type->id=="DMISL" && rocket->flight.launched) {
        const auto delta=rocket->flight.target-rocket->flight.position;
        const auto desired=motion::direction(delta.x,delta.y);
        rocket->flight.heading=(rocket->flight.heading+std::clamp(motion::angleDelta(rocket->flight.heading,desired),-std::max(1,identity.type->rot)*256,std::max(1,identity.type->rot)*256)+65536)%65536;
      }
      auto& pose=registry.get<Transform>(entity);pose.previous=planar(rocket->flight.previous);pose.current=planar(rocket->flight.position);pose.cell=cellAt(pose.current);pose.heading=pose.turret=rocket->flight.heading;
      const auto pitch=motion::unitVector(rocket->flight.pitch),heading=motion::unitVector(rocket->flight.heading);
      const auto halfLength=std::int64_t(definitions.missileFor(identity.type->id).bodyLength)*motion::lepton/2;
      auto exhaust=rocket->flight.position-motion::Vector{halfLength*pitch.x/65536*heading.x/65536,halfLength*pitch.x/65536*heading.y/65536,halfLength*pitch.y/65536};
      const auto exhaustCell=planar(exhaust);exhaust.z=std::max(exhaust.z,std::int64_t(grid.heightAt(exhaustCell.x,exhaustCell.y))*motion::lepton);
      if(advanced.takeoff) {
        if(identity.type->id=="DMISL" && parent!=entities.end())if(auto* manager=registry.try_get<NavalSpawner>(parent->second))for(auto& slot:manager->slots)if(slot.unit==registry.get<Identity>(entity).id) {
          slot.unit=0;slot.phase=NavalSpawnPhase::Regenerating;slot.ready=now+registry.get<Identity>(parent->second).type->navalSpawn->regenerateFrames;
        }
        if(identity.type->id!="DMISL")effect(exhaust,false,"V3TAKOFF","V3",pose.heading);
        if(parent!=entities.end())if(auto* launcher=registry.try_get<MissileLauncher>(parent->second))launcher->ready=now+registry.get<Identity>(parent->second).type->missileLauncher->regenerateFrames;
      }
      if(rocket->flight.launched && now%4==0)effect(exhaust,false,identity.type->id=="DMISL"?"DURASMOKE":"V3TRAIL",identity.type->id,pose.heading);
      if(advanced.impact) {
        Shot shot{};shot.weapon=&definitions.weapon(definitions.missileFor(identity.type->id).impactWeapon);shot.owner=identity.owner;shot.source=rocket->parent;
        shot.view.target=planar(rocket->flight.position);shot.location=rocket->flight.position;shot.view.sourceType=identity.type->id=="DMISL"?"DRED":"V3";
        impact(shot);rocket->detonated=true;registry.get<Health>(entity).hp=0;
      }
    }
  }
  void startLeap(entt::entity actor,Armament& arm,const WeaponDef& weapon) {
    if(!arm.target || !entities.contains(arm.target) || !leapTarget(actor,entities.at(arm.target)))return;
    const auto victim=arm.target;auto& pose=registry.get<Transform>(actor);const auto& type=*registry.get<Identity>(actor).type;
    const auto target=entities.at(victim);const auto& other=registry.get<Transform>(target);
    stop(actor);release(registry.get<Identity>(actor).id);
    // LimboLaunch replaces the shooter with the jumping projectile. It must
    // leave neither a tank reservation nor a foot subcell at its old position.
    registry.remove<Mobile>(actor);
    if(auto* foot=registry.try_get<Infantry>(actor))*foot=Infantry{};
    auto& state=registry.get<leap::State>(actor);state.victim=victim;
    leap::launch(state,fixed(pose.current,grid.heightAt(pose.current.x,pose.current.y)+type.primaryFireFlh[2]),
      fixed(other.current,height(target)),weapon.speed,now);
    if(type.leap->naval) {
      state.phase=leap::Phase::Infesting;state.position=state.previous=fixed(other.current,height(target));
      state.nextDamage=now+std::clamp(definitions.replica().integer("NavalCombat","SquidDamageFrames",11),1,900);
      stop(target);release(victim);const auto hostCell=registry.get<Transform>(target).cell;
      occupied[key(hostCell)]=victim;refresh(hostCell);registry.emplace_or_replace<Mobile>(target,hostCell);
      registry.emplace_or_replace<Infested>(target,registry.get<Identity>(actor).id);
      if(auto* sub=registry.try_get<Submerge>(target)){sub->stage=0;sub->surfacedUntil=now+18;}
    }
    arm.weapon=weapon.id;arm.firedAt=now;arm.nextShot=now+weapon.rof;arm.chargeUntil=0;
  }
  bool landLeap(entt::entity actor,leap::State& state) {
    if(const auto host=entities.find(state.victim);host!=entities.end())if(const auto* link=registry.try_get<Infested>(host->second);link && link->parasite==registry.get<Identity>(actor).id)registry.remove<Infested>(host->second);
    const auto& identity=registry.get<Identity>(actor);auto& pose=registry.get<Transform>(actor);
    const auto center=cellAt(planar(state.position));
    for(int radius=0;radius<=8;++radius)for(int y=-radius;y<=radius;++y)for(int x=-radius;x<=radius;++x) {
      if(std::max(std::abs(x),std::abs(y))!=radius)continue;
      const TileCoord cell{center.x+x,center.y+y};
      if(travelSpeed(*identity.type,cell,cell)<=0)continue;
      const auto slot=identity.type->infantry?availableFootSlot(cell,identity.id,identity.owner):-1;
      if(identity.type->infantry?slot<0:!free(cell,identity.id))continue;
      pose.cell=cell;pose.current=identity.type->infantry?footPosition(cell,slot):position(cell);pose.previous=pose.current;
      if(identity.type->infantry) {
        auto& foot=registry.get<Infantry>(actor);foot=Infantry{};foot.subCell=slot;foot.destination=cell;foot.animationStarted=now;
        footOccupied[key(cell)][slot]=identity.id;
      } else {registry.emplace_or_replace<Mobile>(actor,cell);occupied[key(cell)]=identity.id;}
      if(identity.type->leap->naval)if(auto* arm=registry.try_get<Armament>(actor))arm->nextShot=std::max(arm->nextShot,state.suppressedUntil);
      if(identity.type->leap->naval)if(auto* sub=registry.try_get<Submerge>(actor)){sub->stage=0;sub->surfacedUntil=now+18;}
      state.phase=leap::Phase::Ground;state.victim=0;state.position=state.previous=fixed(pose.current,grid.heightAt(pose.current.x,pose.current.y));
      refresh(cell);return true;
    }
    return false;
  }
  void leapStep() {
    for(const auto& [id,actor]:entities) {
      auto* state=registry.try_get<leap::State>(actor);
      if(!state || state->phase==leap::Phase::Ground || registry.get<Health>(actor).hp<=0)continue;
      const auto& identity=registry.get<Identity>(actor);auto& pose=registry.get<Transform>(actor);
      const auto& weapon=definitions.weapon(identity.type->primary);const auto target=entities.find(state->victim);
      const auto landing=[&] {state->phase=leap::Phase::Landing;state->started=now;};
      if(state->phase==leap::Phase::Flying) {
        // The source no longer participates in ordinary damage/selection while
        // in limbo. A vanished, boarded or newly friendly victim cancels safely.
        if(target==entities.end() || !leapTarget(actor,target->second) || now-state->started>90)landing();
        else if(now>state->started) {
          const auto point=registry.get<Transform>(target->second).current;
          const bool arrived=leap::advance(*state,fixed(point,height(target->second)),weapon.speed,now);
          Shot sweep{};sweep.weapon=&weapon;sweep.source=id;auto after=state->position;
          const bool blocked=clipProjectile(sweep,state->previous,after);state->position=after;
          pose.current=planar(after);pose.cell=cellAt(pose.current);
          if(pose.current!=pose.previous)pose.heading=pose.turret=direction({pose.current.x-pose.previous.x,pose.current.y-pose.previous.y});
          if(blocked)landing();
          else if(arrived) {
            const auto& victim=registry.get<Identity>(target->second);
            if(victim.type->category==TypeCategory::Infantry || victim.type->organic) {
              registry.get<Health>(target->second).hp=0;
              if(auto* foot=registry.try_get<Infantry>(target->second))foot->death=1;
              release(victim.id);landing();
            } else {registry.emplace_or_replace<Infested>(target->second,id);state->phase=leap::Phase::Infesting;state->nextDamage=now+weapon.rof;state->suppressedUntil=0;}
          }
        }
      } else if(state->phase==leap::Phase::Infesting) {
        if(target==entities.end() || registry.get<Health>(target->second).hp<=0 ||
            registry.get<Identity>(target->second).type->category!=TypeCategory::Vehicle) {
          if(target!=entities.end() && registry.get<Health>(target->second).hp<=0)release(state->victim);
          landing();
        } else {
          const auto& host=registry.get<Transform>(target->second);state->previous=state->position;state->position=fixed(host.current,height(target->second));
          pose.current=host.current;pose.cell=host.cell;
          if(!frozen(target->second) && now>=state->nextDamage && now>=state->suppressedUntil) {
            auto& hp=registry.get<Health>(target->second).hp;
            const auto& type=*registry.get<Identity>(target->second).type;
            const auto damage=std::int64_t(weapon.damage)*definitions.warhead(weapon.warhead).versesPercent[type.armor]/100;
            hp=int(std::max<std::int64_t>(0,hp-damage));state->nextDamage=now+(identity.type->leap->naval?std::clamp(definitions.replica().integer("NavalCombat","SquidDamageFrames",11),1,900):weapon.rof);
            if(identity.type->leap->naval) {
              effect(fixed(host.current,height(target->second)),false,"SQDWAKE",identity.type->id,host.heading);
              if(definitions.warhead(weapon.warhead).culling && std::int64_t(hp)*compat::kRatioScale<=std::int64_t(type.strength)*definitions.profile().conditionRedMillionths)hp=0;
            }
            if(hp<=0){release(state->victim);landing();}
          }
        }
      }
      if(state->phase==leap::Phase::Landing && !landLeap(actor,*state) && now-state->started>90) {
        state->removed=true;registry.get<Health>(actor).hp=0;
      }
    }
  }
  void weapons(entt::entity entity) {
    auto* arm = registry.try_get<Armament>(entity);
    if (!arm || limbo(entity) || grappled(entity) || registry.all_of<NavalAircraft>(entity) || frozen(entity) || recovering(entity) || registry.get<Health>(entity).hp<=0 || !complete(entity) || registry.any_of<FactoryExit,Contained>(entity)) return;
    const auto& identity = registry.get<Identity>(entity);
    if((identity.type->leap || identity.type->engineer || identity.type->spy) && registry.all_of<BuildingInteraction>(entity))return;
    if(identity.type->canDisguise || registry.all_of<ShipyardRepair>(entity))return;
    if(!powered(entity)) {arm->chargeStarted=arm->chargeUntil=0;return;}
    if(const auto* launcher=registry.try_get<MissileLauncher>(entity);launcher && launcher->child)if(const auto child=entities.find(launcher->child);child!=entities.end())
      if(!registry.get<SpawnedMissile>(child->second).flight.launched)return;
    auto* foot=registry.try_get<Infantry>(entity);
    if(foot && (foot->postureUntil || registry.get<Health>(entity).hp<=0)) return;
    if(foot && identity.type->deployFire && foot->posture==InfantryPosture::Deployed){deployedRadiation(entity,*foot,*arm);return;}
    auto* air=registry.try_get<Airborne>(entity);
    auto* pilot=registry.try_get<Pilot>(entity);
    if(pilot && (pilot->mission!=AircraftMission::Attacking && !identity.type->aircraft->balloon))return;
    if(pilot && (pilot->ammo==0 && !arm->burstRemaining))return;
    if(pilot && height(entity)<flightSurface(registry.get<Transform>(entity).cell)+identity.type->aircraft->movement.height-20)return;
    if(air && air->takingOff)return;
    auto& transform = registry.get<Transform>(entity);
    auto* mobile=registry.try_get<Mobile>(entity);
    const bool moving=(mobile && (mobile->reserved || mobile->requested || !mobile->path.empty())) ||
      (air && air->requested) || (foot && (foot->requested || foot->targetSubCell>=0 || !foot->path.empty()));
    const auto range=[&](const WeaponDef& w,entt::entity target) {return w.rangeMilliCells+(airTarget(target)?identity.type->airRangeBonus:0);};
    const auto eligible=[&](entt::entity candidate,bool automatic)->const WeaponDef* {
      const auto& other=registry.get<Identity>(candidate);
      if(automatic && other.type->wall)return nullptr;
      if(!definitions.targetDomainAllowed(*identity.type,airTarget(candidate),waterAt(registry.get<Transform>(candidate).cell),submerged(candidate)))return nullptr;
      if(other.type->cloakable && !recognizes(candidate,identity.owner))return nullptr;
      if(limbo(candidate) || registry.get<Health>(candidate).hp<=0 || registry.any_of<Contained,FactoryExit>(candidate))return nullptr;
      if(const auto* rocket=registry.try_get<SpawnedMissile>(candidate);rocket && !rocket->flight.launched)return nullptr;
      const auto* charge=other.owner==identity.owner?definitions.chargeWeapon(*identity.type,*other.type):nullptr;
      const auto* w=charge?charge:targetWeapon(entity,candidate);if(!w)return nullptr;
      if(frozen(candidate) && !w->temporal)return nullptr;
      if(identity.type->c4 && other.type->category==TypeCategory::Building) {
        if(automatic || !other.type->canC4 || other.owner==identity.owner || registry.all_of<C4Charge>(candidate))return nullptr;
        return &definitions.weapon(identity.type->secondary);
      }
      if(frozen(candidate) && !w->temporal)return nullptr;
      if(w->temporal && (!other.type->warpable || airTarget(candidate)))return nullptr;
      if(w->charges)return charge && complete(candidate)?w:nullptr;
      if(w->plantsBomb) {
        if(other.id==identity.id || !other.type->bombable || airTarget(candidate) || registry.all_of<AttachedBomb>(candidate) || (automatic && other.owner==identity.owner))return nullptr;
        return w;
      }
      if(w->disarmsBomb)return !airTarget(candidate) && registry.all_of<AttachedBomb>(candidate)?w:nullptr;
      if(w->leap && !leapTarget(entity,candidate))return nullptr;
      if(other.type->wall && !definitions.warhead(w->warhead).damagesWalls)return nullptr;
      if(w->repairs?(other.id==identity.id || other.owner!=identity.owner || other.type->category!=TypeCategory::Vehicle || (registry.get<Health>(candidate).hp>=other.type->strength && !infestor(other.id))):(other.owner==identity.owner && !(w->sonic && grappled(candidate) && !automatic)))return nullptr;
      if(w->minimumRangeMilliCells>0 && distanceSquared(transform.current,registry.get<Transform>(candidate).current)<square(w->minimumRangeMilliCells))return nullptr;
      if(definitions.warhead(w->warhead).versesPercent[other.type->armor]<(automatic?2:1))return nullptr;
      if(definitions.warhead(w->warhead).radiation && other.type->immuneToRadiation)return nullptr;
      if(automatic && ((!complete(candidate) && !(w->temporal && frozen(candidate))) || (other.type->garrisonable && (!registry.all_of<Cargo>(candidate) || registry.get<Cargo>(candidate).passengers.empty()))))return nullptr;
      return w;
    };
    const auto scanRange=[&](const WeaponDef& w,entt::entity target){return identity.type->leap?std::max(range(w,target),identity.type->leap->scanRange):range(w,target);};
    if(arm->active && arm->target) {
      const auto it=entities.find(arm->target);const auto* w=it==entities.end()?nullptr:eligible(it->second,arm->automatic);
      if(!w || (arm->automatic && distanceSquared(transform.current,registry.get<Transform>(it->second).current)>square(scanRange(*w,it->second)))) {
        arm->active=false;arm->target=0;arm->burstRemaining=0;
        if(foot){foot->firing=false;foot->fired=false;}
      }
    }
    if(!arm->active && identity.type->canPassiveAcquire && (!moving || identity.type->opportunityFire)) {
      std::int64_t nearest=std::numeric_limits<std::int64_t>::max();
      for(const auto& [id,candidate]:entities) {
        const auto& other=registry.get<Identity>(candidate);
        if(other.owner==identity.owner && definitions.chargeWeapon(*identity.type,*other.type))continue;
        if(!recognizes(candidate,identity.owner))continue;
        const auto* w=eligible(candidate,true);if(!w)continue;
        const auto distance=distanceSquared(transform.current,registry.get<Transform>(candidate).current);
        if(distance<=square(scanRange(*w,candidate)) && distance<nearest){nearest=distance;arm->target=id;}
      }
      // An idle trooper may support a nearby friendly coil. Enemy acquisition
      // takes priority; this never moves the trooper without a player order.
      if(nearest==std::numeric_limits<std::int64_t>::max() && !moving && identity.type->infantry && !arm->autoChargeSuppressed &&
          !registry.all_of<BuildingInteraction>(entity))for(const auto& [id,candidate]:entities) {
        const auto& other=registry.get<Identity>(candidate);
        if(other.owner!=identity.owner)continue;
        const auto* w=definitions.chargeWeapon(*identity.type,*other.type);if(!w || !eligible(candidate,true))continue;
        const auto distance=distanceSquared(transform.current,registry.get<Transform>(candidate).current);
        if(distance<=square(w->rangeMilliCells) && distance<nearest){nearest=distance;arm->target=id;}
      }
      if(nearest!=std::numeric_limits<std::int64_t>::max()){arm->active=true;arm->automatic=true;}
    }
    if(!arm->active) {
      arm->chargeStarted=arm->chargeUntil=0;
      if(moving && !air)transform.turret=transform.turretTurn.update(transform.turret,transform.heading,identity.type->rot,now);
      return;
    }
    const auto* chosen=arm->target?targetWeapon(entity,entities.at(arm->target)):activeWeapon(entity,false);
    if(!chosen){arm->active=false;arm->burstRemaining=0;return;}
    const auto& weapon=*chosen;
    const bool omniFire=weapon.omniFire || (pilot && identity.type->aircraft->fixedWing);
    if(foot && foot->firing && (arm->weapon!=weapon.id || arm->windupTarget!=arm->target)){foot->firing=false;foot->fired=false;}
    Position target = arm->ground;
    Position targetVelocity{};
    if (arm->target) {
      const auto it = entities.find(arm->target);
      if (it == entities.end()) { arm->active = false; return; }
      if (!weapon.repairs && !weapon.charges && !weapon.plantsBomb && !weapon.disarmsBomb && !(weapon.sonic && grappled(it->second)) && registry.get<Identity>(it->second).owner == identity.owner) { arm->active = false; return; }
      const auto& other=registry.get<Transform>(it->second);
      target = other.current;
      if(identity.type->c4 && registry.get<Identity>(it->second).type->category==TypeCategory::Building) {
        auto nearest=std::numeric_limits<std::int64_t>::max();
        for(const auto cell:footprint(*registry.get<Identity>(it->second).type,other.cell)) {
          const auto p=position(cell);const auto d=distanceSquared(transform.current,p);if(d<nearest){nearest=d;target=p;}
        }
      }
      targetVelocity={other.current.x-other.previous.x,other.current.y-other.previous.y};
    }
    const int targetRange=arm->target?range(weapon,entities.at(arm->target)):weapon.rangeMilliCells;
    const auto targetDistance=weapon.cellRangefinding && arm->target?distanceSquared(position(transform.cell),position(cellAt(target))):distanceSquared(transform.current,target);
    const bool inRange=targetDistance<=square(targetRange) && targetDistance>=square(weapon.minimumRangeMilliCells);
    if (!inRange && (!arm->automatic || weapon.leap) && mobile &&
        ((!mobile->requested && mobile->path.empty()) || (now>=mobile->retry && distanceSquared(position(mobile->destination),target)>square(2000)))) {
      move(entity,cellAt(target)); mobile->retry=now+12;
    }
    if (inRange && (!arm->automatic || weapon.leap) && mobile) {
      mobile->requested = false;
      if (mobile->reserved) mobile->path = {*mobile->reserved}; else mobile->path.clear();
    }
    if(air && !arm->automatic) {
      if(inRange) {air->requested=false;air->path.clear();air->speed=0;releaseAir(identity.id,false);air->destinationSlot=-1;}
      else if(!air->requested || (now>=air->retry && distanceSquared(position(air->destination),target)>square(2000))) {move(entity,cellAt(target));air->retry=now+12;}
    }
    if(foot && !air && (!arm->automatic || weapon.leap)) {
      if(inRange) haltFoot(entity,false);
      else if(foot->posture!=InfantryPosture::Deployed &&
          ((!foot->requested && foot->path.empty()) || (now>=foot->retry && distanceSquared(position(foot->destination),target)>square(2000)))) {
        move(entity,cellAt(target));foot->retry=now+12;
      }
    }
    if(weapon.temporal)if(const auto* channel=registry.try_get<TemporalChannel>(entity);channel && channel->target==arm->target) {
      if(!inRange)registry.remove<TemporalChannel>(entity);
      return;
    }
    const auto& projectile=definitions.projectile(weapon.projectile);
    const int targetHeight=arm->target?height(entities.at(arm->target)):grid.heightAt(target.x,target.y);
    const auto targetPoint=[&](Position p) { return fixed(p,targetHeight); };
    Position aim=target;
    if (inRange && !projectile.invisible && !projectile.homing && (targetVelocity.x || targetVelocity.y)) {
      // A launch-time intercept, never a homing shell. Recompute with the real
      // muzzle twice; turns and terrain changes after launch can still evade it.
      for (int iteration=0;iteration<2;++iteration) {
        const auto bearing=direction({aim.x-transform.current.x,aim.y-transform.current.y});
        const auto lead=motion::ballisticLaunch(targetPoint(aim)-muzzle(transform,*identity.type,bearing),
                                               weapon.rangeMilliCells*256/1000,definitions.profile().gravity);
        if (!lead) { aim=target; break; }
        aim={target.x+targetVelocity.x*lead->flight256/256,target.y+targetVelocity.y*lead->flight256/256};
      }
    }
    const auto desired = direction({aim.x - transform.current.x, aim.y - transform.current.y});
    auto& facing = identity.type->turret ? transform.turret : transform.heading;
    auto& rotation = identity.type->turret ? transform.turretTurn : transform.bodyTurn;
    if(identity.type->category==TypeCategory::Building && !identity.type->turret)facing=desired;
    else if (!omniFire && (identity.type->turret || !moving)) facing = rotation.update(facing,desired,air?identity.type->flight->turnRate:identity.type->rot,now);
    if (!identity.type->turret) transform.turret = facing;
    const int fireDelay=identity.type->spriteVehicle?identity.type->spriteVehicle->sequence("FIREUP").count:identity.type->fireDelay;
    if (!inRange || (!omniFire && facing != desired) || (!foot && (arm->burstRemaining?now<arm->burstNext:now+fireDelay<arm->nextShot)) ||
        (moving && !pilot && (!identity.type->turret || !identity.type->opportunityFire))) return;
    if(weapon.leap && !arm->target){arm->active=false;return;}
    if((weapon.plantsBomb || weapon.disarmsBomb) && !arm->target){arm->active=false;return;}
    if(weapon.spawner){if(identity.type->navalSpawn)launchNavalSpawn(entity,*arm,weapon);else launchMissile(entity,*arm,weapon,aim,facing);return;}
    if(fireDelay>0) {
      if(!arm->chargeUntil || arm->windupTarget!=arm->target || arm->weapon!=weapon.id) {
        arm->chargeStarted=now;arm->chargeUntil=now+fireDelay;
        arm->windupTarget=arm->target;arm->weapon=weapon.id;
      }
      if(now<arm->chargeUntil)return;
      if(now<arm->nextShot)return;
      arm->chargeUntil=0;
    }
    if(weapon.suicide) {
      // Native Suicide kills the shooter; only its death weapon detonates.
      // No target-centered projectile, muzzle Anim, or infantry firing pose.
      arm->weapon=weapon.id;arm->firedAt=now;
      registry.get<Health>(entity).hp=0;
      return;
    }
    if(foot) {
      if(foot->firing && foot->fired) return;
      if(!foot->firing) {
        const auto& art=*identity.type->infantry;
        const bool prone=foot->posture==InfantryPosture::Prone;
        const auto sequence=swimming(entity)?"WETATTACK":air?"FIREFLY":foot->posture==InfantryPosture::Deployed?"DEPLOYEDFIRE":prone?"FIREPRONE":"FIREUP";
        const auto delay=(prone?art.fireProne:art.fireUp)*art.sequence(sequence).delay;
        // ROF measures shot-to-shot time. Begin the windup before cooldown ends,
        // rather than adding FireUp a second time to every shot interval.
        if(now+delay<arm->nextShot)return;
        foot->firing=true;foot->fired=false;arm->weapon=weapon.id;arm->windupTarget=arm->target;
        footAnimation(*foot,sequence,false);
        foot->animationStarted=now;
        // FireUp may equal Count (CONS=6): keep the event on the logical timeline,
        // allowing the final visual frame to be held until the event occurs.
        foot->fireAt=now+(prone?art.fireProne:art.fireUp)*art.sequence(foot->sequence).delay;
        foot->attackUntil=std::max(now+footDuration(identity,foot->sequence),foot->fireAt+1);
      }
      if(now<foot->fireAt) return;
      foot->fired=true;
    }
    if(weapon.sonic) {
      const auto distance=std::max<std::int64_t>(1,integerSqrt(distanceSquared(transform.current,aim)));
      const Position end{transform.current.x+(aim.x-transform.current.x)*weapon.rangeMilliCells/distance,transform.current.y+(aim.y-transform.current.y)*weapon.rangeMilliCells/distance};
      sonicWaves.push_back({transform.current,end,identity.id,arm->target,identity.owner,now,0,identity.type->id});
      beams.push_back({transform.current,end,height(entity),height(entity),now,Tick((weapon.rangeMilliCells*256/1000+weapon.speed-1)/weapon.speed),identity.owner,weapon.id});
      arm->weapon=weapon.id;arm->firedAt=now;arm->nextShot=now+weapon.rof;return;
    }
    if(weapon.temporal) {
      if(!arm->target)return;
      const auto victim=entities.at(arm->target);
      registry.emplace_or_replace<TemporalChannel>(entity,arm->target,weapon.id);
      if(!frozen(victim))registry.emplace<TemporalVictim>(victim,registry.get<Identity>(victim).type->strength*10,now);
      arm->weapon=weapon.id;arm->firedAt=now;return;
    }
    if(weapon.sapper && arm->target && registry.get<Identity>(entities.at(arm->target)).type->naval) {
      const auto victim=entities.at(arm->target);auto& hp=registry.get<Health>(victim).hp;
      hp=std::max(0,hp-weapon.damage);
      const auto& animations=definitions.warhead(weapon.warhead).animations;
      if(!animations.empty())effect(fixed(bombPosition(victim),height(victim)),true,animations.back(),identity.type->id);
      arm->weapon=weapon.id;arm->firedAt=now;arm->nextShot=now+weapon.rof;return;
    }
    if(identity.type->c4 && arm->target && registry.get<Identity>(entities.at(arm->target)).type->category==TypeCategory::Building) {
      const auto delay=std::max(1,int(definitions.rules().number("CombatDamage","C4Delay",.03)*900+.5));
      registry.emplace<C4Charge>(entities.at(arm->target),now+delay);
      arm->weapon=weapon.id;arm->firedAt=now;arm->nextShot=now+weapon.rof;
      arm->active=false;arm->automatic=false;arm->target=0;message="C4 planted";return;
    }
    if(weapon.plantsBomb || weapon.disarmsBomb) {
      const auto victim=entities.at(arm->target);
      if(weapon.plantsBomb) {
        registry.emplace<AttachedBomb>(victim,identity.id,identity.owner,now,now+definitions.ivanBomb().delay,identity.type->id);
        message="Time bomb attached";
      } else {registry.remove<AttachedBomb>(victim);message="Time bomb disarmed";}
      arm->weapon=weapon.id;arm->firedAt=now;arm->nextShot=now+weapon.rof;
      arm->active=false;arm->automatic=false;arm->target=0;arm->burstRemaining=0;
      return;
    }
    if(weapon.leap){startLeap(entity,*arm,weapon);return;}
    const int fireFacing=omniFire?desired:facing;
    if(weapon.decloakToFire)if(auto* sub=registry.try_get<Submerge>(entity)){sub->stage=0;sub->surfacedUntil=now+std::max(1,int(definitions.rules().number("General","CloakDelay",.02)*900));}
    const int count=identity.type->garrisonable?static_cast<int>(registry.get<Cargo>(entity).passengers.size()):1;
    arm->weapon=weapon.id;
    arm->firedAt=now;
    if(auto* disguise=registry.try_get<Camouflage>(entity);disguise && weapon.disguiseBlinkFrames>0)
      disguise->visibleUntil=std::max(disguise->visibleUntil,now+weapon.disguiseBlinkFrames);
    for(int occupant=0;occupant<count;++occupant) {
    const auto start=muzzle(transform,*identity.type,fireFacing,occupant,identity.type->gunner?gunnerMode(entity):-1,arm->burstRemaining>0,(air || pilot)?height(entity)-grid.heightAt(transform.current.x,transform.current.y):0);
    Position endpoint=aim;const bool scatter=projectile.inaccurate && projectile.flakScatter;
    if(scatter) {
      const auto radius=scatterRandom()%(definitions.profile().ballisticScatter*(projectile.invisible?2:1)+1);
      const auto vector=motion::unitVector(scatterRandom()&65535);
      endpoint.x+=vector.x*radius/65536;endpoint.y+=vector.y*radius/65536;
    }
    const int endHeight=weapon.charges?targetHeight+registry.get<Identity>(entities.at(arm->target)).type->buildingHeight:
      arm->target && airTarget(entities.at(arm->target))?targetHeight:grid.heightAt(endpoint.x,endpoint.y);
    const auto finish=fixed(endpoint,endHeight);
    const auto launch=projectile.vertical?std::optional{motion::Launch{{0,0,-weapon.speed*motion::lepton},256}}:(projectile.invisible || projectile.homing) ? std::optional{motion::Launch{finish-start,256}} :
      motion::ballisticLaunch(finish-start,weapon.rangeMilliCells*256/1000,definitions.profile().gravity);
    if (!launch) return;
    Shot shot{};
    shot.firepower=veteranAbility(entity,"FIREPOWER")?int(definitions.rules().number("General","VeteranCombat",1.1)*1000+.5):1000;
    shot.view.id=nextShot++; shot.view.start=planar(start); shot.view.target=endpoint; shot.view.started=now;shot.view.targetHeightLeptons=endHeight;
    shot.view.impact=now+static_cast<Tick>((launch->flight256+255)/256);
    shot.view.sourceType=identity.type->id; shot.view.facing=fireFacing; shot.view.invisible=projectile.invisible;
    shot.weapon=&weapon; shot.source=identity.id; shot.owner=identity.owner;
    shot.location=start; shot.velocity=launch->velocity; shot.targetEntity=arm->target;shot.scatter=scatter;
    if(projectile.homing) {
      const auto length=std::max<std::int64_t>(1,motion::length(finish-start));const auto speed=projectileSpeed(weapon,arm->target && airTarget(entities.at(arm->target)));
      shot.velocity={(finish.x-start.x)*speed/length,(finish.y-start.y)*speed/length,(finish.z-start.z)*speed/length};
    }
    shot.view.previous=shot.view.position=planar(start);
    shot.view.previousHeightLeptons=shot.view.heightLeptons=static_cast<int>(start.z/motion::lepton);
    shot.view.weaponId=weapon.id; shot.view.image=projectile.image;
    emitLaser(shot);
    if(weapon.charges)registry.emplace_or_replace<CoilCharger>(entity,arm->target);
    else projectiles.push_back(std::move(shot));
    // Weapon Anim lists use clockwise compass names, unlike infantry SHP groups.
    const auto muzzleAnimation=weapon.muzzleAnimations.size()==8?weapon.muzzleAnimations[((facing+4096)/8192+3)&7]:weapon.muzzleAnimation;
    effect(start,false,muzzleAnimation,identity.type->id,facing);
    }
    const bool startingSalvo=arm->burstRemaining==0;
    if(arm->burstRemaining) --arm->burstRemaining;
    else {arm->burstRemaining=weapon.burst-1;arm->nextShot=now+static_cast<Tick>(veteranRof(entity,weapon));}
    arm->burstNext=now+5;
    // Charge a native ammo point on the first missile of the burst. Cancelling
    // the remaining missile must not refund a fired salvo.
    if(pilot && startingSalvo && pilot->ammo>0)--pilot->ammo;
    if(pilot && pilot->ammo==0 && !arm->burstRemaining)returnAircraft(entity);
  }
  std::int64_t projectileSpeed(const WeaponDef& weapon,bool airborne) const {
    const auto base=static_cast<std::int64_t>(weapon.speed)*motion::lepton;
    return weapon.turboBoost && airborne?base*std::clamp(int(definitions.rules().number("CombatDamage","TurboBoost",1.5)*1000+.5),1,10000)/1000:base;
  }
  void impact(const Shot& shot) {
    const auto& warhead = definitions.warhead(shot.weapon->warhead);
    for (const auto& [id, entity] : entities) {
      (void)id;
      if(registry.get<Health>(entity).hp<=0 || limbo(entity) || frozen(entity) || registry.all_of<Contained>(entity))continue;
      const auto& projectile=definitions.projectile(shot.weapon->projectile);
      if(airTarget(entity)?!projectile.antiAir:!projectile.antiGround)continue;
      if(submerged(entity)) {
        const auto source=definitions.units().find(shot.view.sourceType);
        if(!shot.weapon->id.starts_with('@') && (source==definitions.units().end() || !definitions.targetDomainAllowed(source->second,false,true,true)))continue;
      }
      const auto& identity = registry.get<Identity>(entity);
      if(warhead.radiation && identity.type->immuneToRadiation)continue;
      if(identity.type->wall && !warhead.damagesWalls)continue;
      if(shot.weapon->repairs && (identity.owner!=shot.owner || identity.type->category!=TypeCategory::Vehicle))continue;
      if(warhead.sonic && identity.type->typeImmune && identity.type->id==shot.view.sourceType)continue;
      const auto& transform = registry.get<Transform>(entity);
      const auto vertical=(height(entity)*motion::lepton-shot.location.z)/motion::scale;
      auto distance = integerSqrt(distanceSquared(transform.current, shot.view.target)+square(vertical));
      if (identity.type->category == TypeCategory::Building) for (const auto cell : footprint(*identity.type, transform.cell))
        distance = std::min(distance, integerSqrt(distanceSquared(position(cell), shot.view.target)+square(vertical)));
      if(identity.type->wall) {
        // A swept projectile hits the wall's edge, not its cell center. Use
        // the occupied volume so a zero-spread shell can destroy that wall.
        const auto center=position(transform.cell);
        const Position nearest{std::clamp(shot.view.target.x,center.x-500,center.x+500),
                               std::clamp(shot.view.target.y,center.y-500,center.y+500)};
        const auto ground=grid.heightAt(center.x,center.y)*motion::lepton;
        const auto dz=(shot.location.z-std::clamp(shot.location.z,ground,ground+128*motion::lepton))/motion::scale;
        distance=integerSqrt(distanceSquared(nearest,shot.view.target)+square(dz));
      }
      if(warhead.spreadMilliCells==0 && shot.targetEntity && id!=shot.targetEntity) continue;
      // Occupancy anchors are not zero-size hit shapes. Measure splash falloff
      // from the infantry body's edge, as in OpenRA's SpreadDamageWarhead.
      // Never round a foot unit to its cell: distinct subcells and real Z still
      // matter, and a moving target can escape a non-homing shell's impact.
      const int bodyRadius=identity.type->infantry?definitions.profile().infantryHitRadiusLeptons*1000/256:0;
      distance=std::max<std::int64_t>(0,distance-bodyRadius);
      // A direct target selects the victim, but does not make an arcing shell
      // home onto a unit that has moved away from the impact point.
      const int reach=std::max(warhead.spreadMilliCells,identity.type->infantry?0:100);
      if (distance > reach) continue;
      if(warhead.sonic)if(const auto parasite=infestor(id);parasite && registry.get<Identity>(entities.at(parasite)).type->leap->naval) {
        auto& state=registry.get<leap::State>(entities.at(parasite));state.phase=leap::Phase::Landing;state.started=now;state.suppressedUntil=now+50;
      }
      const int attenuation = warhead.spreadMilliCells > 0 ?
        100 - static_cast<int>(std::min<std::int64_t>(distance, warhead.spreadMilliCells) * (100 - warhead.percentAtMax) / warhead.spreadMilliCells) : 100;
      auto* foot=registry.try_get<Infantry>(entity);
      const int prone=foot && (foot->posture==InfantryPosture::Prone || foot->posture==InfantryPosture::GoingProne)?warhead.proneDamagePercent:100;
      auto damage = static_cast<std::int64_t>(shot.weapon->damage) * warhead.versesPercent[identity.type->armor] * attenuation * prone / 1000000;
      if(damage>0) {
        damage=damage*shot.firepower/1000;
        if(veteranAbility(entity,"STRONGER"))damage=std::max<std::int64_t>(1,int(damage/std::max(.001,definitions.rules().number("General","VeteranArmor",1.5))));
      }
      auto& health = registry.get<Health>(entity);
      if(const auto parasite=infestor(id);parasite && entities.contains(parasite)) {
        auto& state=registry.get<leap::State>(entities.at(parasite));
        if(damage<0){state.removed=true;registry.get<Health>(entities.at(parasite)).hp=0;}
        else if(damage>=registry.get<Identity>(entities.at(parasite)).type->leap->suppressionThreshold) {
          if(registry.get<Identity>(entities.at(parasite)).type->leap->naval){state.phase=leap::Phase::Landing;state.started=now;state.suppressedUntil=now+50;}
          else state.suppressedUntil=now+50;
        }
      }
      if(damage>0)if(auto* sub=registry.try_get<Submerge>(entity)){sub->stage=0;sub->surfacedUntil=now+std::max(1,int(definitions.rules().number("General","CloakDelay",.02)*900));}
      health.hp = static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(health.hp) - damage, 0, identity.type->strength));
      if(foot && damage>0) {
        foot->death=warhead.infantryDeath;
        if(!swimming(entity) && !identity.type->teleporter && !identity.type->infantry->fearless && (foot->posture==InfantryPosture::Standing || foot->posture==InfantryPosture::Prone || foot->posture==InfantryPosture::GoingProne)) {
          // Initial compatibility profile; fear thresholds/decay require vanilla traces.
          foot->proneUntil=now+120;
          if(foot->posture==InfantryPosture::Standing) {
            foot->firing=false;foot->posture=InfantryPosture::GoingProne;
            footAnimation(*foot,"DOWN",false);foot->postureUntil=now+footDuration(identity,"DOWN");
          }
        }
      }
    }
    const auto water=definitions.rules().list("CombatDamage","SplashList");
    const bool waterImpact=waterAt(cellAt(shot.view.target)) && shot.location.z<=std::int64_t(grid.heightAt(shot.view.target.x,shot.view.target.y)+32)*motion::lepton;
    if(waterImpact && shot.weapon->damage>=30 && !water.empty()) {
      effect(shot.location,true,canonicalId(water[std::min<std::size_t>(water.size()-1,std::size_t(shot.weapon->damage/100))]),shot.view.sourceType,shot.view.facing);
    } else if (!warhead.animations.empty()) {
      // Vanilla AnimList selects by damage in 25-damage bands, clamped to the
      // available list. This is not a random small/large explosion toggle.
      const auto index=std::min<std::size_t>(static_cast<std::size_t>(shot.weapon->damage/25),warhead.animations.size()-1);
      effect(shot.location,true,warhead.animations[index],shot.view.sourceType,shot.view.facing);
    }
    if(shot.weapon->radiationLevel>0)
      radiationField.refresh(cellAt(planar(shot.location)),warhead.spreadMilliCells,shot.weapon->radiationLevel,now,definitions.radiation());
  }
  Position bombPosition(entt::entity entity) const {
    const auto& pose=registry.get<Transform>(entity);const auto& type=*registry.get<Identity>(entity).type;
    return type.category==TypeCategory::Building?Position{pose.current.x+(type.foundationWidth-1)*500,pose.current.y+(type.foundationHeight-1)*500}:pose.current;
  }
  void detonateBomb(entt::entity entity) {
    const auto bomb=registry.get<AttachedBomb>(entity);
    registry.remove<AttachedBomb>(entity); // Remove before area damage / chained death.
    Shot blast{};blast.weapon=&definitions.weapon(definitions.ivanBomb().weapon);blast.source=bomb.source;blast.owner=bomb.owner;
    blast.view.target=bombPosition(entity);blast.location=fixed(blast.view.target,height(entity));
    blast.view.sourceType=bomb.sourceType;blast.view.facing=registry.get<Transform>(entity).heading;
    impact(blast);
  }
  void bombsStep() {
    for(const auto& [id,entity]:entities) {
      (void)id;const auto* bomb=registry.try_get<AttachedBomb>(entity);
      if(bomb && !frozen(entity) && registry.get<Health>(entity).hp>0 && now>=bomb->detonates && !limbo(entity) && !registry.all_of<Contained>(entity))detonateBomb(entity);
    }
  }
  void selfHealing() {
    if(now%definitions.profile().repairFrames)return;
    for(const auto& [id,entity]:entities) {
      (void)id;const auto& type=*registry.get<Identity>(entity).type;auto& hp=registry.get<Health>(entity).hp;
      if(!type.selfHealing || hp<=0 || limbo(entity) || frozen(entity) || registry.all_of<Contained>(entity))continue;
      const auto ceiling=std::int64_t(type.strength)*definitions.profile().selfHealCapMillionths/compat::kRatioScale;
      if(hp<ceiling)++hp; // Native self-heal is independent of RepairStep and costs no credits.
    }
  }
  void resolveDeaths() {
    // Always choose the smallest unresolved stable ID. Damage may kill an
    // earlier ID, so restart the search after each detonation, without recursion.
    for(;;) {
      const auto dead=std::find_if(entities.begin(),entities.end(),[&](const auto& item) {
        return registry.get<Health>(item.second).hp<=0 && !registry.all_of<DeathResolution>(item.second);
      });
      if(dead==entities.end())break;
      const auto [id,entity]=*dead;
      const auto& identity=registry.get<Identity>(entity);const auto& type=*identity.type;
      auto& resolution=registry.emplace<DeathResolution>(entity);
      if(const auto* manager=registry.try_get<NavalSpawner>(entity))for(const auto& node:manager->slots)if(node.unit && entities.contains(node.unit)) {
        const auto child=entities.at(node.unit);const auto* rocket=registry.try_get<SpawnedMissile>(child);
        if(!rocket || !rocket->flight.launched){registry.get<Health>(child).hp=0;registry.emplace_or_replace<Erased>(child);}
      }
      if(type.airport)for(const auto& [childId,child]:entities) {
        (void)childId;if(const auto* pilot=registry.try_get<Pilot>(child);pilot && pilot->airport==id && pilot->mission==AircraftMission::Landed)registry.get<Health>(child).hp=0;
      }
      if(registry.all_of<Erased>(entity)){resolution.detonated=true;continue;}
      if(registry.all_of<AttachedBomb>(entity) && !limbo(entity) && !registry.all_of<Contained>(entity)) {
        const auto* interaction=registry.try_get<BuildingInteraction>(entity);
        if(!interaction || !interaction->consumed)detonateBomb(entity);
      }
      const auto* current=definitions.weaponForTarget(type,false,false,gunnerMode(entity));
      // FV's explicit CRNuke is armed by a suicide passenger weapon, never by
      // an empty IFV or ordinary infantry. Resolve this BEFORE clearing cargo.
      const WeaponDef* weapon=nullptr;
      if(!limbo(entity) && !registry.all_of<Contained>(entity) && (type.explodes || (current && current->suicide))) {
        if(!type.deathWeapon.empty())weapon=&definitions.weapon(type.deathWeapon);
        else if(!type.collateralWeapon.empty())weapon=&definitions.weapon(type.collateralWeapon);
        else weapon=current;
        if(weapon && !weapon->supported)weapon=nullptr;
      }
      if(auto* cargo=registry.try_get<Cargo>(entity)) {
        if(type.garrisonable)while(!cargo->passengers.empty() && unloadOne(entity)) {}
        // Limbo passengers are removed with their host. They cannot detonate
        // independently in the world or multiply a suicide IFV's one explosion.
        for(const auto passenger:cargo->passengers)registry.get<Health>(entities.at(passenger)).hp=0;
        cargo->passengers.clear();
      }
      if(!weapon)continue;
      resolution.detonated=true;
      const auto& pose=registry.get<Transform>(entity);
      Shot blast{};blast.weapon=weapon;blast.source=id;blast.owner=identity.owner;
      blast.location=fixed(pose.current,height(entity));blast.view.target=pose.current;
      blast.view.sourceType=type.id;blast.view.facing=pose.heading;
      impact(blast);
      if(definitions.warhead(weapon->warhead).animations.empty() && !type.deathAnimation.empty())effect(blast.location,true,type.deathAnimation,type.id,pose.heading);
    }
  }
  void shrapnel(const Shot& parent,std::vector<Shot>& children) {
    const auto& projectile=definitions.projectile(parent.weapon->projectile);
    if(projectile.shrapnelWeapon.empty() || projectile.shrapnelCount<=0 || !parent.targetEntity)return;
    const auto hit=entities.find(parent.targetEntity);
    // Vanilla shrapnel triggers on a unit hit, not a building or empty ground.
    if(hit==entities.end() || registry.get<Identity>(hit->second).type->category==TypeCategory::Building)return;
    const auto& weapon=definitions.weapon(projectile.shrapnelWeapon);if(!weapon.supported)return;
    std::vector<std::pair<std::int64_t,EntityId>> candidates;
    for(const auto& [id,entity]:entities) {
      const auto& identity=registry.get<Identity>(entity);
      if(id==parent.targetEntity || identity.owner==parent.owner || registry.get<Health>(entity).hp<=0 ||
          (limbo(entity) || registry.any_of<Contained,FactoryExit>(entity) || airTarget(entity)) || definitions.warhead(weapon.warhead).versesPercent[identity.type->armor]<=0)continue;
      const auto distance=distanceSquared(planar(parent.location),registry.get<Transform>(entity).current);
      if(distance<=square(weapon.rangeMilliCells))candidates.emplace_back(distance,id);
    }
    std::sort(candidates.begin(),candidates.end());
    for(int i=0;i<std::min(projectile.shrapnelCount,32);++i) {
      EntityId target=0;Position point{};int elevation=0;
      if(i<static_cast<int>(candidates.size())) {
        target=candidates[i].second;const auto entity=entities.at(target);point=registry.get<Transform>(entity).current;elevation=height(entity);
      } else {
        const auto radius=1+scatterRandom()%weapon.rangeMilliCells;const auto angle=motion::unitVector(scatterRandom()&65535);const auto origin=planar(parent.location);
        point={origin.x+angle.x*radius/65536,origin.y+angle.y*radius/65536};elevation=grid.heightAt(point.x,point.y);
      }
      Shot child{};child.weapon=&weapon;child.source=parent.source;child.owner=parent.owner;child.targetEntity=target;
      child.location=parent.location;child.velocity=fixed(point,elevation)-child.location;
      child.view.id=nextShot++;child.view.start=planar(child.location);child.view.target=point;
      child.view.previous=child.view.position=child.view.start;child.view.targetHeightLeptons=elevation;
      child.view.previousHeightLeptons=child.view.heightLeptons=static_cast<int>(child.location.z/motion::lepton);
      child.view.started=now;child.view.impact=now+1;child.view.invisible=true;child.view.weaponId=weapon.id;child.view.sourceType=parent.view.sourceType;
      emitLaser(child);children.push_back(std::move(child));
    }
  }
  bool clipProjectile(Shot& shot,motion::Vector before,motion::Vector& after) const {
    const auto& type=definitions.projectile(shot.weapon->projectile);
    // Swept segment samples at <=1/32 cell prevent a fast projectile from
    // tunnelling through a one-cell wall or cliff between logic frames.
    const int samples=std::max(1,static_cast<int>((motion::length(after-before)+31999)/32000));
    for (int i=1;i<=samples;++i) {
      const motion::Vector p{before.x+(after.x-before.x)*i/samples,before.y+(after.y-before.y)*i/samples,before.z+(after.z-before.z)*i/samples};
      const auto xy=planar(p); const auto cell=cellAt(xy); const auto& surface=grid.cell(cell).surface;
      const auto ground=grid.heightAt(xy.x,xy.y)*motion::lepton;
      const bool wall=type.subjectToWalls && surface.wallHeightLeptons>0 && p.z<=ground+surface.wallHeightLeptons*motion::lepton;
      const bool cliff=type.subjectToCliffs && surface.cliff && p.z<=ground;
      // An instantaneous ray may start at the previous impact on the
      // ground (prism fragments). Touching a level floor along that ray
      // must not detonate it immediately at its own origin.
      const bool floor=!type.waterBound && !shot.weapon->leap && p.z<=ground && (type.subjectToElevation || surface.heightLeptons==0) &&
        !(type.invisible && p.z==ground && i<samples);
      if(type.waterBound && !waterAt(cell)) {
        shot.targetEntity=0;after={before.x+(after.x-before.x)*(i-1)/samples,before.y+(after.y-before.y)*(i-1)/samples,before.z+(after.z-before.z)*(i-1)/samples};return true;
      }
      if (wall || cliff || floor) {
        if(wall || cliff || (floor && distanceSquared(xy,shot.view.target)>square(100))) shot.targetEntity=0;
        if(wall)if(const auto found=structures.find(key(cell));found!=structures.end())
          if(const auto entity=entities.find(found->second);entity!=entities.end() && registry.get<Identity>(entity->second).type->wall)
            shot.targetEntity=found->second;
        after=p;
        // Remove sub-step rounding at the intended ground impact, without
        // snapping a shell through obstacles or onto a moving target.
        if (!wall && !cliff && distanceSquared(xy,shot.view.target)<=square(32))
          after=fixed(shot.view.target,grid.heightAt(shot.view.target.x,shot.view.target.y));
        return true;
      }
    }
    return false;
  }
  void emitLaser(const Shot& source) {
    if(!source.weapon->laser && !source.weapon->electricBolt && !source.weapon->radiationBeam)return;
    // LaserZap is a firing visual. Do not wait for the invisible damage
    // projectile's next update, and do not let drawing mutate that projectile.
    auto ray=source;auto end=fixed(ray.view.target,ray.view.targetHeightLeptons);
    clipProjectile(ray,ray.location,end);
    beams.push_back({source.view.start,planar(end),source.view.heightLeptons,
      static_cast<int>(end.z/motion::lepton),now,static_cast<Tick>(source.weapon->electricBolt?17:source.weapon->laserDuration),source.owner,source.weapon->id});
  }
  void projectilesStep() {
    std::vector<Shot> flying,children;
    for (auto shot:projectiles) {
      if (shot.view.started==now) { flying.push_back(std::move(shot)); continue; }
      const auto& type=definitions.projectile(shot.weapon->projectile);
      const auto before=shot.location;
      auto after=before+shot.velocity;
      shot.view.previous=planar(before); shot.view.previousHeightLeptons=static_cast<int>(before.z/motion::lepton);
      bool detonated=false;
      if (type.invisible) {
        if (!shot.scatter)if(const auto it=entities.find(shot.targetEntity);it!=entities.end() && !registry.all_of<Contained>(it->second) && !limbo(it->second)) {
          shot.view.target=registry.get<Transform>(it->second).current;shot.view.targetHeightLeptons=height(it->second);
        }
        after=fixed(shot.view.target,shot.view.targetHeightLeptons); detonated=true;
      } else if(type.homing) {
        if(const auto it=entities.find(shot.targetEntity);it!=entities.end() && !registry.all_of<Contained>(it->second) && !limbo(it->second)) {
          shot.view.target=registry.get<Transform>(it->second).current;shot.view.targetHeightLeptons=height(it->second);
        }
        else shot.targetEntity=0;
        const auto target=fixed(shot.view.target,shot.view.targetHeightLeptons);
        const auto delta=target-before;const auto length=motion::length(delta),speed=projectileSpeed(*shot.weapon,shot.view.targetHeightLeptons>grid.heightAt(shot.view.target.x,shot.view.target.y));
        if(length<=speed) {after=target;detonated=true;}
        else {
          const auto current=direction({shot.velocity.x,shot.velocity.y}),desired=direction({delta.x,delta.y});
          const int turn=((desired-current+98304)%65536)-32768,step=std::clamp(turn,-std::max(1,type.turnRate)*64,std::max(1,type.turnRate)*64);
          const auto heading=(current+step+65536)%65536;
          const auto planarSpeed=integerSqrt(square(speed)-std::min(square(speed),square(delta.z*speed/length)));
          const auto velocity=motion::unitVector(heading);
          shot.velocity={velocity.x*planarSpeed/65536,velocity.y*planarSpeed/65536,delta.z*speed/length};after=before+shot.velocity;
        }
      }
      {
        detonated=clipProjectile(shot,before,after) || detonated;
        if(!type.homing)shot.velocity.z-=(type.vertical?std::max(0,type.acceleration):definitions.profile().gravity)*motion::lepton;
      }
      if(type.waterBound && now%3==0)effect(before,false,"BBBLELRG",shot.view.sourceType,shot.view.facing);
      shot.location=after; shot.view.position=planar(after); shot.view.heightLeptons=static_cast<int>(after.z/motion::lepton);
      if (now-shot.view.started>4096) detonated=true;
      if (detonated) {
        shot.view.target=planar(after);
        shrapnel(shot,children);impact(shot);
      }
      else flying.push_back(std::move(shot));
    }
    for(auto& child:children)flying.push_back(std::move(child));
    projectiles=std::move(flying);
  }
  void transitions() {
    // Stable IDs define mutation order. EnTT storage order never defines game rules.
    for (const auto& [id, entity] : entities) {
      auto* deploy = registry.try_get<Deploy>(entity);
      if (!deploy || frozen(entity)) continue;
      auto& transform = registry.get<Transform>(entity);
      const bool unfolding = deploy->target->category == TypeCategory::Building;
      transform.heading = transform.bodyTurn.update(transform.heading,12*2048,registry.get<Identity>(entity).type->rot,now);
      transform.turret = transform.heading;
      if (transform.heading != 12 * 2048 || (!unfolding && now < deploy->complete)) continue;
      const auto* target = deploy->target;
      auto& identity = registry.get<Identity>(entity);
      auto& health = registry.get<Health>(entity);
      health.hp = std::max(1, static_cast<int>(static_cast<std::int64_t>(health.hp) * target->strength / identity.type->strength));
      identity.type = target;
      transform.cell = deploy->cell;
      transform.previous = transform.current = position(transform.cell);
      release(id);
      auto& owners = target->category == TypeCategory::Building ? structures : occupied;
      for (const auto cell : footprint(*target, transform.cell)) { owners[key(cell)] = id; refresh(cell); }
      // The vehicle turns first, then its target building owns the buildup phase.
      // Converting straight to Complete skips every frame of the deployment SHP.
      components(entity, *target, transform.cell, unfolding);
      harvesting.add(id,*target);
      transform.animationSpeed = clock.speed();
    }
  }
  void closeExit(EntityId factoryId, EntityId unitId) {
    const auto found = entities.find(factoryId);
    if (found == entities.end()) return;
    auto& factory = registry.get<Factory>(found->second);
    if (factory.exitingUnit != unitId) return;
    factory.exitingUnit = 0;
    factory.phase = registry.get<Identity>(found->second).type->factoryExit->clearFrames == 0 ?
                    FactoryExitPhase::Idle : FactoryExitPhase::Clearing;
    factory.phaseStarted = now;
  }
  void advanceFactoryExits() {
    for (const auto& [id, entity] : entities) {
      (void)id;
      auto* factory = registry.try_get<Factory>(entity);
      if (!factory || frozen(entity)) continue;
      const auto& definition = registry.get<Identity>(entity).type->factoryExit;
      if (!definition) continue;
      if (factory->phase == FactoryExitPhase::Preparing && now - factory->phaseStarted >= static_cast<Tick>(definition->prepareFrames)) {
        factory->phase = FactoryExitPhase::Driving; factory->phaseStarted = now;
      } else if (factory->phase == FactoryExitPhase::Clearing && now - factory->phaseStarted >= static_cast<Tick>(definition->clearFrames)) {
        factory->phase = FactoryExitPhase::Idle; factory->phaseStarted = now;
      }
    }
    // Finish on the tick after arrival: previous/current presentation positions are
    // both outside before removing the explicit link to the building's over-layers.
    for (const auto& [id, entity] : entities) {
      auto* exit = registry.try_get<FactoryExit>(entity);
      if(!exit)continue;
      if(const auto* pilot=registry.try_get<Pilot>(entity)) {
        const auto factory=entities.find(exit->factory);
        if(factory!=entities.end()) {
          const auto& host=*registry.get<Identity>(factory->second).type;
          const auto& pose=registry.get<Transform>(factory->second);
          if(int(pilot->flight.position.z/motion::lepton)<grid.heightAt(pose.current.x,pose.current.y)+host.buildingHeight+64)continue;
        }
      } else if(!registry.get<Mobile>(entity).path.empty())continue;
      const auto deferred = exit->deferred;
      closeExit(exit->factory, id);
      for (const auto cell : exit->route) {
        const auto found = reservations.find(key(cell));
        if (found != reservations.end() && found->second == id) { reservations.erase(found); refresh(cell); }
      }
      registry.remove<FactoryExit>(entity);
      if (deferred) apply(*deferred);
    }
  }
  void beginNavalProduction(entt::entity entity) {
    const auto owner=registry.get<Identity>(entity).owner;
    const auto typeId=registry.get<Factory>(entity).queue.front().type;
    const auto& type=definitions.unit(typeId);
    auto candidates=entryPerimeter(entity);
    const auto anchor=registry.get<Transform>(entity).cell;
    std::stable_sort(candidates.begin(),candidates.end(),[&](auto a,auto b){return distanceSquared(position(a),position(anchor))<distanceSquared(position(b),position(anchor));});
    for(const auto cell:candidates) {
      if(!waterAt(cell) || !free(cell) || travelSpeed(type,cell,cell)<=0)continue;
      const auto id=spawn(type,cell,owner,false);if(!id)continue;
      trained(id,owner);
      auto& factory=registry.get<Factory>(entity);factory.queue.pop_front();factory.phase=FactoryExitPhase::Idle;factory.phaseStarted=factory.cycleStarted=now;
      move(entities.at(id),factory.rally.value_or(TileCoord{cell.x+3,cell.y}));message="Naval unit launched";return;
    }
    auto& factory=registry.get<Factory>(entity);factory.phase=FactoryExitPhase::Blocked;factory.phaseStarted=now;message="Shipyard water exit blocked";
  }
  void startShipyardRepair(entt::entity actor,EntityId dockId) {
    const auto dock=entities.find(dockId);const auto& identity=registry.get<Identity>(actor);
    if(dock==entities.end() || !identity.type->naval || !identity.type->groundMobile || registry.all_of<Contained>(actor))return;
    const auto& destination=registry.get<Identity>(dock->second);
    if(!destination.type->naval || !destination.type->unitRepair || destination.owner!=identity.owner || !complete(dock->second) || registry.get<Health>(dock->second).hp<=0 || registry.get<Health>(actor).hp>=identity.type->strength)return;
    stop(actor);registry.emplace<ShipyardRepair>(actor,dockId,now);message="Ship approaching repair berth";
  }
  void navalStep() {
    const int stages=std::clamp(definitions.rules().integer("General","CloakingStages",9),1,64);
    std::map<EntityId,int> repairBerths;
    for(const auto& [id,actor]:entities) {
      const auto& identity=registry.get<Identity>(actor);const auto& pose=registry.get<Transform>(actor);
      if(registry.get<Health>(actor).hp<=0 || limbo(actor) || registry.all_of<Contained>(actor) || frozen(actor))continue;
      if(auto* sub=registry.try_get<Submerge>(actor)) {
        if(!waterAt(pose.cell) || grappled(actor) || registry.all_of<ShipyardRepair>(actor)){sub->stage=0;sub->surfacedUntil=now+18;}
        else if(now<sub->surfacedUntil)sub->stage=0;
        else sub->stage=std::min(stages,int((now-sub->surfacedUntil+1)/std::max(1,identity.type->cloakingSpeed)));
      }
      if(identity.type->naval && identity.type->category==TypeCategory::Vehicle && !identity.type->cloakable && waterAt(pose.cell) && pose.current!=pose.previous && now%6==0)
        effect(fixed(pose.current,grid.heightAt(pose.current.x,pose.current.y)),false,canonicalId(definitions.rules().string("AudioVisual","Wake","WAKE1")),identity.type->id,pose.heading);
      auto* task=registry.try_get<ShipyardRepair>(actor);if(!task)continue;
      const auto dock=entities.find(task->dock);
      if(dock==entities.end() || registry.get<Health>(dock->second).hp<=0 || registry.get<Identity>(dock->second).owner!=identity.owner || !complete(dock->second)) {stop(actor);continue;}
      auto* mobile=registry.try_get<Mobile>(actor);if(!mobile)continue;
      auto perimeter=entryPerimeter(dock->second);
      const bool atBerth=std::find(perimeter.begin(),perimeter.end(),pose.cell)!=perimeter.end() && waterAt(pose.cell);
      if(!atBerth) {
        if(mobile->requested || mobile->reserved || !mobile->path.empty())continue;
        std::stable_sort(perimeter.begin(),perimeter.end(),[&](auto a,auto b){return distanceSquared(position(a),pose.current)<distanceSquared(position(b),pose.current);});
        for(const auto cell:perimeter)if(waterAt(cell) && free(cell,id) && travelSpeed(*identity.type,cell,cell)>0){move(actor,cell);break;}
        continue;
      }
      if(mobile->reserved || !mobile->path.empty())continue;
      mobile->requested=false;
      if(++repairBerths[task->dock]>std::max(1,definitions.rules().integer(registry.get<Identity>(dock->second).type->id,"NumberOfDocks",1)))continue;
      if(now<task->next || !powered(dock->second))continue;
      auto& hp=registry.get<Health>(actor).hp;
      const int heal=std::min(definitions.profile().repairStep,identity.type->strength-hp);
      if(heal<=0){registry.remove<ShipyardRepair>(actor);continue;}
      const auto numerator=std::int64_t(identity.type->cost)*heal*definitions.profile().repairPercentMillionths;
      const auto denominator=std::int64_t(identity.type->strength)*compat::kRatioScale;
      const int price=int((numerator+denominator-1)/denominator);
      task->next=now+std::max(1,int(definitions.rules().number("General","URepairRate",.016)*900));
      if(funds(identity.owner)<price){message="Ship repair waiting for credits";continue;}
      funds(identity.owner)-=price;hp+=heal;
      if(hp==identity.type->strength){registry.remove<ShipyardRepair>(actor);message="Ship repaired";}
    }
  }
  void beginFactoryExit(entt::entity entity) {
    const auto factoryId = registry.get<Identity>(entity).id;
    const auto owner = registry.get<Identity>(entity).owner;
    const auto* factoryType = registry.get<Identity>(entity).type;
    if(definitions.unit(registry.get<Factory>(entity).queue.front().type).aircraft){beginAirProduction(entity);return;}
    const auto& definition = *factoryType->factoryExit;
    const auto anchor = registry.get<Transform>(entity).cell;
    if(factoryType->naval) {beginNavalProduction(entity);return;}
    if(factoryType->factory=="INFANTRYTYPE") {
      const auto offset=definition.path.front();const TileCoord cell{anchor.x+offset.x,anchor.y+offset.y};
      const auto typeId=registry.get<Factory>(entity).queue.front().type;
      if(availableFootSlot(cell,0,owner)<0 || (definitions.unit(typeId).flight && availableAirSlot(cell,0)<0)) {
        auto& f=registry.get<Factory>(entity);f.phase=FactoryExitPhase::Blocked;f.phaseStarted=now;return;
      }
      const auto id=spawn(definitions.unit(typeId),cell,owner,false);if(!id)return;
      trained(id,owner);
      auto& f=registry.get<Factory>(entity);f.queue.pop_front();f.phase=FactoryExitPhase::Idle;f.phaseStarted=f.cycleStarted=now;
      const auto target=f.rally.value_or(TileCoord{cell.x+2,cell.y+2});
      move(entities.at(id),target);return;
    }
    std::vector<TileCoord> route;
    route.reserve(definition.path.size());
    for (const auto offset : definition.path) {
      const TileCoord cell{anchor.x + offset.x, anchor.y + offset.y};
      if (!free(cell, 0, factoryId)) {
        auto& factory = registry.get<Factory>(entity);
        if (factory.phase != FactoryExitPhase::Blocked) { factory.phase = FactoryExitPhase::Blocked; factory.phaseStarted = now; }
        return;
      }
      route.push_back(cell);
    }
    // No component references survive spawn: an EnTT pool can move on insertion.
    const auto typeId = registry.get<Factory>(entity).queue.front().type;
    const auto id = spawn(definitions.unit(typeId), route.front(), owner, false, factoryId);
    if (!id) return;
    trained(id,owner);
    const auto vehicle = entities.at(id);
    for (const auto cell : route) { reservations[key(cell)] = id; refresh(cell); }
    auto& transform = registry.get<Transform>(vehicle);
    transform.heading = direction({route[1].x - route[0].x, route[1].y - route[0].y});
    transform.turret = transform.heading;
    transform.previousHeading = transform.heading; transform.previousTurret = transform.turret;
    transform.bodyTurn.reset(transform.heading); transform.turretTurn.reset(transform.turret);
    auto& mobile = registry.get<Mobile>(vehicle);
    mobile.destination = route.back();
    mobile.path.assign(route.begin() + 1, route.end());
    registry.emplace<FactoryExit>(vehicle, factoryId, std::move(route), std::nullopt);
    auto& factory = registry.get<Factory>(entity);
    factory.queue.pop_front();
    factory.exitingUnit = id;
    factory.cycleStarted = factory.phaseStarted = now;
    factory.animationSpeed = clock.speed();
    factory.phase = definition.prepareFrames > 0 ? FactoryExitPhase::Preparing : FactoryExitPhase::Driving;
  }
  void production() {
    std::vector<entt::entity> ready;
    for (const auto& [id, entity] : entities) {
      (void)id;
      auto* factory = registry.try_get<Factory>(entity);
      if (!factory || factory->queue.empty() || !complete(entity) || (registry.get<Identity>(entity).type->naval && !powered(entity)) ||
          (factory->phase != FactoryExitPhase::Idle && factory->phase != FactoryExitPhase::Blocked)) continue;
      auto& item = factory->queue.front();
      if (item.remaining > 0) --item.remaining;
      if (item.remaining == 0) ready.push_back(entity);
    }
    // Recheck/reserve atomically in stable factory ID order. Two factories cannot
    // both claim an external cell just because it was free earlier in the tick.
    for (const auto entity : ready) beginFactoryExit(entity);
  }
  void step() {
    presentCurrentFrame = false;
    ++now;
    movementStats={}; assignmentBudget=8192;
    for (const auto& [id, entity] : entities) { (void)id; auto& t = registry.get<Transform>(entity); t.previous = t.current; t.previousHeading = t.heading; t.previousTurret = t.turret; }
    std::stable_sort(commands.begin(), commands.end(), [](const auto& a, const auto& b) { return std::tie(a.tick, a.player, a.sequence) < std::tie(b.tick, b.player, b.sequence); });
    std::size_t processed = 0;
    for (const auto& command : commands) { if (command.tick > now) break; apply(command); ++processed; }
    commands.erase(commands.begin(), commands.begin() + static_cast<std::ptrdiff_t>(processed));
    temporalStep(false);
    assignFormations();
    transitions();
    selfHealing();
    for (const auto& [id, entity] : entities) {
      (void)id;
      if (const auto* structure = registry.try_get<Structure>(entity); structure && structure->complete == now)
        registry.get<Transform>(entity).animationSpeed = clock.speed();
    }
    advanceFactoryExits();
    std::size_t pathBudget = 8192;
    harvesting.step(now,*this,pathBudget);
    const auto order=trafficOrder();
    planTraffic(order,pathBudget);
    movementStats.pathExpanded=8192-pathBudget;
    advanceMovement(order);
    crushInfantry();
    flightStep(pathBudget);
    aircraftStep();
    navalStep();
    disguiseStep();
    prepareBuildingInteractions(pathBudget);
    for(const auto& [id,entity]:entities) {(void)id;infantryStep(entity,pathBudget);}
    for(const auto& [id,entity]:entities)if(auto* sprite=registry.try_get<SpriteAnimation>(entity)) {
      (void)id;const auto& p=registry.get<Transform>(entity);const bool moving=p.previous!=p.current;
      if(moving!=sprite->moving){sprite->moving=moving;sprite->started=now;}
    }
    for (const auto& [id, entity] : entities) { (void)id; weapons(entity); }
    navalAircraftStep();
    missileStep();
    sonicStep();
    projectilesStep();
    c4Step();
    bombsStep();
    radiationStep();
    temporalStep(true);
    resolveDeaths();
    leapStep();
    completeBuildingInteractions();
    buildingServices();
    cargoStep();
    resolveDeaths();
    temporalStep(false);
    fallingCorpses();
    for (auto it = entities.begin(); it != entities.end();) {
      if (registry.get<Health>(it->second).hp <= 0) {
        if(const auto* state=registry.try_get<leap::State>(it->second);state && state->phase==leap::Phase::Infesting)
          if(const auto host=entities.find(state->victim);host!=entities.end())if(const auto* link=registry.try_get<Infested>(host->second);link && link->parasite==it->first)registry.remove<Infested>(host->second);
        if(const auto* contained=registry.try_get<Contained>(it->second))if(const auto host=entities.find(contained->host);host!=entities.end())
          if(auto* cargo=registry.try_get<Cargo>(host->second))std::erase(cargo->passengers,it->first);
        const auto* interaction=registry.try_get<BuildingInteraction>(it->second);
        if(const auto* pilot=registry.try_get<Pilot>(it->second);pilot && !registry.all_of<Erased>(it->second)) {
          const auto& identity=registry.get<Identity>(it->second);const auto& pose=registry.get<Transform>(it->second);
          if(height(it->second)>grid.heightAt(pose.current.x,pose.current.y)+32)airWrecks.push_back({identity.type->id,identity.owner,pose.current,pose.heading,height(it->second),height(it->second),identity.type->aircraft->movement.crash});
          else effect(fixed(pose.current,height(it->second)),true,"TWLT070",identity.type->id,pose.heading);
        }
        if(const auto* rocket=registry.try_get<SpawnedMissile>(it->second)) {
          if(const auto parent=entities.find(rocket->parent);parent!=entities.end())if(auto* launcher=registry.try_get<MissileLauncher>(parent->second);launcher && launcher->child==it->first) {
            launcher->child=0;if(rocket->cancelled){launcher->ready=now;if(auto* arm=registry.try_get<Armament>(parent->second))arm->nextShot=now;}
          }
          if(!rocket->detonated && !rocket->cancelled)effect(rocket->flight.position,true,"TWLT070","V3ROCKET",rocket->flight.heading);
        }
        if(const auto* child=registry.try_get<NavalAircraft>(it->second)) {
          if(!child->recovered && !registry.all_of<Erased>(it->second))effect(child->flight.position,true,"TWLT070",registry.get<Identity>(it->second).type->id);
        }
        EntityId navalParent=0;
        if(const auto* child=registry.try_get<NavalAircraft>(it->second))navalParent=child->parent;
        if(const auto* child=registry.try_get<SpawnedMissile>(it->second))navalParent=child->parent;
        if(navalParent)if(const auto parent=entities.find(navalParent);parent!=entities.end())if(auto* manager=registry.try_get<NavalSpawner>(parent->second))for(auto& node:manager->slots)if(node.unit==it->first) {
          node.unit=0;node.phase=NavalSpawnPhase::Regenerating;node.ready=now+registry.get<Identity>(parent->second).type->navalSpawn->regenerateFrames;
        }
        if(const auto* air=registry.try_get<Airborne>(it->second);air && !registry.all_of<Erased>(it->second)) {
          const auto& identity=registry.get<Identity>(it->second);const auto& pose=registry.get<Transform>(it->second);
          corpses.push_back({identity.type->id,"AIRDEATHSTART",identity.owner,pose.current,pose.heading,air->height,now,600,true,air->height,identity.type->flight->crash});
        } else if(const auto* foot=registry.try_get<Infantry>(it->second);foot && !registry.get<DeathResolution>(it->second).detonated && !limbo(it->second) && !registry.all_of<Contained>(it->second) && (!interaction || !interaction->consumed)) {
          const auto& identity=registry.get<Identity>(it->second); const auto& pose=registry.get<Transform>(it->second);
          if(foot->death==5 || foot->death==6 || foot->death==7)effect(fixed(pose.current,grid.heightAt(pose.current.x,pose.current.y)),true,foot->death==5?"ELECTRO":foot->death==6?canonicalId(definitions.rules().string("AudioVisual","InfantryExplode","S_BANG34")):"NUKEDIE",identity.type->id,pose.heading);
          else {
            const std::string sequence=swimming(it->second)?(foot->death==2?"WETDIE2":"WETDIE1"):(foot->death==2?"DIE2":"DIE1");
            corpses.push_back({identity.type->id,sequence,identity.owner,pose.current,pose.heading,
              grid.heightAt(pose.current.x,pose.current.y),now,static_cast<Tick>(footDuration(identity,sequence))});
          }
        }
        if(!registry.all_of<Erased>(it->second) && registry.get<Identity>(it->second).type->spriteVehicle && !limbo(it->second) && !registry.all_of<Contained>(it->second)) {
          const auto& t=registry.get<Transform>(it->second);effect(fixed(t.current,grid.heightAt(t.current.x,t.current.y)),true,"TWLT070");
        }
        if(const auto& identity=registry.get<Identity>(it->second);identity.type->naval && identity.type->category==TypeCategory::Vehicle && !registry.any_of<Contained,Erased>(it->second) && waterAt(registry.get<Transform>(it->second).cell)) {
          const auto& pose=registry.get<Transform>(it->second);effect(fixed(pose.current,height(it->second)),true,"H2O_EXP1",identity.type->id,pose.heading);
        }
        if (const auto* exit = registry.try_get<FactoryExit>(it->second)) closeExit(exit->factory, it->first);
        harvesting.remove(it->first); release(it->first); registry.destroy(it->second); it = entities.erase(it);
      }
      else ++it;
    }
    production();
    std::erase_if(corpses,[&](const auto& corpse){return !corpse.falling && now-corpse.started>=corpse.duration;});
    std::erase_if(effects, [&](const auto& effect) { return now-effect.started>effect.duration; });
    std::erase_if(beams,[&](const auto& beam){return now-beam.started>=beam.duration;});
  }
  std::optional<HarvestActor> miningActor(MiningEntity id) const override {
    const auto it=entities.find(id); if (it==entities.end()) return std::nullopt;
    const auto entity=it->second;
    const auto& identity=registry.get<Identity>(entity);
    const auto* mobile=registry.try_get<Mobile>(entity);
    return HarvestActor{identity.type,identity.owner,registry.get<Transform>(entity).cell,
      complete(entity) && !registry.all_of<FactoryExit>(entity) && registry.get<Health>(entity).hp>0,
      mobile && (mobile->requested || mobile->reserved || !mobile->path.empty())};
  }
  MiningMove miningMove(MiningEntity id, TileCoord cell, std::size_t& budget) override {
    const auto entity=entities.at(id);
    auto* mobile=registry.try_get<Mobile>(entity);
    if (!mobile || !free(cell,id)) return MiningMove::Blocked;
    if (mobile->reserved || !budget) return MiningMove::Deferred;
    const auto start=registry.get<Transform>(entity).cell;
    if (start==cell) { stop(entity); return MiningMove::Started; }
    std::size_t expanded=0;
    const auto path=findTilePath(grid,start,cell,std::min<std::size_t>(budget,2048),&expanded,
      [&](TileCoord a,TileCoord b) { return travelSpeed(*registry.get<Identity>(entity).type,a,b); });
    budget-=std::min(budget,std::max<std::size_t>(1,expanded));
    if (!path || path->target!=cell) return MiningMove::Blocked;
    stop(entity); mobile->destination=cell; mobile->exactTarget=true;
    mobile->path.assign(path->cells.begin(),path->cells.end());
    return MiningMove::Started;
  }
  bool miningFree(TileCoord cell, MiningEntity self) const override { return free(cell,self); }
  void miningHalt(MiningEntity id) override { if (entities.contains(id)) stop(entities.at(id)); }
  bool miningTeleport(MiningEntity id, TileCoord cell) override {
    if (!free(cell,id)) return false;
    const auto entity=entities.at(id);
    if (registry.get<Mobile>(entity).reserved) return false;
    stop(entity); release(id); occupied[key(cell)]=id; refresh(cell);
    auto& transform=registry.get<Transform>(entity);
    transform.cell=cell; transform.previous=transform.current=position(cell);
    auto& mobile=registry.get<Mobile>(entity); mobile.segmentProgress=0; mobile.destination=cell;
    return true;
  }
  bool miningCredit(std::uint32_t owner, int amount) override {
    auto& balance=funds(owner);
    if (balance>std::numeric_limits<int>::max()-amount) return false;
    balance+=amount; return true;
  }
  MiningEntity miningSpawn(std::string_view type, TileCoord cell, std::uint32_t owner) override {
    return spawn(definitions.unit(type),cell,owner,false);
  }
};

World::World(const DefinitionRegistry& definitions) : impl_(std::make_unique<Impl>(definitions)) {}
World::~World() = default;
const std::vector<AircraftWreck>& World::aircraftWrecks() const {return impl_->airWrecks;}
EntityId World::spawn(std::string_view type, TileCoord cell, std::uint32_t owner, bool constructing) { return impl_->spawn(impl_->definitions.unit(type), cell, owner, constructing); }
bool World::enqueue(Command command) {
  if(command.kind==CommandKind::GroupMove && (command.members.empty() || command.members.size()>128)) return false;
  if ((command.kind == CommandKind::Produce || command.kind == CommandKind::PlaceBuilding) && command.type.empty()) return false;
  if (command.tick < impl_->now || command.sequence <= impl_->sequences[command.player] || !validCell(command.cell)) return false;
  if (!command.type.empty() && !impl_->definitions.units().contains(canonicalId(command.type))) return false;
  impl_->sequences[command.player] = command.sequence;
  impl_->commands.push_back(std::move(command));
  return true;
}
void World::step() { impl_->step(); }
void World::advance(std::uint64_t microseconds, std::uint32_t frameBudget) {
  const auto count = impl_->clock.advance(microseconds, frameBudget);
  for (std::uint32_t i = 0; i < count; ++i) impl_->step();
}
void World::setGameSpeed(GameSpeed speed) {
  const bool wasUnlimited = !targetGameFrameRate(impl_->clock.speed());
  impl_->clock.setSpeed(speed);
  if (wasUnlimited && targetGameFrameRate(speed)) {
    impl_->presentCurrentFrame = true;
    // Unlimited presentation shows current state. Do not interpolate backwards
    // towards an older position when returning to a finite rate.
    for (const auto& [id, entity] : impl_->entities) {
      (void)id;
      auto& transform = impl_->registry.get<Transform>(entity);
      transform.previous = transform.current;
      transform.previousHeading = transform.heading; transform.previousTurret = transform.turret;
      if(auto* air=impl_->registry.try_get<Airborne>(entity))air->previousHeight=air->height;
      if(auto* p=impl_->registry.try_get<Pilot>(entity))p->flight.previous=p->flight.position;
    }
  }
}
GameSpeed World::gameSpeed() const { return impl_->clock.speed(); }
std::uint64_t World::pendingFrames() const { return impl_->clock.pendingFrames(); }
Tick World::tick() const { return impl_->now; }
double World::interpolation() const { return impl_->presentCurrentFrame ? 1.0 : impl_->clock.interpolation(); }
const MapGrid& World::map() const { return impl_->grid; }
std::vector<EntityId> World::occupants(TileCoord cell) const {
  std::vector<EntityId> ids;
  if(const auto id=structureAt(cell))ids.push_back(id);
  if(const auto it=impl_->occupied.find(key(cell));it!=impl_->occupied.end())ids.push_back(it->second);
  if(const auto it=impl_->footOccupied.find(key(cell));it!=impl_->footOccupied.end())for(auto id:it->second)if(id)ids.push_back(id);
  std::sort(ids.begin(),ids.end());return ids;
}
std::vector<EntityId> World::airOccupants(TileCoord cell) const {
  std::vector<EntityId> result;
  if(const auto it=impl_->airOccupied.find(key(cell));it!=impl_->airOccupied.end())for(auto id:it->second)if(id)result.push_back(id);
  std::sort(result.begin(),result.end());return result;
}
EntityId World::occupant(TileCoord cell) const {
  // Preserve the existing factory passage contract: mobile occupancy is returned
  // before the structure whose footprint an exiting vehicle is crossing.
  if(const auto it=impl_->occupied.find(key(cell));it!=impl_->occupied.end())return it->second;
  if(const auto it=impl_->footOccupied.find(key(cell));it!=impl_->footOccupied.end())for(auto id:it->second)if(id)return id;
  return structureAt(cell);
}
EntityId World::structureAt(TileCoord cell) const { const auto it = impl_->structures.find(key(cell)); return it == impl_->structures.end() ? 0 : it->second; }
void World::setBuildable(TileCoord cell, bool value) {
  if (!validCell(cell) || impl_->structures.contains(key(cell)) || impl_->occupied.contains(key(cell)) || impl_->reservations.contains(key(cell)) || impl_->hasFoot(cell)) throw std::invalid_argument("Cannot edit occupied terrain");
  impl_->grid.setBuildable(cell, value);
  if (value) impl_->blockedTerrain.erase(key(cell)); else impl_->blockedTerrain.insert(key(cell));
}
void World::setTerrain(TileCoord cell, TerrainSurface surface) {
  if (!validCell(cell) || impl_->structures.contains(key(cell)) || impl_->occupied.contains(key(cell)) || impl_->reservations.contains(key(cell)) || impl_->hasFoot(cell))
    throw std::invalid_argument("Cannot edit occupied terrain");
  if (surface.heightLeptons<0 || surface.heightLeptons>16384 || std::abs(surface.slopeX)>256 ||
      std::abs(surface.slopeY)>256 || surface.wallHeightLeptons<0 || surface.wallHeightLeptons>4096)
    throw std::invalid_argument("Terrain height/slope outside supported range");
  surface.land=canonicalId(surface.land);
  impl_->grid.setSurface(cell,surface); impl_->terrain[key(cell)]=std::move(surface); impl_->refresh(cell);
}
int World::credits(std::uint32_t player) const { const auto it = impl_->money.find(player); return it == impl_->money.end() ? impl_->definitions.profile().startingCredits : it->second; }
PowerSnapshot World::power(std::uint32_t player) const {return impl_->power(player);}
EspionageSnapshot World::espionage(std::uint32_t player) const {const auto it=impl_->espionage.find(player);return it==impl_->espionage.end()?EspionageSnapshot{}:it->second;}
bool World::canPlaceBuilding(std::string_view type,TileCoord cell) const {return impl_->buildingPlacement(impl_->definitions.unit(type),cell);}
bool World::canRepair(EntityId id,std::uint32_t player) const {return impl_->canRepair(id,player);}
bool World::canSell(EntityId id,std::uint32_t player) const {return impl_->canSell(id,player);}
int World::repairCost(EntityId id) const {return impl_->repairCost(id);}
int World::saleRefund(EntityId id) const {return impl_->saleRefund(id);}
void World::setResource(TileCoord cell, std::string_view type, int amount) { impl_->harvesting.setResource(cell,type,amount); }
std::optional<ResourceCell> World::resourceAt(TileCoord cell) const { return impl_->harvesting.resourceAt(cell); }
std::vector<ResourceCell> World::resources() const { return impl_->harvesting.resources(); }
const std::string& World::lastMessage() const { return impl_->message; }
const std::vector<EffectSnapshot>& World::effects() const { return impl_->effects; }
const std::vector<InfantryCorpse>& World::infantryCorpses() const {return impl_->corpses;}
const std::vector<BeamSnapshot>& World::beams() const {return impl_->beams;}
const std::vector<radiation::Site>& World::radiationSites() const {return impl_->radiationField.sites();}
int World::radiationLevel(TileCoord c) const {return impl_->radiationField.level(c,impl_->now,impl_->definitions.radiation());}
int World::radiationLight(TileCoord c) const {return impl_->radiationField.light(c,impl_->now,impl_->definitions.radiation());}
bool World::recognizes(EntityId target,std::uint32_t player) const {
  const auto found=impl_->entities.find(target);return found!=impl_->entities.end() && impl_->recognizes(found->second,player);
}
std::vector<ShotSnapshot> World::shots() const {
  std::vector<ShotSnapshot> result;
  for (const auto& shot : impl_->projectiles) result.push_back(shot.view);
  return result;
}
std::vector<EntitySnapshot> World::snapshot() const {
  std::vector<EntitySnapshot> result;
  result.reserve(impl_->entities.size());
  for (const auto& [id, entity] : impl_->entities) {
    const auto& identity = impl_->registry.get<Identity>(entity);
    const auto& transform = impl_->registry.get<Transform>(entity);
    EntitySnapshot view;
    view.id = id; view.owner = identity.owner; view.type = identity.type->id;
    if(const auto* bomb=impl_->registry.try_get<AttachedBomb>(entity))view.bomb=*bomb;
    view.previous = transform.previous; view.position = transform.current; view.cell = transform.cell;
    view.previousHeading = transform.previousHeading; view.previousTurretHeading = transform.previousTurret;
    view.heading = transform.heading; view.turretHeading = transform.turret;
    view.heightLeptons=impl_->grid.heightAt(transform.current.x,transform.current.y);
    view.previousHeightLeptons=impl_->grid.heightAt(transform.previous.x,transform.previous.y);
    const auto& surface=impl_->grid.cell(cellAt(transform.current)).surface;
    if (identity.type->isTilter) { view.slopeX=surface.slopeX; view.slopeY=surface.slopeY; }
    view.animationSpeed = transform.animationSpeed;
    view.powered=impl_->powered(entity);view.coilChargers=impl_->coilChargers(entity);view.overpowered=impl_->overpowered(entity);
    if(const auto* arm=impl_->registry.try_get<Armament>(entity))view.firingStarted=identity.type->fireDelay>0?arm->chargeStarted:arm->firedAt;
    if(identity.type->wall) {
      constexpr std::array<TileCoord,4> directions{{{0,-1},{1,0},{0,1},{-1,0}}};
      for(int side=0;side<4;++side) {
        const auto adjacent=TileCoord{view.cell.x+directions[side].x,view.cell.y+directions[side].y};
        if(const auto wall=impl_->structures.find(key(adjacent));wall!=impl_->structures.end()) {
          const auto& other=impl_->registry.get<Identity>(impl_->entities.at(wall->second));
          if(other.type->wall && other.type->image==identity.type->image)view.wallConnections|=1<<side;
        }
      }
    }
    view.hp = impl_->registry.get<Health>(entity).hp; view.maxHp = identity.type->strength;
    if(const auto* repair=impl_->registry.try_get<BuildingRepair>(entity)) {
      view.repairing=true;view.repairPaused=repair->paused;view.repairStarted=repair->started;
    }
    if(const auto* sub=impl_->registry.try_get<Submerge>(entity)){view.submerged=impl_->submerged(entity);view.cloakStage=sub->stage;view.surfacedUntil=sub->surfacedUntil;}
    if(const auto* dock=impl_->registry.try_get<ShipyardRepair>(entity))view.repairDock=dock->dock;
    if(const auto* charge=impl_->registry.try_get<C4Charge>(entity))view.c4Detonates=charge->detonates;
    if(const auto* chrono=impl_->registry.try_get<ChronoRecovery>(entity))view.chronoReady=chrono->ready;
    if(const auto* temporal=impl_->registry.try_get<TemporalVictim>(entity)){view.frozen=true;view.frozenSince=temporal->started;view.warpRemaining=temporal->remaining;}
    if(const auto* spy=impl_->registry.try_get<SpyDisguise>(entity)){view.spyDisguise=spy->type;view.disguiseOwner=spy->owner;}
    view.veteran=impl_->registry.all_of<Veteran>(entity);
    view.harvesting=impl_->harvesting.snapshot(id);
    if(const auto* contained=impl_->registry.try_get<Contained>(entity))view.containedBy=contained->host;
    if(const auto* weapon=impl_->activeWeapon(entity))view.activeWeapon=weapon->id;
    if(const auto* disguise=impl_->registry.try_get<Camouflage>(entity)) {
      view.disguised=disguise->active;view.disguise=disguise->image;
      view.disguiseStarted=disguise->started;
      view.disguiseFireUntil=disguise->visibleUntil;
      view.disguiseExposedUntil=disguise->exposedUntil;
    }
    if(identity.type->chargeTurrets>0)if(const auto* arm=impl_->registry.try_get<Armament>(entity);arm && !arm->weapon.empty() && arm->nextShot>impl_->now) {
      const auto rof=impl_->definitions.weapon(arm->weapon).rof;
      view.chargeTurret=std::min<int>(identity.type->chargeTurrets-1,(arm->nextShot-impl_->now)*identity.type->chargeTurrets/rof);
    }
    if(const auto* cargo=impl_->registry.try_get<Cargo>(entity)) {
      const auto& type=*identity.type;const int mode=type.gunner?impl_->gunnerMode(entity):0;
      const auto turret=type.gunnerTurrets.find(mode);
      view.cargo=CargoSnapshot{cargo->passengers,impl_->cargoUsed(entity),type.garrisonable?type.maxOccupants:type.passengers,mode,
        turret==type.gunnerTurrets.end()?0:turret->second,cargo->unloading};
    }
    if (const auto* mobile = impl_->registry.try_get<Mobile>(entity)) {
      view.destination = mobile->destination;
      view.awaitingFormation=mobile->pendingGroup!=0;
      if (!mobile->path.empty() || mobile->requested || mobile->pendingGroup || mobile->yielding) view.activity = Activity::Moving;
    }
    if(const auto* foot=impl_->registry.try_get<Infantry>(entity)) {
      view.infantry=InfantrySnapshot{foot->subCell,foot->posture,foot->sequence,foot->animationStarted,foot->loop};
      view.destination=foot->destination;
      if(foot->requested || !foot->path.empty())view.activity=Activity::Moving;
    }
    view.infestedBy=impl_->infestor(id);view.grappled=impl_->grappled(entity);
    if(const auto* manager=impl_->registry.try_get<NavalSpawner>(entity)) {
      view.spawns=manager->slots;view.launcherEmpty=std::none_of(manager->slots.begin(),manager->slots.end(),[](const auto& slot){return !slot.unit && slot.phase==NavalSpawnPhase::Docked;});
    }
    if(const auto* p=impl_->registry.try_get<Pilot>(entity)) {
      view.slopeX=view.slopeY=0;
      view.aircraft=AircraftSnapshot{p->mission,p->airport,p->slot,p->ammo,Tick(p->ammo<identity.type->aircraft->ammo?p->reload:0)};
      view.airborne=impl_->airTarget(entity);view.heightLeptons=int(p->flight.position.z/motion::lepton);view.previousHeightLeptons=int(p->flight.previous.z/motion::lepton);
      view.takingOff=p->mission!=AircraftMission::Landed && view.heightLeptons<impl_->flightSurface(view.cell)+identity.type->aircraft->movement.height-20;
      view.destination=cellAt(p->destination);if(p->mission!=AircraftMission::Landed && p->mission!=AircraftMission::Holding)view.activity=Activity::Moving;
    }
    if(const auto* plane=impl_->registry.try_get<NavalAircraft>(entity)) {
      view.spawnedAircraft=view.airborne=true;view.spawnParent=plane->parent;view.activity=Activity::Moving;
      view.heightLeptons=int(plane->flight.position.z/motion::lepton);view.previousHeightLeptons=int(plane->flight.previous.z/motion::lepton);
    }
    if(const auto* sprite=impl_->registry.try_get<SpriteAnimation>(entity)) {
      view.spriteSequence=sprite->moving?"WALK":"READY";view.spriteStarted=sprite->started;
      if(const auto* arm=impl_->registry.try_get<Armament>(entity);arm && arm->chargeUntil>impl_->now) {view.spriteSequence="FIREUP";view.spriteStarted=arm->chargeStarted;}
    }
    if(const auto* l=impl_->registry.try_get<leap::State>(entity);l && l->phase!=leap::Phase::Ground) {
      view.leaping=l->phase!=leap::Phase::Infesting;view.parasiteHost=l->phase==leap::Phase::Infesting?l->victim:0;view.stateStarted=l->started;
      view.previousHeightLeptons=static_cast<int>(l->previous.z/motion::lepton);view.heightLeptons=static_cast<int>(l->position.z/motion::lepton);
    }
    if(const auto* air=impl_->registry.try_get<Airborne>(entity)) {
      view.airborne=true;view.takingOff=air->takingOff;view.heightLeptons=air->height;view.previousHeightLeptons=air->previousHeight;
      view.destination=air->destination;if(air->requested || air->takingOff)view.activity=Activity::Moving;
    }
    if(const auto* rocket=impl_->registry.try_get<SpawnedMissile>(entity)) {
      view.missile=view.airborne=true;view.takingOff=!rocket->flight.launched;view.heightLeptons=static_cast<int>(rocket->flight.position.z/motion::lepton);
      view.previousHeightLeptons=static_cast<int>(rocket->flight.previous.z/motion::lepton);view.destination=cellAt(planar(rocket->flight.target));view.activity=Activity::Moving;
      view.missilePitch=rocket->flight.pitch;view.previousMissilePitch=rocket->flight.previousPitch;
    }
    if(const auto* launcher=impl_->registry.try_get<MissileLauncher>(entity)) {
      view.launcherEmpty=launcher->child || impl_->now<launcher->ready;view.launcherReady=launcher->ready;
    }
    if(const auto* task=impl_->registry.try_get<BuildingInteraction>(entity)) {
      view.buildingInteraction=BuildingInteractionSnapshot{task->target,task->kind,task->entering};
      view.activity=Activity::Moving;
    }
    if (const auto* building = impl_->registry.try_get<Structure>(entity)) {
      view.stateStarted = building->started; view.stateDuration = building->complete - building->started;
      if (building->complete > impl_->now) view.activity = Activity::Constructing;
    }
    if (const auto* deploy = impl_->registry.try_get<Deploy>(entity)) {
      view.activity = Activity::Transforming; view.stateStarted = deploy->started; view.stateDuration = deploy->complete - deploy->started;
    }
    if(const auto* sale=impl_->registry.try_get<BuildingSale>(entity)) {
      view.activity=Activity::Selling;view.stateStarted=sale->started;view.stateDuration=sale->complete-sale->started;
    }
    if (const auto* factory = impl_->registry.try_get<Factory>(entity)) {
      view.factoryPhase = factory->phase; view.factoryPhaseStarted = factory->phaseStarted;
      view.factoryCycleStarted = factory->cycleStarted; view.exitingUnit = factory->exitingUnit;
      view.productionAnimationSpeed = factory->animationSpeed;
      if (!factory->queue.empty()) {
        view.productionType = factory->queue.front().type; view.productionRemaining = factory->queue.front().remaining;
        view.productionTotal = factory->queue.front().total; view.queueSize = factory->queue.size();
      }
    }
    if (const auto* exit = impl_->registry.try_get<FactoryExit>(entity)) {
      view.activity = Activity::LeavingFactory; view.exitFactory = exit->factory; view.hasDeferredCommand = exit->deferred.has_value();
    }
    result.push_back(std::move(view));
  }
  return result;
}
std::uint64_t World::stateHash() const {
  std::uint64_t hash = 14695981039346656037ULL;
  const auto add = [&](std::uint64_t value) { for (int i = 0; i < 8; ++i) { hash ^= (value >> (i * 8)) & 255; hash *= 1099511628211ULL; } };
  const auto text = [&](std::string_view value) { add(value.size()); for (const unsigned char ch : value) add(ch); };
  text(impl_->definitions.fingerprint()); add(impl_->now); add(impl_->nextId); add(impl_->nextShot);
  add(impl_->scatterState);
  add(impl_->harvesting.stateHash());
  for(const auto& w:impl_->airWrecks){text(w.type);add(w.owner);add(w.position.x);add(w.position.y);add(w.heading);add(w.height);add(w.speed);}
  add(impl_->outages.size());for(const auto& [owner,until]:impl_->outages){add(owner);add(until);}
  add(impl_->espionage.size());for(const auto& [owner,bonus]:impl_->espionage){add(owner);add(bonus.veteranInfantry);add(bonus.veteranVehicles);add(bonus.alliedTech);add(bonus.sovietTech);}
  for (const auto& view : snapshot()) {
    add(view.id); text(view.type); add(view.owner); add(view.position.x); add(view.position.y); add(view.cell.x); add(view.cell.y); add(view.hp); add(view.heading); add(view.turretHeading);
    add(static_cast<int>(view.activity)); add(view.stateStarted); add(view.stateDuration);
    add(view.bomb.has_value());
    if(view.bomb){add(view.bomb->source);add(view.bomb->owner);add(view.bomb->planted);add(view.bomb->detonates);text(view.bomb->sourceType);}
    const auto entity = impl_->entities.at(view.id);
    const auto& pose = impl_->registry.get<Transform>(entity);
    for (const auto* rotation : {&pose.bodyTurn,&pose.turretTurn}) {
      add(rotation->target); add(rotation->delta); add(rotation->duration); add(rotation->started);
    }
    if (const auto* m = impl_->registry.try_get<Mobile>(entity)) {
      add(m->requested); add(m->exactTarget); add(m->destination.x); add(m->destination.y); add(m->segmentProgress); add(m->speedRemainder); add(m->retry);
      add(m->cooperative); add(m->yielding); add(m->yieldFor); add(m->blockedBy); add(m->yieldOrigin.x); add(m->yieldOrigin.y); add(m->waitSince); add(m->yieldUntil); add(m->laneWaiting); add(m->pendingGroup);
      add(m->flowGroup);
      add(m->path.size()); for (const auto c : m->path) { add(c.x); add(c.y); }
      add(m->driving); add(m->reserved.has_value());
      if (m->reserved) { add(m->reserved->x); add(m->reserved->y); }
      add(m->track.startHeading); add(m->track.endHeading);
      for (const auto p:m->track.points) { add(p.x); add(p.y); }
      for (const auto d:m->track.distances) add(d);
      for (const auto heading:m->track.headings) add(heading);
      add(m->sweptReservations.size()); for (const auto c:m->sweptReservations) { add(c.x); add(c.y); }
    }
    if(const auto* f=impl_->registry.try_get<Infantry>(entity)) {
      add(f->subCell);add(f->targetSubCell);add(f->death);add(f->destination.x);add(f->destination.y);add(f->next.x);add(f->next.y);
      add(f->requested);add(f->firing);add(f->fired);add(f->segmentStart.x);add(f->segmentStart.y);add(f->progress);add(f->length);add(f->remainder);
      add(f->retry);add(f->postureUntil);add(f->proneUntil);add(f->fireAt);add(f->attackUntil);add(static_cast<int>(f->posture));text(f->sequence);add(f->animationStarted);add(f->loop);
      add(f->path.size());for(auto c:f->path){add(c.x);add(c.y);}
    }
    if(const auto* l=impl_->registry.try_get<leap::State>(entity)) {
      add(static_cast<int>(l->phase));add(l->victim);add(l->started);add(l->duration);add(l->nextDamage);add(l->suppressedUntil);add(l->removed);
      for(const auto p:{l->start,l->position,l->previous}){add(p.x);add(p.y);add(p.z);}
    }
    if(const auto* sprite=impl_->registry.try_get<SpriteAnimation>(entity)){add(sprite->moving);add(sprite->started);}
    if(const auto* link=impl_->registry.try_get<Infested>(entity))add(link->parasite);
    if (const auto* arm = impl_->registry.try_get<Armament>(entity)) { add(arm->active); add(arm->automatic); add(arm->target); add(arm->ground.x); add(arm->ground.y); add(arm->nextShot);add(arm->burstRemaining);add(arm->burstNext);text(arm->weapon);add(arm->windupTarget);add(arm->firedAt);add(arm->chargeStarted);add(arm->chargeUntil); }
    if(const auto* charger=impl_->registry.try_get<CoilCharger>(entity))add(charger->target);
    if(const auto* arm=impl_->registry.try_get<Armament>(entity))add(arm->autoChargeSuppressed);
    if(const auto* manager=impl_->registry.try_get<NavalSpawner>(entity)) {add(manager->target);add(manager->launchNext);add(manager->ground.x);add(manager->ground.y);add(manager->groundFire);for(const auto& slot:manager->slots){add(slot.unit);add(slot.ready);add(int(slot.phase));}}
    if(const auto* p=impl_->registry.try_get<Pilot>(entity)) {add(int(p->mission));add(p->destination.x);add(p->destination.y);add(p->airport);add(p->slot);add(p->ammo);add(p->reload);add(p->repairNext);add(p->flight.position.x);add(p->flight.position.y);add(p->flight.position.z);add(p->flight.heading);add(p->flight.speedMilli);}
    if(const auto* plane=impl_->registry.try_get<NavalAircraft>(entity)) {add(plane->parent);add(plane->target);add(plane->slot);add(int(plane->phase));add(plane->flight.position.x);add(plane->flight.position.y);add(plane->flight.position.z);add(plane->flight.heading);add(plane->flight.speedMilli);add(plane->recovered);add(plane->launched);add(plane->ground.x);add(plane->ground.y);add(plane->groundFire);}
    if(const auto* launcher=impl_->registry.try_get<MissileLauncher>(entity)){add(launcher->child);add(launcher->ready);}
    if(const auto* rocket=impl_->registry.try_get<SpawnedMissile>(entity)) {
      add(rocket->parent);add(rocket->target);add(rocket->detonated);add(rocket->cancelled);const auto& m=rocket->flight;
      add(m.position.x);add(m.position.y);add(m.position.z);add(m.target.x);add(m.target.y);add(m.target.z);
      add(m.origin.x);add(m.origin.y);add(m.origin.z);add(m.travelled);
      add(m.started);add(m.heading);add(m.pitch);add(m.speedMilli);add(static_cast<int>(m.phase));add(m.launched);
    }
    if(const auto* air=impl_->registry.try_get<Airborne>(entity)) {
      add(air->height);add(air->speed);add(air->subCell);add(air->destinationSlot);add(air->nextSlot);add(air->destination.x);add(air->destination.y);
      add(air->retry);add(air->remainder);add(air->requested);add(air->takingOff);add(air->path.size());for(auto c:air->path){add(c.x);add(c.y);}
    }
    if(const auto* disguise=impl_->registry.try_get<Camouflage>(entity)) {
      text(disguise->image);add(disguise->active);add(disguise->started);add(disguise->visibleUntil);add(disguise->exposedUntil);
    }
    if(const auto* cargo=impl_->registry.try_get<Cargo>(entity)) {
      add(cargo->passengers.size());for(const auto passenger:cargo->passengers)add(passenger);
      add(cargo->originalOwner);add(cargo->unloading);add(cargo->retry);
    }
    add(view.submerged);add(view.cloakStage);add(view.surfacedUntil);add(view.repairDock);
    if(const auto* repair=impl_->registry.try_get<ShipyardRepair>(entity))add(repair->next);
    add(view.c4Detonates);add(view.chronoReady);add(view.frozen);add(view.frozenSince);add(view.warpRemaining);add(view.veteran);text(view.spyDisguise);add(view.disguiseOwner);
    add(impl_->registry.all_of<TemporalChannel>(entity));if(const auto* channel=impl_->registry.try_get<TemporalChannel>(entity)){add(channel->target);text(channel->weapon);}
    add(view.containedBy);
    if(const auto* task=impl_->registry.try_get<BuildingInteraction>(entity)) {
      add(task->target);add(task->targetOwner);add(static_cast<int>(task->kind));add(task->approach.x);add(task->approach.y);
      add(task->entryStart.x);add(task->entryStart.y);add(task->entryEnd.x);add(task->entryEnd.y);
      add(task->started);add(task->duration);add(task->retry);add(task->planned);add(task->entering);add(task->consumed);
    }
    if (const auto* d = impl_->registry.try_get<Deploy>(entity)) { text(d->target->id); add(d->cell.x); add(d->cell.y); }
    if(const auto* repair=impl_->registry.try_get<BuildingRepair>(entity)){add(repair->owner);add(repair->started);add(repair->next);add(repair->paused);}
    if(const auto* sale=impl_->registry.try_get<BuildingSale>(entity)){add(sale->owner);add(sale->started);add(sale->complete);}
    if (const auto* f = impl_->registry.try_get<Factory>(entity)) {
      add(f->rally.has_value());if(f->rally){add(f->rally->x);add(f->rally->y);}
      add(f->exitingUnit); add(static_cast<int>(f->phase)); add(f->phaseStarted); add(f->cycleStarted); add(f->queue.size());
      for (const auto& item : f->queue) { text(item.type); add(item.remaining); add(item.total); add(item.paid); }
    }
    if (const auto* exit = impl_->registry.try_get<FactoryExit>(entity)) {
      add(exit->factory); add(exit->route.size()); for (const auto cell : exit->route) { add(cell.x); add(cell.y); }
      add(exit->deferred.has_value());
      if (exit->deferred) {
        const auto& command = *exit->deferred;
        add(command.tick); add(command.player); add(command.sequence); add(static_cast<int>(command.kind));
        add(command.entity); add(command.target); add(command.cell.x); add(command.cell.y); text(command.type);
        add(command.members.size()); for(const auto id:command.members) add(id); add(command.exactDestination);
      }
    }
  }
  for (const auto& [player, money] : impl_->money) { add(player); add(money); }
  add(impl_->trafficLanes.size());
  for(const auto& [cell,lane]:impl_->trafficLanes) {
    add(cell.first); add(cell.second); add(lane.direction); add(lane.draining); add(lane.admitted); add(lane.approachUntil); add(lane.cells.size());
    for(const auto c:lane.cells) { add(c.x); add(c.y); }
  }
  add(impl_->formations.size());
  for(const auto& job:impl_->formations) {
    add(job.command.tick); add(job.command.player); add(job.command.sequence); add(job.command.cell.x); add(job.command.cell.y);
    add(job.actors.size()); for(const auto id:job.actors) add(id);
    add(job.slots.size()); for(const auto c:job.slots) { add(c.x); add(c.y); }
    add(job.sourceCenter.x); add(job.sourceCenter.y);
    add(job.costs.size()); for(const auto& row:job.costs) { add(row.size()); for(const auto value:row) add(value); }
  }
  add(impl_->nextFlowGroup); add(impl_->flowGroups.size());
  for(const auto& [id,g]:impl_->flowGroups) {
    add(id); add(g.center.x); add(g.center.y); add(g.minimum.x); add(g.minimum.y); add(g.maximum.x); add(g.maximum.y);
    add(g.dockingRadius); add(g.topology); add(g.members.size()); for(const auto actor:g.members) add(actor);
    add(g.profiles.size()); for(const auto& [profile,p]:g.profiles) { text(profile); text(p.type->id); add(p.field.stateHash()); }
  }
  for (const auto& [player, sequence] : impl_->sequences) { add(player); add(sequence); }
  for (const auto& cell : impl_->blockedTerrain) { add(cell.first); add(cell.second); }
  for (const auto& [cell,s]:impl_->terrain) { add(cell.first); add(cell.second); text(s.land); add(s.heightLeptons); add(s.slopeX); add(s.slopeY); add(s.wallHeightLeptons); add(s.cliff); }
  for (const auto* cells : {&impl_->structures, &impl_->occupied, &impl_->reservations}) {
    add(cells->size());
    for (const auto& [cell, owner] : *cells) { add(cell.first); add(cell.second); add(owner); }
  }
  for(const auto* slots:{&impl_->footOccupied,&impl_->footReservations,&impl_->airOccupied,&impl_->airReservations}) {
    add(slots->size());for(const auto& [cell,owners]:*slots){add(cell.first);add(cell.second);for(auto owner:owners)add(owner);}
  }
  for (const auto& shot : impl_->projectiles) {
    add(shot.view.id); add(shot.view.started); add(shot.view.impact); add(shot.view.target.x); add(shot.view.target.y); text(shot.weapon->id);
    add(shot.location.x); add(shot.location.y); add(shot.location.z);
    add(shot.velocity.x); add(shot.velocity.y); add(shot.velocity.z); add(shot.source); add(shot.owner); add(shot.targetEntity);
    add(shot.view.targetHeightLeptons);add(shot.scatter);add(shot.firepower);
  }
  for(const auto& wave:impl_->sonicWaves){add(wave.source);add(wave.target);add(wave.owner);add(wave.start.x);add(wave.start.y);add(wave.end.x);add(wave.end.y);add(wave.travelled);add(wave.started);text(wave.sourceType);add(wave.directDelivered);}
  for (const auto& command : impl_->commands) { add(command.tick); add(command.player); add(command.sequence); add(static_cast<int>(command.kind)); add(command.entity); add(command.target); add(command.cell.x); add(command.cell.y); text(command.type); add(command.members.size()); for(const auto id:command.members) add(id); add(command.exactDestination); }
  for(const auto& site:impl_->radiationField.sites()){add(site.cell.x);add(site.cell.y);add(site.spread);add(site.level);add(site.started);}
  return hash;
}
MovementDiagnostics World::movementDiagnostics() const { return impl_->movementStats; }
}
