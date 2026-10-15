#pragma once
#include "config/definitions.h"
#include "map_grid.h"
#include "harvesting.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace ra2 {
using EntityId = std::uint64_t;
using Tick = GameFrame; // One World step is one compatibility game frame.
// 1 cell = 1000 integer subcells. Presentation alone converts to floating point.
struct Position { std::int64_t x = 0, y = 0; auto operator<=>(const Position&) const = default; };
enum class Activity { Idle, Moving, Constructing, Transforming, LeavingFactory };
enum class FactoryExitPhase { Idle, Blocked, Preparing, Driving, Clearing };
enum class CommandKind { Move, Stop, AttackGround, AttackEntity, Deploy, Produce, CancelProduction, PlaceBuilding, Harvest, ReturnToRefinery, GroupMove };
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
  GameSpeed animationSpeed{}, productionAnimationSpeed{}; // Presentation only; sampled when the animation starts.
  int heightLeptons = 0, previousHeightLeptons = 0, slopeX = 0, slopeY = 0;
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
  const std::vector<EffectSnapshot>& effects() const;
  const MapGrid& map() const;
  EntityId occupant(TileCoord cell) const;
  EntityId structureAt(TileCoord cell) const;
  void setBuildable(TileCoord cell, bool value);
  void setTerrain(TileCoord cell, TerrainSurface surface);
  void setResource(TileCoord cell, std::string_view type, int amount);
  std::optional<ResourceCell> resourceAt(TileCoord cell) const;
  std::vector<ResourceCell> resources() const;
  int credits(std::uint32_t player = 0) const;
  const std::string& lastMessage() const;
  std::uint64_t stateHash() const;
  MovementDiagnostics movementDiagnostics() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
