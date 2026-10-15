#include "config/definitions.h"
#include "art_ini.h"
#include "simulation/world.h"
#include "test_support.h"
#include "simulation_fixture.h"
#include <algorithm>
#include <iostream>

using namespace ra2;
namespace {
EntitySnapshot find(const World& world, EntityId id) {
  const auto views = world.snapshot();
  const auto it = std::find_if(views.begin(), views.end(), [&](const auto& v) { return v.id == id; });
  require(it != views.end(), "Entity unexpectedly missing");
  return *it;
}
void steps(World& world, int count) { for (int i = 0; i < count; ++i) world.step(); }
void configTests(const DefinitionRegistry& defs) {
  auto base = IniDocument::parse("\xef\xbb\xbf[A]\nStrength=400\n[A]\nSpeed=6 ; comment\n", "base.ini");
  base.overlay(IniDocument::parse("[a]\nstrength=300\n", "override.ini"));
  require(base.integer("a", "STRENGTH") == 300 && base.integer("A", "speed") == 6, "Field overlay discarded inherited values");
  require(base.find("A", "Strength")->file == "override.ini" && base.find("A", "Strength")->line == 2, "Overlay provenance lost");
  for (const auto* bad : {"400abc", "", "999999999999", "1.5"}) {
    const auto doc = IniDocument::parse(std::string("[X]\nN=") + bad, "bad.ini");
    requireRejected([&] { doc.integer("X", "N"); }, "Malformed integer accepted");
  }
  requireRejected([] { IniDocument::parse("[X]\nB=maybe").boolean("X", "B"); }, "Malformed boolean accepted");
  require(defs.unit("htnk").strength == 400 && defs.unit("mtnk").strength == 300, "Tank Strength import failed");
  require(defs.unit("GAPOWR").strength == 750, "Building Strength import failed");
  require(defs.weapon(defs.unit("HTNK").primary).damage == 90, "Primary reference not resolved");
  require(defs.weapon(defs.unit("MTNK").primary).damage == 65, "Grizzly weapon wrong");
  require(defs.unit("AMCV").deploysInto == "GACNST", "Deploy reference wrong");
  require(!defs.unit("BEAG").unsupported.empty() && !defs.unit("DEST").unsupported.empty(), "Unsupported air/naval silently accepted");
  require(defs.unit("HTNK").unsupported.empty(), "Rhino capabilities rejected");
  require(defs.unit("MTNK").unsupported.empty(), "Grizzly capabilities rejected");
  const auto art = ArtIni::fromDocument(defs.art(), defs.replica());
  require(art.building("GAWEAP").layers.size() == 5 && art.building("GAWEAP").deferOverUnit, "Factory layer profile not imported");
  require(art.animationFrameLimit("GAWEAP_A") == 15, "Animation profile not imported");
  const auto positionedArt = ArtIni::fromDocument(IniDocument::parse(
    "[B]\nActiveAnim=LIGHT\nActiveAnimX=-7\nActiveAnimY=12\nActiveAnimZAdjust=-32\nActiveAnimYSort=362\n"
    "[LIGHT]\nYDrawOffset=-3\nNormalZAdjust=-10\n"));
  const auto placement = positionedArt.animationPlacement("B","ActiveAnim");
  const auto animation = positionedArt.animation("LIGHT");
  require(placement.offset.x==-7 && placement.offset.y==12 && placement.zAdjust==-32 && placement.ySort==362 &&
          animation.yDrawOffset==-3 && animation.normalZAdjust==-10,
          "Animation position and depth fields were conflated or dropped");
  require(art.animationPlacement("GAPOWR","ActiveAnim").offset.y==0 &&
          art.animationPlacement("GAHPAD","ActiveAnim").offset.y==-12,
          "Original art.ini slot positions were not imported");
  const auto missing = positionedArt.building("B").foundation.value_or(FoundationSize{});
  require(missing.width==1 && missing.height==1, "Missing Foundation must default to 1x1");
  requireRejected([] { (void)ArtIni::fromDocument(IniDocument::parse("[B]\nFoundation=Custom\n")).building("B"); },
                  "Unsupported Foundation silently became 1x1");
  auto rules = defs.rules();
  std::string extra = "[VehicleTypes]\n999=TEST_TANK\n[TEST_TANK]\n";
  for (const auto& [field, value] : rules.section("HTNK")) extra += field + "=" + value.text + "\n";
  extra += "Strength=812\nSpeed=9\nPrimary=105mm\n";
  rules.overlay(IniDocument::parse(extra));
  const auto extended = DefinitionRegistry::compile(rules, defs.art(), defs.replica());
  World modded(extended);
  const auto id = modded.spawn("TEST_TANK", {0,0});
  require(id && find(modded,id).hp == 812 && extended.weapon(extended.unit("TEST_TANK").primary).damage == 65,
          "Adding a new rules type still requires per-unit C++ code");
}
void behaviorTests(const DefinitionRegistry& defs) {
  World world(defs);
  const auto rhino = world.spawn("HTNK", {0,0});
  const auto grizzly = world.spawn("MTNK", {0,4});
  require(rhino && grizzly && find(world, rhino).hp == 400 && find(world, grizzly).hp == 300, "Unified spawning ignored Strength");
  require(world.spawn("BEAG", {9,9}) == 0 && world.spawn("HTNK", {0,0}) == 0, "Unsupported or overlapping spawn accepted");
  require(world.enqueue({0,0,1,CommandKind::Move,rhino,0,{6,0}}), "Move not queued");
  require(!world.enqueue({0,0,1,CommandKind::Move,rhino,0,{6,0}}), "Duplicate command accepted");
  world.enqueue({0,0,2,CommandKind::Move,grizzly,0,{6,4}});
  steps(world, 100);
  require(find(world, grizzly).position.x > find(world, rhino).position.x, "Speed not driven by unit rules");
  steps(world, 150);
  require(find(world, rhino).cell.x == 6 && find(world, grizzly).cell.x == 6, "Move did not finish");

  World contested(defs);
  const auto a = contested.spawn("HTNK", {-2,0}), b = contested.spawn("HTNK", {2,0});
  contested.enqueue({0,0,1,CommandKind::Move,a,0,{0,0}});
  contested.enqueue({0,0,2,CommandKind::Move,b,0,{0,0}});
  for (int i=0;i<300;++i) {
    contested.step();
    const auto av = find(contested,a), bv = find(contested,b);
    require(av.position != bv.position, "Units overlapped while contesting a cell");
    require(contested.occupant(av.cell) == a && contested.occupant(bv.cell) == b, "Occupancy owner overwritten");
  }

  // Isolate damage/death/occupancy from the defender's newly supported retaliation.
  auto battleRules=defs.rules(); battleRules.overlay(IniDocument::parse("[HTNK]\nCanPassiveAquire=no"));
  const auto battleDefinitions=DefinitionRegistry::compile(battleRules,defs.art(),defs.replica());
  World battle(battleDefinitions);
  const auto attacker = battle.spawn("MTNK", {0,0});
  const auto defender = battle.spawn("HTNK", {3,0}, 1);
  battle.enqueue({0,0,1,CommandKind::AttackEntity,attacker,defender,{3,0}});
  steps(battle, 40); // First impact, before the next 60-game-frame shot.
  require(find(battle,defender).hp == 335, "Grizzly projectile/armor damage was not 65");
  steps(battle, 1500);
  require(battle.occupant({3,0}) == 0 && battle.snapshot().size() == 1, "Death did not release entity occupancy");

  World deployment(defs);
  const auto mcv1 = deployment.spawn("AMCV", {0,0}), mcv2 = deployment.spawn("AMCV", {10,10});
  deployment.enqueue({0,0,1,CommandKind::Deploy,mcv1});
  deployment.enqueue({0,0,2,CommandKind::Deploy,mcv2});
  steps(deployment, 150);
  require(find(deployment,mcv1).type == "GACNST" && find(deployment,mcv2).type == "GACNST", "Multiple MCV deployment failed");
  deployment.enqueue({deployment.tick(),0,3,CommandKind::Deploy,mcv1});
  steps(deployment,150);
  require(find(deployment,mcv1).type == "AMCV" && find(deployment,mcv2).type == "GACNST", "Per-entity undeploy failed");
  require(deployment.occupant({1,1}) == 0, "Old building footprint leaked after undeploy");

  World factories(defs);
  const auto f1 = factories.spawn("GAWEAP", {-2,-1}), f2 = factories.spawn("GAWEAP", {8,9});
  factories.enqueue({0,0,1,CommandKind::Produce,f1,0,{},"MTNK"});
  factories.enqueue({0,0,2,CommandKind::Produce,f2,0,{},"AMCV"});
  steps(factories, 150);
  require(factories.snapshot().size() == 4, "Independent factory queues did not produce");
  EntityId producedMcv = 0;
  for (const auto& v : factories.snapshot()) { if (v.type == "MTNK") require(v.hp == 300, "Produced stats differ from initial stats"); if (v.type == "AMCV") producedMcv = v.id; }
  require(producedMcv != 0, "Produced MCV missing");
  factories.enqueue({factories.tick(),0,3,CommandKind::Move,producedMcv,0,{20,20}});
  for (int i=0;i<1200 && find(factories,producedMcv).cell != TileCoord{20,20};++i) factories.step();
  require(find(factories,producedMcv).cell == TileCoord{20,20} && find(factories,producedMcv).activity == Activity::Idle,
          "Produced MCV failed to finish its deferred move");
  factories.enqueue({factories.tick(),0,4,CommandKind::Deploy,producedMcv});
  steps(factories,150);
  require(find(factories,producedMcv).type == "GACNST", "Produced MCV cannot deploy");
}
void edgeTests(const DefinitionRegistry& defs) {
  World blocked(defs);
  const auto factory = blocked.spawn("GAWEAP", {-2,-1});
  const auto obstacle = blocked.spawn("HTNK", {3,0});
  blocked.enqueue({0,0,1,CommandKind::Produce,factory,0,{},"MTNK"});
  steps(blocked,300);
  require(blocked.snapshot().size() == 2 && find(blocked,factory).productionRemaining == 0 && find(blocked,factory).queueSize == 1,
          "Blocked factory exit lost or duplicated production");
  blocked.enqueue({blocked.tick(),0,2,CommandKind::Move,obstacle,0,{8,0}});
  steps(blocked,200);
  require(blocked.snapshot().size() == 3 && find(blocked,factory).queueSize == 0, "Factory did not resume when exit cleared");
  steps(blocked,200);
  require(blocked.snapshot().size() == 3, "Factory generated completed item more than once");

  World deployment(defs);
  const auto mcv = deployment.spawn("AMCV", {0,0});
  const auto blocker = deployment.spawn("HTNK", {1,1});
  deployment.enqueue({0,0,1,CommandKind::Deploy,mcv});
  steps(deployment,150);
  require(find(deployment,mcv).type == "AMCV" && deployment.occupant({1,1}) == blocker, "Blocked deploy overwrote another entity");
  require(!deployment.enqueue({deployment.tick(),0,2,CommandKind::Produce,mcv}), "Empty production type accepted");
  deployment.enqueue({deployment.tick(),1,1,CommandKind::Move,mcv,0,{7,0}});
  steps(deployment,100);
  require(find(deployment,mcv).position == Position{}, "A foreign player commanded an entity");

  auto rules = defs.rules();
  rules.overlay(IniDocument::parse("[MTNK]\nPrerequisite=\n"));
  auto profile = defs.replica();
  profile.overlay(IniDocument::parse("[Simulation]\nSandbox=no\nStartingCredits=1000\nProductionFramesOverride=120"));
  const auto economy = DefinitionRegistry::compile(rules, defs.art(), profile);
  World paid(economy);
  const auto paidFactory = paid.spawn("GAWEAP", {-2,-1});
  paid.enqueue({0,0,1,CommandKind::Produce,paidFactory,0,{},"MTNK"});
  paid.enqueue({0,0,2,CommandKind::Produce,paidFactory,0,{},"MTNK"});
  paid.step();
  require(paid.credits() == 300 && find(paid,paidFactory).queueSize == 1, "Queue charged incorrectly or ignored credits");
  paid.enqueue({paid.tick(),0,3,CommandKind::CancelProduction,paidFactory}); paid.step();
  require(paid.credits() == 1000 && find(paid,paidFactory).queueSize == 0, "Cancellation refund or queue cleanup failed");

  World loss(defs);
  const auto gun = loss.spawn("HTNK", {0,0});
  const auto target = loss.spawn("MTNK", {3,0},1);
  loss.enqueue({0,0,1,CommandKind::AttackEntity,gun,target,{3,0}});
  steps(loss,1500);
  require(loss.shots().empty() && loss.snapshot().size() == 1, "Destroyed target remained active for firing");
  loss.enqueue({loss.tick(),0,2,CommandKind::Move,target,0,{4,0}}); loss.step();
  require(loss.snapshot().size() == 1, "Command to dead entity resurrected it");
}
void replayTests(const DefinitionRegistry& defs) {
  World a(defs), b(defs);
  for (auto* world : {&a,&b}) {
    const auto id = world->spawn("HTNK", {0,0});
    world->enqueue({1,0,1,CommandKind::Move,id,0,{10,4}});
    world->enqueue({250,0,2,CommandKind::AttackGround,id,0,{10,6}});
  }
  for (int i=0;i<100;++i) a.advance(100000);
  for (int i=0;i<400;++i) b.advance(25000);
  require(a.tick() == 600 && a.stateHash() == b.stateHash(), "Simulation depends on render frame partition");
  for (const int fps : {30,60,144}) {
    World replay(defs);
    const auto id = replay.spawn("HTNK", {0,0});
    replay.enqueue({1,0,1,CommandKind::Move,id,0,{10,4}});
    replay.enqueue({250,0,2,CommandKind::AttackGround,id,0,{10,6}});
    for (int frame=0; frame<fps*10; ++frame) replay.advance((static_cast<std::uint64_t>(frame+1)*1000000/fps) - (static_cast<std::uint64_t>(frame)*1000000/fps));
    require(replay.stateHash() == a.stateHash(), "30/60/144 Hz replay differs");
  }
  World delayed(defs);
  delayed.spawn("HTNK", {0,0});
  delayed.enqueue({1,0,1,CommandKind::Move,1,0,{10,4}});
  delayed.enqueue({250,0,2,CommandKind::AttackGround,1,0,{10,6}});
  delayed.advance(10000000);
  require(delayed.tick() == 120, "Long frame exceeded catch-up budget");
  for (int i=0;i<4;++i) delayed.advance(0);
  require(delayed.stateHash() == a.stateHash(), "Long frame discarded simulation time");
  World terrainA(defs), terrainB(defs);
  terrainB.setBuildable({2,2}, false);
  require(terrainA.stateHash() != terrainB.stateHash(), "Terrain absent from state hash");
  std::cout << "Replay hash: " << a.stateHash() << '\n';
}
}
int main(int argc, char** argv) {
  try {
    require(argc == 2, "Expected settings path");
    const auto defs = fastProductionFixture(argv[1]);
    std::cout << "Definitions: " << defs.units().size() << ", fingerprint: " << defs.fingerprint() << '\n';
    configTests(defs); behaviorTests(defs); edgeTests(defs); replayTests(defs);
    std::cout << "Configuration and simulation regressions passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
