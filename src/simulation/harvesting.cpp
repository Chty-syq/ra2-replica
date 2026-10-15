#include "harvesting.h"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace ra2 {
namespace {
auto key(TileCoord c) { return std::pair{c.x,c.y}; }
std::int64_t distance(TileCoord a, TileCoord b) {
  const auto x=static_cast<std::int64_t>(a.x)-b.x, y=static_cast<std::int64_t>(a.y)-b.y;
  return x*x+y*y;
}
constexpr std::array<TileCoord,8> neighbors{{{1,0},{0,1},{-1,0},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}}};
}
const char* harvestPhaseName(HarvestPhase phase) {
  switch (phase) {
    case HarvestPhase::Stopped: return "已停止";
    case HarvestPhase::SeekingOre: return "寻找矿区";
    case HarvestPhase::MovingToOre: return "前往矿区";
    case HarvestPhase::Harvesting: return "正在采矿";
    case HarvestPhase::SeekingRefinery: return "寻找可用矿场";
    case HarvestPhase::WaitingForDock: return "等待卸货位置";
    case HarvestPhase::MovingToRefinery: return "返回矿场";
    case HarvestPhase::Teleporting: return "超时空返回";
    case HarvestPhase::Unloading: return "正在卸货";
    case HarvestPhase::LeavingDock: return "驶离矿场";
  }
  return "Unknown";
}
void HarvestSystem::add(MiningEntity id, const UnitTypeDef& type) {
  remove(id);
  if (type.harvester) { Miner miner{}; miner.definition=&*type.harvester; miners_.emplace(id,std::move(miner)); }
  if (type.refinery) refineries_.emplace(id,Refinery{&*type.refinery,true,{},std::vector<MiningEntity>(type.refinery->docks.size())});
}
void HarvestSystem::release(MiningEntity id, Miner& miner) {
  if (miner.ore) { const auto it=claims_.find(key(*miner.ore)); if (it!=claims_.end() && it->second==id) claims_.erase(it); }
  if (const auto it=refineries_.find(miner.refinery); it!=refineries_.end()) {
    std::erase(it->second.queue,id);
    for (auto& slot:it->second.slots) if (slot==id) slot=0;
  }
  miner.ore.reset(); miner.refinery=0;
}
void HarvestSystem::remove(MiningEntity id) {
  if (const auto it=miners_.find(id); it!=miners_.end()) { release(id,it->second); miners_.erase(it); }
  refineries_.erase(id); // Miners detect the missing refinery on their next frame.
}
void HarvestSystem::stop(MiningEntity id) {
  if (const auto it=miners_.find(id); it!=miners_.end()) { release(id,it->second); it->second.phase=HarvestPhase::Stopped; }
}
bool HarvestSystem::harvest(MiningEntity id, TileCoord near) {
  const auto it=miners_.find(id); if (it==miners_.end()) return false;
  auto& miner=it->second; release(id,miner);
  miner.phase=HarvestPhase::SeekingOre; miner.preferred=near; miner.timer=0;
  miner.failedOre.clear(); miner.failedRefinery.clear();
  return true;
}
bool HarvestSystem::compatible(const HarvestActor& miner, MiningEntity refinery, const HarvestHost& host) const {
  const auto target=host.miningActor(refinery);
  return miner.type->harvester && refineries_.contains(refinery) && target && target->ready && target->owner==miner.owner &&
    std::find(miner.type->harvester->docks.begin(),miner.type->harvester->docks.end(),target->type->id)!=miner.type->harvester->docks.end();
}
bool HarvestSystem::returnTo(MiningEntity id, MiningEntity refinery, const HarvestHost& host) {
  const auto it=miners_.find(id); const auto actor=host.miningActor(id);
  if (it==miners_.end() || !actor || !compatible(*actor,refinery,host)) return false;
  auto& miner=it->second; release(id,miner); miner.refinery=refinery;
  refineries_.at(refinery).queue.push_back(id); miner.phase=HarvestPhase::WaitingForDock; miner.timer=0;
  return true;
}
int HarvestSystem::load(const Miner& miner) const { int result=0; for (const auto& [type,amount]:miner.cargo) { (void)type; result+=amount; } return result; }
void HarvestSystem::setResource(TileCoord cell, std::string_view type, int amount) {
  if (cell.x < -100000 || cell.x > 100000 || cell.y < -100000 || cell.y > 100000 || amount<0 || amount>1000000)
    throw std::invalid_argument("Resource cell or amount outside supported range");
  const auto& definition=definitions_.resource(type);
  if (amount==0) resources_.erase(key(cell)); else resources_[key(cell)]={cell,definition.id,amount};
}
std::optional<ResourceCell> HarvestSystem::resourceAt(TileCoord cell) const {
  const auto it=resources_.find(key(cell)); return it==resources_.end()?std::nullopt:std::optional{it->second};
}
std::vector<ResourceCell> HarvestSystem::resources() const {
  std::vector<ResourceCell> result; for (const auto& [cell,resource]:resources_) { (void)cell; result.push_back(resource); } return result;
}
std::optional<HarvestSnapshot> HarvestSystem::snapshot(MiningEntity id) const {
  const auto it=miners_.find(id); if (it==miners_.end()) return std::nullopt;
  const auto& miner=it->second; int value=0;
  for (const auto& [type,amount]:miner.cargo) value+=definitions_.resource(type).value*amount;
  return HarvestSnapshot{miner.phase,load(miner),miner.definition->capacity,value,miner.refinery,
                         miner.phase==HarvestPhase::Harvesting ? miner.timer-miner.definition->harvestFrames : 0};
}
void HarvestSystem::step(GameFrame now, HarvestHost& host, std::size_t& budget) {
  // Snapshot IDs because FreeUnit inserts miners; no reference into an ECS pool
  // survives the spawn. A blocked exit is retried, never paid out twice.
  for (auto& [id,refinery]:refineries_) {
    const auto actor=host.miningActor(id);
    if (!actor || !actor->ready || !refinery.freeUnitPending) continue;
    if (refinery.definition->freeUnit.empty()) { refinery.freeUnitPending=false; continue; }
    for (int radius=1;radius<=4 && refinery.freeUnitPending;++radius) for (auto offset:neighbors) {
      const auto dock=refinery.definition->docks.front();
      const TileCoord cell{actor->cell.x+dock.x+offset.x*radius,actor->cell.y+dock.y+offset.y*radius};
      bool isDock=false;
      for (const auto& [otherId,other]:refineries_) if (const auto building=host.miningActor(otherId))
        for (auto d:other.definition->docks) if (cell==TileCoord{building->cell.x+d.x,building->cell.y+d.y}) isDock=true;
      if (!isDock && host.miningFree(cell,0) && host.miningSpawn(refinery.definition->freeUnit,cell,actor->owner)) {
        refinery.freeUnitPending=false; break;
      }
    }
  }
  for (auto& [id,miner]:miners_) {
    const auto actor=host.miningActor(id);
    if (!actor || !actor->ready || miner.phase==HarvestPhase::Stopped) continue;
    std::erase_if(miner.failedOre,[&](const auto& item){return item.second<=now;});
    std::erase_if(miner.failedRefinery,[&](const auto& item){return item.second<=now;});
    if (miner.refinery && !compatible(*actor,miner.refinery,host)) {
      release(id,miner); host.miningHalt(id);
      miner.phase=load(miner)>0?HarvestPhase::SeekingRefinery:HarvestPhase::SeekingOre; miner.timer=0;
    }
    if (actor->cell!=miner.lastCell) { miner.lastCell=actor->cell; miner.progressAt=now; }
    if (now<miner.timer) continue;
    const auto retry=[&] { miner.timer=now+30; };
    const auto leaveOre=[&] { release(id,miner); miner.phase=load(miner)>=miner.definition->capacity?HarvestPhase::SeekingRefinery:HarvestPhase::SeekingOre; miner.timer=0; };
    const auto failRefinery=[&] {
      miner.failedRefinery[miner.refinery]=now+180;
      release(id,miner); host.miningHalt(id); miner.phase=HarvestPhase::SeekingRefinery; miner.timer=0;
    };
    switch (miner.phase) {
      case HarvestPhase::SeekingOre: {
        if (actor->moving) break;
        if (load(miner)>=miner.definition->capacity) { miner.phase=HarvestPhase::SeekingRefinery; break; }
        std::vector<std::pair<std::int64_t,CellKey>> candidates;
        for (const auto& [cell,resource]:resources_) if (!claims_.contains(cell) && !miner.failedOre.contains(cell) && host.miningFree(resource.cell,id))
          candidates.emplace_back(distance(miner.preferred.value_or(actor->cell),resource.cell),cell);
        std::sort(candidates.begin(),candidates.end());
        bool deferred=false;
        for (const auto& [rank,cell]:candidates) {
          (void)rank; const TileCoord target{cell.first,cell.second};
          const auto moved=host.miningMove(id,target,budget);
          if (moved==MiningMove::Deferred) { deferred=true; break; }
          if (moved==MiningMove::Blocked) { miner.failedOre[cell]=now+180; continue; }
          miner.ore=target; miner.preferred=target; claims_[cell]=id;
          miner.phase=HarvestPhase::MovingToOre; miner.progressAt=now; break;
        }
        if (miner.phase==HarvestPhase::SeekingOre && !deferred) {
          if (load(miner)>0) miner.phase=HarvestPhase::SeekingRefinery; else retry();
        }
        break;
      }
      case HarvestPhase::MovingToOre:
        if (!miner.ore || !resources_.contains(key(*miner.ore))) { host.miningHalt(id); leaveOre(); }
        else if (!actor->moving && actor->cell==*miner.ore) { miner.phase=HarvestPhase::Harvesting; miner.timer=now+miner.definition->harvestFrames; }
        else if (now-miner.progressAt>180) { miner.failedOre[key(*miner.ore)]=now+180; host.miningHalt(id); leaveOre(); }
        break;
      case HarvestPhase::Harvesting: {
        const auto found=miner.ore?resources_.find(key(*miner.ore)):resources_.end();
        if (found==resources_.end() || actor->moving || actor->cell!=*miner.ore) { leaveOre(); break; }
        auto& resource=found->second;
        ++miner.cargo[resource.type]; --resource.amount;
        const bool depleted=resource.amount==0;
        if (depleted) resources_.erase(found);
        if (depleted || load(miner)>=miner.definition->capacity) leaveOre();
        else miner.timer=now+miner.definition->harvestFrames;
        break;
      }
      case HarvestPhase::SeekingRefinery: {
        if (actor->moving) break;
        std::vector<std::pair<std::int64_t,MiningEntity>> candidates;
        for (const auto& [target,refinery]:refineries_) if (compatible(*actor,target,host) && !miner.failedRefinery.contains(target)) {
          const auto building=host.miningActor(target);
          // Queue cost avoids crowding one refinery when another is nearby.
          candidates.emplace_back(distance(actor->cell,building->cell)+static_cast<std::int64_t>(refinery.queue.size())*16,target);
        }
        std::sort(candidates.begin(),candidates.end());
        if (candidates.empty()) { retry(); break; }
        miner.refinery=candidates.front().second; refineries_.at(miner.refinery).queue.push_back(id);
        miner.phase=HarvestPhase::WaitingForDock; miner.progressAt=now;
        break;
      }
      case HarvestPhase::WaitingForDock: {
        if (actor->moving) break;
        auto& refinery=refineries_.at(miner.refinery);
        if (refinery.queue.empty() || refinery.queue.front()!=id) break;
        const auto building=host.miningActor(miner.refinery);
        bool assigned=false, hasVacant=false, deferred=false;
        for (std::size_t i=0;i<refinery.slots.size();++i) if (!refinery.slots[i]) {
          hasVacant=true;
          const auto offset=refinery.definition->docks[i];
          const TileCoord target{building->cell.x+offset.x,building->cell.y+offset.y};
          if (!host.miningFree(target,id)) continue;
          const auto moved=miner.definition->teleportReturn?MiningMove::Started:host.miningMove(id,target,budget);
          if (moved==MiningMove::Deferred) { deferred=true; break; }
          if (moved==MiningMove::Blocked) continue;
          refinery.slots[i]=id; refinery.queue.pop_front(); miner.dock=target; miner.progressAt=now;
          miner.phase=miner.definition->teleportReturn?HarvestPhase::Teleporting:HarvestPhase::MovingToRefinery;
          miner.timer=now+(miner.definition->teleportReturn?miner.definition->chronoReturnFrames:0); assigned=true; break;
        }
        // Never time out solely because another miner owns the dock.
        if (!assigned && hasVacant && !deferred && now-miner.progressAt>180) failRefinery();
        break;
      }
      case HarvestPhase::Teleporting:
        if (!host.miningTeleport(id,miner.dock)) { failRefinery(); break; }
        miner.phase=HarvestPhase::Unloading; miner.timer=now+miner.definition->unloadFrames;
        break;
      case HarvestPhase::MovingToRefinery:
        if (!actor->moving && actor->cell==miner.dock) { miner.phase=HarvestPhase::Unloading; miner.timer=now+miner.definition->unloadFrames; }
        else if (now-miner.progressAt>180) failRefinery();
        break;
      case HarvestPhase::Unloading:
        if (actor->cell!=miner.dock || actor->moving) { failRefinery(); break; }
        if (!miner.cargo.empty()) {
          auto item=miner.cargo.begin();
          if (!host.miningCredit(actor->owner,definitions_.resource(item->first).value)) { retry(); break; }
          if (--item->second==0) miner.cargo.erase(item);
          miner.timer=now+miner.definition->unloadFrames;
        }
        if (miner.cargo.empty()) { miner.phase=HarvestPhase::LeavingDock; miner.timer=0; miner.progressAt=now; }
        break;
      case HarvestPhase::LeavingDock:
        if (actor->moving) {
          if (now-miner.progressAt>180) { host.miningHalt(id); miner.progressAt=now; }
          break;
        }
        if (actor->cell!=miner.dock) { release(id,miner); miner.phase=HarvestPhase::SeekingOre; break; }
        for (auto offset:neighbors) {
          const TileCoord target{actor->cell.x+offset.x,actor->cell.y+offset.y};
          bool otherDock=false;
          const auto& refinery=refineries_.at(miner.refinery); const auto building=host.miningActor(miner.refinery);
          for (auto dock:refinery.definition->docks) if (target==TileCoord{building->cell.x+dock.x,building->cell.y+dock.y}) otherDock=true;
          if (!otherDock && host.miningFree(target,id) && host.miningMove(id,target,budget)==MiningMove::Started) break;
        }
        break;
      case HarvestPhase::Stopped: break;
    }
  }
}
std::uint64_t HarvestSystem::stateHash() const {
  std::uint64_t hash=14695981039346656037ULL;
  const auto add=[&](std::uint64_t value) { for (int i=0;i<8;++i) { hash^=(value>>(i*8))&255; hash*=1099511628211ULL; } };
  const auto text=[&](const std::string& value) { add(value.size()); for (unsigned char c:value) add(c); };
  const auto cell=[&](TileCoord c){add(c.x);add(c.y);};
  add(resources_.size()); for (const auto& [k,r]:resources_) { (void)k; cell(r.cell);text(r.type);add(r.amount); }
  add(claims_.size()); for (const auto& [k,id]:claims_) {add(k.first);add(k.second);add(id);}
  add(miners_.size());
  for (const auto& [id,m]:miners_) {
    add(id);add(static_cast<int>(m.phase));add(m.timer);add(m.progressAt);add(m.refinery);cell(m.dock);cell(m.lastCell);
    add(m.ore.has_value());if(m.ore)cell(*m.ore);add(m.preferred.has_value());if(m.preferred)cell(*m.preferred);
    add(m.cargo.size());for(const auto& [t,n]:m.cargo){text(t);add(n);}
    add(m.failedOre.size());for(const auto& [c,t]:m.failedOre){add(c.first);add(c.second);add(t);}
    add(m.failedRefinery.size());for(const auto& [r,t]:m.failedRefinery){add(r);add(t);}
  }
  add(refineries_.size());for(const auto& [id,r]:refineries_){add(id);add(r.freeUnitPending);add(r.queue.size());for(auto m:r.queue)add(m);add(r.slots.size());for(auto m:r.slots)add(m);}
  return hash;
}
}
