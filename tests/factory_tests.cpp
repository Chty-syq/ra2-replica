#include "simulation/world.h"
#include "foundation.h"
#include "test_support.h"
#include "simulation_fixture.h"
#include <algorithm>
#include <iostream>
#include <set>

using namespace ra2;
namespace {
EntitySnapshot view(const World& world, EntityId id) {
  const auto entities = world.snapshot();
  const auto found = std::find_if(entities.begin(), entities.end(), [id](const auto& v) { return v.id == id; });
  require(found != entities.end(), "Expected factory/vehicle disappeared");
  return *found;
}
bool exists(const World& world, EntityId id) {
  const auto entities = world.snapshot();
  return std::any_of(entities.begin(), entities.end(), [id](const auto& v) { return v.id == id; });
}
void steps(World& world, int count) { while (count-- > 0) world.step(); }
template<class Predicate> void until(World& world, Predicate predicate, const char* error, int budget = 1200) {
  for (int i = 0; i < budget && !predicate(); ++i) world.step();
  require(predicate(), error);
}
DefinitionRegistry altered(const DefinitionRegistry& defs, std::string profile, std::string rules = {}) {
  auto replica = defs.replica(), game = defs.rules();
  replica.overlay(IniDocument::parse(profile, "factory-test.ini"));
  game.overlay(IniDocument::parse(rules, "factory-rules.ini"));
  return DefinitionRegistry::compile(game, defs.art(), replica);
}
EntityId produce(World& world, EntityId factory, const char* type = "MTNK") {
  require(world.enqueue({world.tick(),0,1,CommandKind::Produce,factory,0,{},type}), "Production command rejected");
  until(world, [&] { return view(world,factory).exitingUnit != 0; }, "Factory never began an exit");
  return view(world,factory).exitingUnit;
}
void footprintIntact(const World& world, EntityId factory, TileCoord anchor = {-2,-1}) {
  for (const auto cell : foundationTiles({anchor,5,3})) {
    require(world.structureAt(cell) == factory && world.map().cell(cell).occupied, "Vehicle cleared the building footprint");
  }
}
void phasesAndReservations(const DefinitionRegistry& defs) {
  World world(defs);
  const auto factory = world.spawn("GAWEAP", {-2,-1});
  const auto child = produce(world,factory);
  require(view(world,factory).factoryPhase == FactoryExitPhase::Preparing && view(world,child).position == Position{}, "Vehicle did not spawn inside the factory");
  require(view(world,child).activity == Activity::LeavingFactory && view(world,child).exitFactory == factory, "Exit association missing");
  require(world.occupant({0,0}) == child && world.structureAt({0,0}) == factory, "Building/mobile ownership not independent");
  require(world.spawn("HTNK", {3,0}) == 0, "Another spawn stole the reserved exit");
  require(world.spawn("GAPOWR", {3,0}) == 0, "Placement ignored the exit reservation");
  requireRejected<std::invalid_argument>([&] { world.setBuildable({3,0},false); }, "Terrain editing erased an exit reservation");
  const auto intruder = world.spawn("HTNK", {3,3});
  world.enqueue({world.tick(),0,2,CommandKind::Move,intruder,0,{0,0}});
  steps(world,defs.unit("GAWEAP").factoryExit->prepareFrames - 1);
  require(view(world,child).position == Position{}, "Vehicle moved before preparation finished");
  std::set<FactoryExitPhase> seen{FactoryExitPhase::Preparing};
  for (int i = 0; i < 300; ++i) {
    world.step();
    footprintIntact(world,factory);
    seen.insert(view(world,factory).factoryPhase);
    require(world.structureAt(view(world,intruder).cell) == 0, "Unrelated vehicle entered the factory");
  }
  require(seen.contains(FactoryExitPhase::Driving) && seen.contains(FactoryExitPhase::Clearing) && seen.contains(FactoryExitPhase::Idle), "Exit phase did not advance/settle");
  require(view(world,child).cell == TileCoord{3,0} && view(world,child).exitFactory == 0, "Vehicle did not leave at configured endpoint");
  require(view(world,factory).exitingUnit == 0 && view(world,factory).queueSize == 0, "Exit retained a stale vehicle or queue item");
  world.enqueue({world.tick(),0,3,CommandKind::Move,child,0,{8,0}});
  until(world,[&] { return view(world,child).cell == TileCoord{8,0}; },"Vehicle could not leave exit reservation");
  require(world.spawn("HTNK", {3,0}) != 0, "Completed route leaked reservations");
}
void deferredCommands(const DefinitionRegistry& defs) {
  World world(defs);
  const auto factory = world.spawn("GAWEAP", {-2,-1}), child = produce(world,factory);
  world.enqueue({world.tick(),0,2,CommandKind::Move,child,0,{8,0}});
  world.step();
  require(view(world,child).hasDeferredCommand, "Move during exit was discarded");
  world.enqueue({world.tick(),0,3,CommandKind::Stop,child}); world.step();
  require(!view(world,child).hasDeferredCommand, "Stop did not clear deferred intent");
  until(world,[&] { return view(world,child).exitFactory == 0; },"Stop stranded vehicle inside factory");
  require(view(world,child).cell == TileCoord{3,0}, "Stop interrupted mandatory exit");

  World attack(defs);
  const auto f = attack.spawn("GAWEAP", {-2,-1}), tank = produce(attack,f);
  const auto enemy = attack.spawn("HTNK", {5,3},1);
  attack.enqueue({attack.tick(),0,2,CommandKind::Move,tank,0,{10,0}});
  attack.enqueue({attack.tick(),0,3,CommandKind::AttackEntity,tank,enemy,{5,3}});
  while (view(attack,tank).exitFactory) {
    require(attack.shots().empty(), "Vehicle fired before clearing its factory");
    attack.step();
    require(attack.tick() < 400, "Deferred attack stranded exiting vehicle");
  }
  until(attack,[&] { return view(attack,enemy).hp < 400; },"Deferred attack was not executed");
  require(view(attack,tank).cell == TileCoord{3,0}, "Earlier move replaced the last attack intent");

  const auto longRoute = altered(defs,"[Factory.GAWEAP]\nExitPath=2:1,3:1,4:1,5:1,6:1,7:1\n");
  World deployment(longRoute);
  const auto deployFactory = deployment.spawn("GAWEAP", {-2,-1}), mcv = produce(deployment,deployFactory,"AMCV");
  deployment.enqueue({deployment.tick(),0,2,CommandKind::Deploy,mcv});
  until(deployment,[&] { return view(deployment,mcv).type == "GACNST"; },"Deferred deployment failed after a clear exit");
  footprintIntact(deployment,deployFactory);
  require(view(deployment,mcv).cell == TileCoord{4,-1}, "MCV deployed at an internal route point");
}
void factionValidation(const DefinitionRegistry& defs) {
  for(const auto& [id,unit]:defs.units()) {
    (void)id;if(unit.support()==SupportStatus::Unsupported || unit.category==TypeCategory::Building || unit.visual.faction.empty())continue;
    const bool allied=canonicalId(unit.visual.faction)=="ALLIED";
    const auto factory=unit.infantry?(allied?"GAPILE":"NAHAND"):unit.naval?(allied?"GAYARD":"NAYARD"):
      unit.aircraft && unit.aircraft->fixedWing?"GAAIRC":allied?"GAWEAP":"NAWEAP";
    if(unit.spawnedAircraft || unit.missileSpawn)continue;
    require(defs.factoryMatches(defs.unit(factory),unit),"Correct native factory rejected supported unit");
    const auto wrong=unit.infantry?(allied?"NAHAND":"GAPILE"):unit.naval?(allied?"NAYARD":"GAYARD"):allied?"NAWEAP":"GAWEAP";
    require(!defs.factoryMatches(defs.unit(wrong),unit),"Opposite-faction factory accepted unit because Owner contains both sides");
  }
  World w(defs);const auto allied=w.spawn("GAWEAP",{}),soviet=w.spawn("NAWEAP",{10,0});
  w.enqueue({1,0,1,CommandKind::Produce,allied,0,{},"HTNK"});w.enqueue({1,0,2,CommandKind::Produce,soviet,0,{},"MTNK"});steps(w,1);
  require(view(w,allied).queueSize==0 && view(w,soviet).queueSize==0,"Wrong-factory command bypassed sidebar faction restriction");
}
void queuesAndContention(const DefinitionRegistry& defs) {
  World queue(defs);
  const auto factory = queue.spawn("GAWEAP", {-2,-1}), child = produce(queue,factory);
  queue.enqueue({queue.tick(),0,2,CommandKind::Produce,factory,0,{},"MTNK"}); queue.step();
  const auto remaining = view(queue,factory).productionRemaining;
  steps(queue,20);
  require(view(queue,factory).productionRemaining == remaining, "Second item advanced during an active exit");
  until(queue,[&] { return view(queue,factory).factoryPhase == FactoryExitPhase::Blocked; },"Occupied exit did not block the next item");
  require(queue.snapshot().size() == 2 && view(queue,factory).productionRemaining == 0, "Blocked queue lost or duplicated completed production");
  queue.enqueue({queue.tick(),0,3,CommandKind::Move,child,0,{8,0}});
  until(queue,[&] { return queue.snapshot().size() == 3; },"Queue did not resume when the exit cleared");
  steps(queue,300);
  require(queue.snapshot().size() == 3, "Exit generated a unit twice");

  const auto opposing = altered(defs,"[Factory.NAWEAP]\nExitPath=2:1,1:1,0:1,-1:1\n");
  World shared(opposing);
  const auto left = shared.spawn("GAWEAP", {-2,-1}), right = shared.spawn("NAWEAP", {4,-1});
  shared.enqueue({0,0,1,CommandKind::Produce,left,0,{},"MTNK"});
  shared.enqueue({0,0,2,CommandKind::Produce,right,0,{},"HTNK"});
  steps(shared,defs.profile().productionFramesOverride);
  require(view(shared,left).exitingUnit && view(shared,right).factoryPhase == FactoryExitPhase::Blocked && shared.snapshot().size() == 3,
          "Two factories claimed the same exit cell");
  const auto first = view(shared,left).exitingUnit;
  shared.enqueue({shared.tick(),0,3,CommandKind::Move,first,0,{3,4}});
  until(shared,[&] { return shared.snapshot().size() == 4; },"Losing factory did not retry the shared exit");
  const auto second = view(shared,right).exitingUnit;
  until(shared,[&] { return view(shared,second).exitFactory == 0; },"Reverse configured exit failed");
  require(view(shared,second).cell == TileCoord{3,0} && view(shared,first).cell != view(shared,second).cell, "Shared exits overlapped vehicles");

  const auto economy = altered(defs,"[Simulation]\nSandbox=no\nStartingCredits=1000\nProductionFramesOverride=120\n", "[MTNK]\nPrerequisite=\n");
  World paid(economy);
  const auto paidFactory = paid.spawn("GAWEAP", {-2,-1});
  produce(paid,paidFactory);
  paid.enqueue({paid.tick(),0,2,CommandKind::CancelProduction,paidFactory}); paid.step();
  require(paid.credits() == 300 && paid.snapshot().size() == 2, "Cancel refunded an already materialized unit");
}
void destruction(const DefinitionRegistry& defs) {
  for (const bool killFactory : {false,true}) {
    const auto combat = altered(defs,"[Factory.GAWEAP]\nPrepareFrames=180\n",
      killFactory ? "[GAWEAP]\nStrength=1\n[MTNK]\nStrength=100000\n" : "[GAWEAP]\nStrength=100000\n[MTNK]\nStrength=1\n");
    World world(combat);
    const auto factory = world.spawn("GAWEAP", {-2,-1}), child = produce(world,factory);
    const auto attacker = world.spawn("HTNK", {4,-3},1);
    world.enqueue({world.tick(),1,1,CommandKind::AttackGround,attacker,0,{0,0}});
    until(world,[&] { return !exists(world,killFactory ? factory : child); },"Test attack did not destroy its victim");
    world.enqueue({world.tick(),1,2,CommandKind::Stop,attacker}); world.step();
    if (killFactory) {
      require(exists(world,child), "Destroying the factory destroyed a surviving vehicle");
      until(world,[&] { return view(world,child).exitFactory == 0; },"Factory death stranded a surviving vehicle");
      require(view(world,child).cell == TileCoord{3,0} && world.structureAt({0,0}) == 0, "Orphan exit did not finish/release footprint");
      require(world.spawn("HTNK", {0,0}) != 0, "Destroyed factory retained a route reservation");
    } else {
      footprintIntact(world,factory);
      require(view(world,factory).exitingUnit == 0, "Factory retained a destroyed exit vehicle");
      require(world.spawn("HTNK", {3,0}) != 0, "Vehicle death leaked its external reservation");
      until(world,[&] { return view(world,factory).factoryPhase == FactoryExitPhase::Idle; },"Factory stuck after vehicle death");
    }
  }
}
void validationAndReplay(const DefinitionRegistry& defs) {
  const auto immediate = altered(defs,"[Factory.GAWEAP]\nPrepareFrames=0\nClearFrames=0\nExitPath=2:1,3:1,3:2,3:3\n");
  World turning(immediate);
  const auto factory = turning.spawn("GAWEAP", {-2,-1}), child = produce(turning,factory);
  require(view(turning,factory).factoryPhase == FactoryExitPhase::Driving, "Zero preparation introduced a waiting phase");
  until(turning,[&] { return view(turning,child).exitFactory == 0; },"Turning configured route did not finish");
  require(view(turning,child).cell == TileCoord{1,2} && view(turning,factory).factoryPhase == FactoryExitPhase::Idle,
          "Configured turn or zero clearing time ignored");
  footprintIntact(turning,factory);

  for (const auto* path : {"", "2:1", "2:1,5:1", "2:1,3:2", "2:1,3:1,2:1", "5:1,6:1", "2:1,3:1", "0:q,3:1", "2:1,67:1", "2:1,3:1,4:1,5:1,5:2,4:2"}) {
    requireRejected([&] { altered(defs,std::string("[Factory.GAWEAP]\nExitPath=")+path); }, "Invalid factory route accepted");
  }
  try { altered(defs,"[Factory.GAWEAP]\nPrepareFrames=-1"); require(false,"Negative exit timing accepted"); }
  catch (const std::runtime_error& error) {
    require(std::string(error.what()).find("factory-test.ini:2") != std::string::npos, "Factory validation lost file/line provenance");
  }
  const auto setup = [](World& w) {
    w.spawn("GAWEAP", {-2,-1}); w.spawn("NAWEAP", {8,9});
    w.enqueue({0,0,1,CommandKind::Produce,1,0,{},"MTNK"});
    w.enqueue({0,0,2,CommandKind::Produce,2,0,{},"HTNK"});
    w.enqueue({125,0,3,CommandKind::Move,3,0,{8,0}});
    w.enqueue({130,0,4,CommandKind::Move,4,0,{18,10}});
    w.enqueue({130,0,5,CommandKind::Produce,1,0,{},"AMCV"});
  };
  World reference(defs); setup(reference); steps(reference,600);
  for (const auto fps : {30,60,144}) {
    World replay(defs); setup(replay);
    for (int frame = 0; frame < fps * 10; ++frame)
      replay.advance(static_cast<std::uint64_t>(frame+1)*1000000/fps - static_cast<std::uint64_t>(frame)*1000000/fps);
    require(reference.stateHash() == replay.stateHash(), "Factory replay depends on rendering frequency");
  }
  World delayed(defs); setup(delayed); delayed.advance(10000000);
  while (delayed.tick() < reference.tick()) delayed.advance(0);
  require(delayed.stateHash() == reference.stateHash(), "Factory replay lost time during a long frame");
  std::cout << "Factory replay hash: " << reference.stateHash() << '\n';
}
}
int main(int argc, char** argv) {
  try {
    require(argc == 2, "Expected settings path");
    const auto defs = fastProductionFixture(argv[1]);
    factionValidation(defs);phasesAndReservations(defs); deferredCommands(defs); queuesAndContention(defs);
    destruction(defs); validationAndReplay(defs);
    std::cout << "Factory egress regressions passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
