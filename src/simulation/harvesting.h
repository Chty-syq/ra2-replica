#pragma once
#include "config/definitions.h"
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <vector>

namespace ra2 {
using MiningEntity = std::uint64_t;
enum class HarvestPhase { Stopped, SeekingOre, MovingToOre, Harvesting, SeekingRefinery,
                          WaitingForDock, MovingToRefinery, Teleporting, Unloading, LeavingDock };
const char* harvestPhaseName(HarvestPhase phase);
struct ResourceCell { TileCoord cell{}; std::string type; int amount = 0; };
struct HarvestSnapshot {
  HarvestPhase phase = HarvestPhase::Stopped;
  int cargo = 0, capacity = 0, value = 0;
  MiningEntity refinery = 0;
  GameFrame cycleStarted = 0; // Read-only presentation timing for the current harvest stroke.
};
struct HarvestActor {
  const UnitTypeDef* type;
  std::uint32_t owner;
  TileCoord cell;
  bool ready, moving;
};
enum class MiningMove { Started, Blocked, Deferred };
// The economy owns no ECS references. World provides exact movement and occupancy
// so mining obeys the same collision, command ordering and factory exit rules.
class HarvestHost {
public:
  virtual ~HarvestHost() = default;
  virtual std::optional<HarvestActor> miningActor(MiningEntity id) const = 0;
  virtual MiningMove miningMove(MiningEntity id, TileCoord cell, std::size_t& budget) = 0;
  virtual bool miningFree(TileCoord cell, MiningEntity self) const = 0;
  virtual void miningHalt(MiningEntity id) = 0;
  virtual bool miningTeleport(MiningEntity id, TileCoord cell) = 0;
  virtual bool miningCredit(std::uint32_t owner, int amount) = 0;
  virtual MiningEntity miningSpawn(std::string_view type, TileCoord cell, std::uint32_t owner) = 0;
};
class HarvestSystem {
public:
  explicit HarvestSystem(const DefinitionRegistry& definitions) : definitions_(definitions) {}
  void add(MiningEntity id, const UnitTypeDef& type);
  void remove(MiningEntity id);
  void captured(MiningEntity id,HarvestHost& host);
  void stop(MiningEntity id);
  bool harvest(MiningEntity id, TileCoord near);
  bool returnTo(MiningEntity id, MiningEntity refinery, const HarvestHost& host);
  void step(GameFrame now, HarvestHost& host, std::size_t& pathBudget);
  void setResource(TileCoord cell, std::string_view type, int amount);
  std::optional<ResourceCell> resourceAt(TileCoord cell) const;
  std::vector<ResourceCell> resources() const;
  std::optional<HarvestSnapshot> snapshot(MiningEntity id) const;
  std::uint64_t stateHash() const;
private:
  using CellKey = std::pair<int,int>;
  struct Miner {
    const HarvesterDef* definition;
    HarvestPhase phase = HarvestPhase::SeekingOre;
    std::map<std::string,int> cargo;
    std::optional<TileCoord> ore, preferred;
    TileCoord dock{}, lastCell{};
    MiningEntity refinery = 0;
    GameFrame timer = 0, progressAt = 0;
    std::map<CellKey,GameFrame> failedOre;
    std::map<MiningEntity,GameFrame> failedRefinery;
  };
  struct Refinery {
    const RefineryDef* definition;
    bool freeUnitPending = true;
    std::deque<MiningEntity> queue;
    std::vector<MiningEntity> slots;
  };
  void release(MiningEntity id, Miner& miner);
  bool compatible(const HarvestActor& miner, MiningEntity refinery, const HarvestHost& host) const;
  int load(const Miner& miner) const;
  const DefinitionRegistry& definitions_;
  std::map<CellKey,ResourceCell> resources_;
  std::map<CellKey,MiningEntity> claims_;
  std::map<MiningEntity,Miner> miners_;
  std::map<MiningEntity,Refinery> refineries_;
};
}
