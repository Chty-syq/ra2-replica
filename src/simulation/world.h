#pragma once
#include "config/definitions.h"
#include "map_grid.h"
#include "harvesting.h"
#include "radiation.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace ra2 {
using EntityId = std::uint64_t;
using Tick = GameFrame; // One World step is one compatibility game frame.
// 1 cell = 1000 integer subcells. Presentation alone converts to floating point.
struct Position { std::int64_t x = 0, y = 0; auto operator<=>(const Position&) const = default; };
enum class Activity { Idle, Moving, Constructing, Transforming, LeavingFactory, Selling };
enum class FactoryExitPhase { Idle, Blocked, Preparing, Driving, Clearing };
enum class InfantryPosture { Standing, GoingProne, Prone, GettingUp, Deploying, Deployed, Undeploying };
enum class BuildingInteractionKind { Capture, Repair, Board, Garrison, Infiltrate };
struct BuildingInteractionSnapshot {
  EntityId target=0;
  BuildingInteractionKind kind=BuildingInteractionKind::Capture;
  bool entering=false;
};
struct InfantrySnapshot {
  int subCell=0;
  InfantryPosture posture=InfantryPosture::Standing;
  std::string sequence="READY";
  Tick animationStarted=0;
  bool loop=true;
};
enum class CommandKind { Move, Stop, AttackGround, AttackEntity, Deploy, Produce, CancelProduction, PlaceBuilding, Harvest, ReturnToRefinery, GroupMove, EnterBuilding, EnterTransport, Unload, ToggleRepair, SellBuilding };
struct CargoSnapshot {
  std::vector<EntityId> passengers;
  int used=0, capacity=0, mode=0, turret=0;
  bool unloading=false;
};
struct Command {
  Tick tick = 0;
  std::uint32_t player = 0;
  std::uint64_t sequence = 0;
  CommandKind kind = CommandKind::Stop;
  EntityId entity = 0, target = 0;
  TileCoord cell{};
  std::string type{};
  std::vector<EntityId> members{};
  bool exactDestination=false;
};
struct MovementDiagnostics {
  std::size_t pathExpanded=0, assignmentExpanded=0, scheduledSegments=0, waitingUnits=0, yields=0;
  std::size_t flowExpanded=0, flowBuilds=0, flowSteps=0, dockingSearches=0, forecastChecks=0, forecastHolds=0;
};
struct BombSnapshot {
  EntityId source=0;
  std::uint32_t owner=0;
  Tick planted=0,detonates=0;
  std::string sourceType;
};
enum class AircraftMission { Landed, Moving, Attacking, Returning, Landing, Holding };
struct AircraftSnapshot {AircraftMission mission=AircraftMission::Landed;EntityId airport=0;int slot=-1,ammo=-1;Tick reloadRemaining=0;};
struct AircraftWreck {std::string type;std::uint32_t owner=0;Position position;int heading=0,height=0,previousHeight=0,speed=1;};
enum class NavalSpawnPhase { Docked, Attacking, Returning, Reloading, Regenerating };
struct NavalSpawnSlot {EntityId unit=0;Tick ready=0;NavalSpawnPhase phase=NavalSpawnPhase::Docked;};
struct EntitySnapshot {
  EntityId id = 0;
  std::uint32_t owner = 0;
  std::string type;
  Position previous, position;
  TileCoord cell{}, destination{};
  int heading = 0, turretHeading = 0, previousHeading = 0, previousTurretHeading = 0, hp = 0, maxHp = 0;
  Activity activity = Activity::Idle;
  Tick stateStarted = 0, stateDuration = 0;
  std::string productionType;
  int productionRemaining = 0, productionTotal = 0;
  std::size_t queueSize = 0;
  FactoryExitPhase factoryPhase = FactoryExitPhase::Idle;
  EntityId exitingUnit = 0, exitFactory = 0;
  Tick factoryPhaseStarted = 0, factoryCycleStarted = 0;
  bool hasDeferredCommand = false;
  bool awaitingFormation = false;
  std::optional<HarvestSnapshot> harvesting;
  std::optional<InfantrySnapshot> infantry;
  std::optional<BuildingInteractionSnapshot> buildingInteraction;
  std::optional<CargoSnapshot> cargo;
  std::optional<AircraftSnapshot> aircraft;
  std::optional<BombSnapshot> bomb;
  Tick c4Detonates=0,chronoReady=0,frozenSince=0;
  bool submerged=false;int cloakStage=0;Tick surfacedUntil=0;
  EntityId repairDock=0;
  bool grappled=false,spawnedAircraft=false;
  EntityId spawnParent=0;
  std::vector<NavalSpawnSlot> spawns;
  bool frozen=false,veteran=false;
  int warpRemaining=0;
  std::string spyDisguise;
  std::uint32_t disguiseOwner=0;
  EntityId containedBy=0;
  std::string activeWeapon;
  GameSpeed animationSpeed{}, productionAnimationSpeed{}; // Presentation only; sampled when the animation starts.
  int heightLeptons = 0, previousHeightLeptons = 0, slopeX = 0, slopeY = 0;
  bool airborne=false, takingOff=false;
  bool missile=false,launcherEmpty=false;
  bool leaping=false;
  EntityId parasiteHost=0,infestedBy=0;
  std::string spriteSequence="READY";
  Tick spriteStarted=0;
  int missilePitch=0,previousMissilePitch=0;
  Tick launcherReady=0;
  bool disguised=false;
  // Keep visual fire reveal separate from logical infantry detection. Neither
  // the viewer nor the renderer owns or advances these game-frame clocks.
  Tick disguiseStarted=0, disguiseFireUntil=0, disguiseExposedUntil=0;
  std::string disguise;
  int chargeTurret=0;
  int wallConnections=0;
  Tick firingStarted=0;
  bool powered=true;
  int coilChargers=0;
  bool overpowered=false;
  bool repairing=false, repairPaused=false;
  Tick repairStarted=0;
};
struct PowerSnapshot { int produced=0,consumed=0;Tick outageUntil=0;bool online() const{return !outageUntil && produced>=consumed;} };
struct EspionageSnapshot { bool veteranInfantry=false,veteranVehicles=false,alliedTech=false,sovietTech=false; };
struct BeamSnapshot {
  Position start,end;
  int startHeight=0,endHeight=0;
  Tick started=0,duration=15;
  std::uint32_t owner=0;
  std::string weapon;
};
struct InfantryCorpse {
  std::string type, sequence;
  std::uint32_t owner=0;
  Position position;
  int heading=0, heightLeptons=0;
  Tick started=0, duration=0;
  bool falling=false;
  int previousHeightLeptons=0, crashSpeed=0;
};
struct ShotSnapshot {
  std::uint64_t id = 0;
  Position start, target;
  Tick started = 0, impact = 0;
  std::string sourceType{};
  int facing = 0;
  bool invisible = false;
  Position previous, position;
  int previousHeightLeptons = 0, heightLeptons = 0;
  std::string weaponId, image;
  int targetHeightLeptons=0;
};
struct EffectSnapshot {
  Position position; Tick started = 0; bool explosion = false; std::string sourceType{}; int facing = 0;
  int heightLeptons = 0;
  std::string animation;
  GameSpeed animationSpeed{};
  Tick duration = 256;
};

