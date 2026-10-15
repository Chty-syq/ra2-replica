#include "world.h"
#include "kinematics.h"
#include "traffic.h"
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
struct Armament { bool active = false; EntityId target = 0; Position ground; Tick nextShot = 0; bool automatic = false; };
struct Structure { Tick started = 0, complete = 0; };
struct ProductionItem { std::string type; int remaining = 0, total = 0, paid = 0; };
struct Factory {
  std::deque<ProductionItem> queue;
  EntityId exitingUnit = 0;
  FactoryExitPhase phase = FactoryExitPhase::Idle;
  Tick phaseStarted = 0, cycleStarted = 0;
  GameSpeed animationSpeed{};
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
  std::set<CellKey> blockedTerrain;
  std::map<CellKey,TerrainSurface> terrain;
  MapGrid grid{64, 64};
  std::map<std::uint32_t, int> money;
  std::map<std::uint32_t, std::uint64_t> sequences;
  std::vector<Command> commands;
  std::vector<Shot> projectiles;
  std::vector<EffectSnapshot> effects;
  Tick now = 0;
  EntityId nextId = 1;
  std::uint64_t nextShot = 1;
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
  void refresh(TileCoord cell) {
    const auto& surface=grid.cell(cell).surface;
    grid.setOccupied(std::vector<TileCoord>{cell},surface.wallHeightLeptons>0 || surface.cliff ||
      structures.contains(key(cell)) || occupied.contains(key(cell)) || reservations.contains(key(cell)));
  }
  bool free(TileCoord cell, EntityId self = 0, EntityId passage = 0) const {
    if (!validCell(cell) || !grid.cell(cell).buildable || grid.cell(cell).surface.wallHeightLeptons>0 || grid.cell(cell).surface.cliff) return false;
    const auto occupiedIt = occupied.find(key(cell)), reservedIt = reservations.find(key(cell));
    const auto structureIt = structures.find(key(cell));
    return (structureIt == structures.end() || structureIt->second == self || structureIt->second == passage) &&
           (occupiedIt == occupied.end() || occupiedIt->second == self) && (reservedIt == reservations.end() || reservedIt->second == self);
  }
  std::vector<TileCoord> footprint(const UnitTypeDef& type, TileCoord cell) const {
    if (type.category != TypeCategory::Building) return {cell};
    return foundationTiles({cell, type.foundationWidth, type.foundationHeight});
  }
  void release(EntityId id) {
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
    return type.groundMobile;
  }
  void components(entt::entity entity, const UnitTypeDef& type, TileCoord cell, bool constructing) {
    registry.remove<Mobile, Armament, Structure, Factory, Deploy, FactoryExit>(entity);
    if (type.groundMobile) registry.emplace<Mobile>(entity, cell);
    if (!type.primary.empty() && definitions.rules().contains(type.primary) && definitions.weapon(type.primary).supported) registry.emplace<Armament>(entity);
    if (type.category == TypeCategory::Building) {
      registry.emplace<Structure>(entity, now, constructing ? now + definitions.profile().buildupFrames : now);
      if (!type.factory.empty()) registry.emplace<Factory>(entity);
    }
  }
  EntityId spawn(const UnitTypeDef& type, TileCoord cell, std::uint32_t owner, bool constructing, EntityId passage = 0) {
    if (!validCell(cell) || !playable(type)) return 0;
    const auto cells = footprint(type, cell);
    if (!std::all_of(cells.begin(), cells.end(), [&](auto c) { return free(c, 0, passage); })) { message = "Placement blocked"; return 0; }
    const auto entity = registry.create();
    const auto id = nextId++;
    registry.emplace<Identity>(entity, id, owner, &type);
    registry.emplace<Health>(entity, type.strength);
    registry.emplace<Transform>(entity, position(cell), position(cell), cell).animationSpeed = clock.speed();
    components(entity, type, cell, constructing);
    entities.emplace(id, entity);
    auto& owners = type.category == TypeCategory::Building ? structures : occupied;
    for (const auto c : cells) { owners[key(c)] = id; refresh(c); }
    harvesting.add(id,type);
    return id;
  }
  bool complete(entt::entity entity) const {
    const auto* structure = registry.try_get<Structure>(entity);
    return !registry.all_of<Deploy>(entity) && (!structure || structure->complete <= now);
  }
  bool prerequisites(const UnitTypeDef& type, std::uint32_t owner) const {
    if (definitions.profile().sandbox) return true;
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
  void stop(entt::entity entity) {
    auto& pose = registry.get<Transform>(entity);
    pose.bodyTurn.reset(pose.heading); pose.turretTurn.reset(pose.turret);
    if (auto* armament = registry.try_get<Armament>(entity)) { armament->active = false; armament->automatic = false; armament->target=0; }
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
    const auto& identity = registry.get<Identity>(entity);
    const auto& transform = registry.get<Transform>(entity);
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
    const bool ownersMatch = identity.type->owners.empty() || type.owners.empty() ||
      std::any_of(type.owners.begin(), type.owners.end(), [&](const auto& owner) { return std::find(identity.type->owners.begin(), identity.type->owners.end(), owner) != identity.type->owners.end(); });
    if (!playable(type) || !ownersMatch || identity.type->naval != type.naval ||
        !identity.type->factoryExit || type.speed <= 0 || identity.type->factory != "UNITTYPE" || type.category != TypeCategory::Vehicle || !prerequisites(type, identity.owner)) { message = "Factory/technology cannot produce " + type.id; return; }
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
    if (registry.get<Identity>(entity).owner != command.player || registry.all_of<Deploy>(entity)) return;
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
               command.kind == CommandKind::Harvest || command.kind == CommandKind::ReturnToRefinery) exit->deferred = command;
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
      case CommandKind::Stop: stop(entity); break;
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
      case CommandKind::AttackGround:
        if (auto* armament = registry.try_get<Armament>(entity); armament && validCell(command.cell)) {
          stop(entity);
          armament->active = true;
          armament->target = command.kind == CommandKind::AttackEntity ? command.target : 0;
          armament->ground = position(command.cell);
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
    std::vector<EntityId> actors=command.members;
    std::sort(actors.begin(),actors.end()); actors.erase(std::unique(actors.begin(),actors.end()),actors.end());
    std::erase_if(actors,[&](EntityId id) {
      const auto it=entities.find(id);
      return it==entities.end() || registry.get<Identity>(it->second).owner!=command.player ||
        !registry.all_of<Mobile>(it->second) || registry.all_of<Deploy>(it->second);
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
        const auto speed=compat::movementLeptons(identity.type->speed)*motion::lepton*
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
  motion::Vector muzzle(const Transform& transform, const UnitTypeDef& type, int facing) const {
    const auto& surface=grid.cell(cellAt(transform.current)).surface;
    return fixed(transform.current,grid.heightAt(transform.current.x,transform.current.y))+
      motion::surfaceFlh(transform.heading,facing,type.primaryFireFlh,type.isTilter?surface.slopeX:0,type.isTilter?surface.slopeY:0);
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
    view.duration=static_cast<Tick>(std::max(1,end-art.start))*std::max(1,delay);
    effects.push_back(std::move(view));
  }
  void weapons(entt::entity entity) {
    auto* arm = registry.try_get<Armament>(entity);
    if (!arm || !complete(entity) || registry.all_of<FactoryExit>(entity)) return;
    const auto& identity = registry.get<Identity>(entity);
    const auto& weapon = definitions.weapon(identity.type->primary);
    auto& transform = registry.get<Transform>(entity);
    auto* mobile=registry.try_get<Mobile>(entity);
    const bool moving=mobile && (mobile->reserved || mobile->requested || !mobile->path.empty());
    if (arm->active && arm->target) {
      const auto it=entities.find(arm->target);
      if (it==entities.end() || registry.get<Health>(it->second).hp<=0 ||
          registry.get<Identity>(it->second).owner==identity.owner ||
          (arm->automatic && distanceSquared(transform.current,registry.get<Transform>(it->second).current)>square(weapon.rangeMilliCells))) {
        arm->active=false; arm->target=0;
      }
    }
    if (!arm->active && identity.type->canPassiveAcquire && (!moving || identity.type->opportunityFire)) {
      std::int64_t nearest=std::numeric_limits<std::int64_t>::max();
      for (const auto& [id,candidate]:entities) {
        if (registry.get<Identity>(candidate).owner==identity.owner || registry.get<Health>(candidate).hp<=0 ||
            !complete(candidate) || registry.all_of<FactoryExit>(candidate)) continue;
        const auto distance=distanceSquared(transform.current,registry.get<Transform>(candidate).current);
        if (distance<=square(weapon.rangeMilliCells) && distance<nearest) { nearest=distance; arm->target=id; }
      }
      if (nearest!=std::numeric_limits<std::int64_t>::max()) { arm->active=true; arm->automatic=true; }
    }
    if (!arm->active) {
      if (moving) transform.turret=transform.turretTurn.update(transform.turret,transform.heading,identity.type->rot,now);
      return;
    }
    Position target = arm->ground;
    Position targetVelocity{};
    if (arm->target) {
      const auto it = entities.find(arm->target);
      if (it == entities.end()) { arm->active = false; return; }
      if (registry.get<Identity>(it->second).owner == identity.owner) { arm->active = false; return; }
      const auto& other=registry.get<Transform>(it->second);
      target = other.current;
      targetVelocity={other.current.x-other.previous.x,other.current.y-other.previous.y};
    }
    const bool inRange=distanceSquared(transform.current,target)<=square(weapon.rangeMilliCells);
    if (!inRange && !arm->automatic && mobile &&
        ((!mobile->requested && mobile->path.empty()) || (now>=mobile->retry && distanceSquared(position(mobile->destination),target)>square(2000)))) {
      move(entity,cellAt(target)); mobile->retry=now+12;
    }
    if (inRange && !arm->automatic && mobile) {
      mobile->requested = false;
      if (mobile->reserved) mobile->path = {*mobile->reserved}; else mobile->path.clear();
    }
    const auto& projectile=definitions.projectile(weapon.projectile);
    const auto targetPoint=[&](Position p) { return fixed(p,grid.heightAt(p.x,p.y)); };
    Position aim=target;
    if (inRange && !projectile.invisible && (targetVelocity.x || targetVelocity.y)) {
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
    if (identity.type->turret || !moving) facing = rotation.update(facing,desired,identity.type->rot,now);
    if (!identity.type->turret) transform.turret = facing;
    if (!inRange || facing != desired || now < arm->nextShot ||
        (moving && (!identity.type->turret || !identity.type->opportunityFire))) return;
    const auto start=muzzle(transform,*identity.type,facing), finish=targetPoint(aim);
    const auto launch=projectile.invisible ? std::optional{motion::Launch{finish-start,256}} :
      motion::ballisticLaunch(finish-start,weapon.rangeMilliCells*256/1000,definitions.profile().gravity);
    if (!launch) return;
    Shot shot{};
    shot.view.id=nextShot++; shot.view.start=planar(start); shot.view.target=aim; shot.view.started=now;
    shot.view.impact=now+static_cast<Tick>((launch->flight256+255)/256);
    shot.view.sourceType=identity.type->id; shot.view.facing=facing; shot.view.invisible=projectile.invisible;
    shot.weapon=&weapon; shot.source=identity.id; shot.owner=identity.owner;
    shot.location=start; shot.velocity=launch->velocity; shot.targetEntity=arm->target;
    shot.view.previous=shot.view.position=planar(start);
    shot.view.previousHeightLeptons=shot.view.heightLeptons=static_cast<int>(start.z/motion::lepton);
    shot.view.weaponId=weapon.id; shot.view.image=projectile.image;
    projectiles.push_back(std::move(shot));
    effect(start,false,weapon.muzzleAnimation,identity.type->id,facing);
    arm->nextShot = now + static_cast<Tick>(weapon.rof);
  }
  void impact(const Shot& shot) {
    const auto& warhead = definitions.warhead(shot.weapon->warhead);
    for (const auto& [id, entity] : entities) {
      (void)id;
      const auto& identity = registry.get<Identity>(entity);
      const auto& transform = registry.get<Transform>(entity);
      const auto vertical=(grid.heightAt(transform.current.x,transform.current.y)*motion::lepton-shot.location.z)/motion::scale;
      auto distance = integerSqrt(distanceSquared(transform.current, shot.view.target)+square(vertical));
      if (identity.type->category == TypeCategory::Building) for (const auto cell : footprint(*identity.type, transform.cell))
        distance = std::min(distance, integerSqrt(distanceSquared(position(cell), shot.view.target)+square(vertical)));
      if (distance > std::max(100, warhead.spreadMilliCells)) continue;
      const int attenuation = warhead.spreadMilliCells > 0 ?
        100 - static_cast<int>(std::min<std::int64_t>(distance, warhead.spreadMilliCells) * (100 - warhead.percentAtMax) / warhead.spreadMilliCells) : 100;
      const auto damage = static_cast<std::int64_t>(shot.weapon->damage) * warhead.versesPercent[identity.type->armor] * attenuation / 10000;
      auto& health = registry.get<Health>(entity);
      health.hp = static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(health.hp) - damage, 0, identity.type->strength));
    }
    if (!warhead.animations.empty()) {
      // Vanilla AnimList selects by damage in 25-damage bands, clamped to the
      // available list. This is not a random small/large explosion toggle.
      const auto index=std::min<std::size_t>(static_cast<std::size_t>(shot.weapon->damage/25),warhead.animations.size()-1);
      effect(shot.location,true,warhead.animations[index]);
    }
  }
  void projectilesStep() {
    std::vector<Shot> flying;
    for (auto shot:projectiles) {
      if (shot.view.started==now) { flying.push_back(std::move(shot)); continue; }
      const auto& type=definitions.projectile(shot.weapon->projectile);
      const auto before=shot.location;
      auto after=before+shot.velocity;
      shot.view.previous=planar(before); shot.view.previousHeightLeptons=static_cast<int>(before.z/motion::lepton);
      bool detonated=false;
      if (type.invisible) {
        if (const auto it=entities.find(shot.targetEntity);it!=entities.end()) shot.view.target=registry.get<Transform>(it->second).current;
        after=fixed(shot.view.target,grid.heightAt(shot.view.target.x,shot.view.target.y)); detonated=true;
      } else {
        // Swept segment samples at <=1/32 cell prevent a fast projectile from
        // tunnelling through a one-cell wall or cliff between logic frames.
        const int samples=std::max(1,static_cast<int>((motion::length(after-before)+31999)/32000));
        for (int i=1;i<=samples;++i) {
          const motion::Vector p{before.x+(after.x-before.x)*i/samples,before.y+(after.y-before.y)*i/samples,before.z+(after.z-before.z)*i/samples};
          const auto xy=planar(p); const auto cell=cellAt(xy); const auto& surface=grid.cell(cell).surface;
          const auto ground=grid.heightAt(xy.x,xy.y)*motion::lepton;
          const bool wall=type.subjectToWalls && surface.wallHeightLeptons>0 && p.z<=ground+surface.wallHeightLeptons*motion::lepton;
          const bool cliff=type.subjectToCliffs && surface.cliff && p.z<=ground;
          const bool floor=p.z<=ground && (type.subjectToElevation || surface.heightLeptons==0);
          if (wall || cliff || floor) {
            after=p;
            // Remove sub-step rounding at the intended ground impact, without
            // snapping a shell through obstacles or onto a moving target.
            if (!wall && !cliff && distanceSquared(xy,shot.view.target)<=square(32))
              after=fixed(shot.view.target,grid.heightAt(shot.view.target.x,shot.view.target.y));
            detonated=true; break;
          }
        }
        shot.velocity.z-=definitions.profile().gravity*motion::lepton;
      }
      shot.location=after; shot.view.position=planar(after); shot.view.heightLeptons=static_cast<int>(after.z/motion::lepton);
      if (now-shot.view.started>4096) detonated=true;
      if (detonated) { shot.view.target=planar(after); impact(shot); }
      else flying.push_back(std::move(shot));
    }
    projectiles=std::move(flying);
  }
  void transitions() {
    // Stable IDs define mutation order. EnTT storage order never defines game rules.
    for (const auto& [id, entity] : entities) {
      auto* deploy = registry.try_get<Deploy>(entity);
      if (!deploy) continue;
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
      if (!factory) continue;
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
      if (!exit || !registry.get<Mobile>(entity).path.empty()) continue;
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
  void beginFactoryExit(entt::entity entity) {
    const auto factoryId = registry.get<Identity>(entity).id;
    const auto owner = registry.get<Identity>(entity).owner;
    const auto* factoryType = registry.get<Identity>(entity).type;
    const auto& definition = *factoryType->factoryExit;
    const auto anchor = registry.get<Transform>(entity).cell;
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
      if (!factory || factory->queue.empty() || !complete(entity) ||
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
    assignFormations();
    transitions();
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
    for (const auto& [id, entity] : entities) { (void)id; weapons(entity); }
    projectilesStep();
    for (auto it = entities.begin(); it != entities.end();) {
      if (registry.get<Health>(it->second).hp <= 0) {
        if (const auto* exit = registry.try_get<FactoryExit>(it->second)) closeExit(exit->factory, it->first);
        harvesting.remove(it->first); release(it->first); registry.destroy(it->second); it = entities.erase(it);
      }
      else ++it;
    }
    production();
    std::erase_if(effects, [&](const auto& effect) { return now-effect.started>effect.duration; });
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
    }
  }
}
GameSpeed World::gameSpeed() const { return impl_->clock.speed(); }
std::uint64_t World::pendingFrames() const { return impl_->clock.pendingFrames(); }
Tick World::tick() const { return impl_->now; }
double World::interpolation() const { return impl_->presentCurrentFrame ? 1.0 : impl_->clock.interpolation(); }
const MapGrid& World::map() const { return impl_->grid; }
EntityId World::occupant(TileCoord cell) const { const auto it = impl_->occupied.find(key(cell)); return it == impl_->occupied.end() ? structureAt(cell) : it->second; }
EntityId World::structureAt(TileCoord cell) const { const auto it = impl_->structures.find(key(cell)); return it == impl_->structures.end() ? 0 : it->second; }
void World::setBuildable(TileCoord cell, bool value) {
  if (!validCell(cell) || impl_->structures.contains(key(cell)) || impl_->occupied.contains(key(cell)) || impl_->reservations.contains(key(cell))) throw std::invalid_argument("Cannot edit occupied terrain");
  impl_->grid.setBuildable(cell, value);
  if (value) impl_->blockedTerrain.erase(key(cell)); else impl_->blockedTerrain.insert(key(cell));
}
void World::setTerrain(TileCoord cell, TerrainSurface surface) {
  if (!validCell(cell) || impl_->structures.contains(key(cell)) || impl_->occupied.contains(key(cell)) || impl_->reservations.contains(key(cell)))
    throw std::invalid_argument("Cannot edit occupied terrain");
  if (surface.heightLeptons<0 || surface.heightLeptons>16384 || std::abs(surface.slopeX)>256 ||
      std::abs(surface.slopeY)>256 || surface.wallHeightLeptons<0 || surface.wallHeightLeptons>4096)
    throw std::invalid_argument("Terrain height/slope outside supported range");
  surface.land=canonicalId(surface.land);
  impl_->grid.setSurface(cell,surface); impl_->terrain[key(cell)]=std::move(surface); impl_->refresh(cell);
}
int World::credits(std::uint32_t player) const { const auto it = impl_->money.find(player); return it == impl_->money.end() ? impl_->definitions.profile().startingCredits : it->second; }
void World::setResource(TileCoord cell, std::string_view type, int amount) { impl_->harvesting.setResource(cell,type,amount); }
std::optional<ResourceCell> World::resourceAt(TileCoord cell) const { return impl_->harvesting.resourceAt(cell); }
std::vector<ResourceCell> World::resources() const { return impl_->harvesting.resources(); }
const std::string& World::lastMessage() const { return impl_->message; }
const std::vector<EffectSnapshot>& World::effects() const { return impl_->effects; }
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
    view.previous = transform.previous; view.position = transform.current; view.cell = transform.cell;
    view.previousHeading = transform.previousHeading; view.previousTurretHeading = transform.previousTurret;
    view.heading = transform.heading; view.turretHeading = transform.turret;
    view.heightLeptons=impl_->grid.heightAt(transform.current.x,transform.current.y);
    view.previousHeightLeptons=impl_->grid.heightAt(transform.previous.x,transform.previous.y);
    const auto& surface=impl_->grid.cell(cellAt(transform.current)).surface;
    if (identity.type->isTilter) { view.slopeX=surface.slopeX; view.slopeY=surface.slopeY; }
    view.animationSpeed = transform.animationSpeed;
    view.hp = impl_->registry.get<Health>(entity).hp; view.maxHp = identity.type->strength;
    view.harvesting=impl_->harvesting.snapshot(id);
    if (const auto* mobile = impl_->registry.try_get<Mobile>(entity)) {
      view.destination = mobile->destination;
      view.awaitingFormation=mobile->pendingGroup!=0;
      if (!mobile->path.empty() || mobile->requested || mobile->pendingGroup || mobile->yielding) view.activity = Activity::Moving;
    }
    if (const auto* building = impl_->registry.try_get<Structure>(entity)) {
      view.stateStarted = building->started; view.stateDuration = building->complete - building->started;
      if (building->complete > impl_->now) view.activity = Activity::Constructing;
    }
    if (const auto* deploy = impl_->registry.try_get<Deploy>(entity)) {
      view.activity = Activity::Transforming; view.stateStarted = deploy->started; view.stateDuration = deploy->complete - deploy->started;
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
  add(impl_->harvesting.stateHash());
  for (const auto& view : snapshot()) {
    add(view.id); text(view.type); add(view.owner); add(view.position.x); add(view.position.y); add(view.cell.x); add(view.cell.y); add(view.hp); add(view.heading); add(view.turretHeading);
    add(static_cast<int>(view.activity)); add(view.stateStarted); add(view.stateDuration);
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
    if (const auto* arm = impl_->registry.try_get<Armament>(entity)) { add(arm->active); add(arm->automatic); add(arm->target); add(arm->ground.x); add(arm->ground.y); add(arm->nextShot); }
    if (const auto* d = impl_->registry.try_get<Deploy>(entity)) { text(d->target->id); add(d->cell.x); add(d->cell.y); }
    if (const auto* f = impl_->registry.try_get<Factory>(entity)) {
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
  for (const auto& shot : impl_->projectiles) {
    add(shot.view.id); add(shot.view.started); add(shot.view.impact); add(shot.view.target.x); add(shot.view.target.y); text(shot.weapon->id);
    add(shot.location.x); add(shot.location.y); add(shot.location.z);
    add(shot.velocity.x); add(shot.velocity.y); add(shot.velocity.z); add(shot.source); add(shot.owner); add(shot.targetEntity);
  }
  for (const auto& command : impl_->commands) { add(command.tick); add(command.player); add(command.sequence); add(static_cast<int>(command.kind)); add(command.entity); add(command.target); add(command.cell.x); add(command.cell.y); text(command.type); add(command.members.size()); for(const auto id:command.members) add(id); add(command.exactDestination); }
  return hash;
}
MovementDiagnostics World::movementDiagnostics() const { return impl_->movementStats; }
}