// Owns the only gameplay state. Snapshots contain values, never writable component
// references. Registry must outlive World; it is immutable after scene creation.
class World {
public:
  explicit World(const DefinitionRegistry& definitions);
  const std::vector<AircraftWreck>& aircraftWrecks() const;
  ~World();
  World(const World&) = delete;
  World& operator=(const World&) = delete;
  EntityId spawn(std::string_view type, TileCoord cell, std::uint32_t owner = 0, bool constructing = false);
  bool enqueue(Command command);
  void step();
  void advance(std::uint64_t elapsedMicroseconds, std::uint32_t frameBudget = 120);
  void setGameSpeed(GameSpeed speed);
  GameSpeed gameSpeed() const;
  std::uint64_t pendingFrames() const;
  Tick tick() const;
  double interpolation() const;
  std::vector<EntitySnapshot> snapshot() const;
  std::vector<ShotSnapshot> shots() const;
  const std::vector<BeamSnapshot>& beams() const;
  const std::vector<radiation::Site>& radiationSites() const;
  int radiationLevel(TileCoord) const;
  int radiationLight(TileCoord) const;
  bool recognizes(EntityId target,std::uint32_t player=0) const;
  const std::vector<EffectSnapshot>& effects() const;
  const std::vector<InfantryCorpse>& infantryCorpses() const;
  const MapGrid& map() const;
  EntityId occupant(TileCoord cell) const;
  std::vector<EntityId> occupants(TileCoord cell) const;
  std::vector<EntityId> airOccupants(TileCoord cell) const;
  EntityId structureAt(TileCoord cell) const;
  void setBuildable(TileCoord cell, bool value);
  void setTerrain(TileCoord cell, TerrainSurface surface);
  void setResource(TileCoord cell, std::string_view type, int amount);
  std::optional<ResourceCell> resourceAt(TileCoord cell) const;
  std::vector<ResourceCell> resources() const;
  int credits(std::uint32_t player = 0) const;
  PowerSnapshot power(std::uint32_t player=0) const;
  EspionageSnapshot espionage(std::uint32_t player=0) const;
  bool canPlaceBuilding(std::string_view type,TileCoord cell) const;
  bool canRepair(EntityId id,std::uint32_t player=0) const;
  bool canSell(EntityId id,std::uint32_t player=0) const;
  int repairCost(EntityId id) const;
  int saleRefund(EntityId id) const;
  const std::string& lastMessage() const;
  std::uint64_t stateHash() const;
  MovementDiagnostics movementDiagnostics() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
