#include "simulation/world.h"
#include "compat/rules_timing.h"
#include "art_ini.h"
#include "test_support.h"
#include <algorithm>
#include <iostream>

using namespace ra2;
namespace {
EntitySnapshot view(const World& world, EntityId id) {
  const auto views = world.snapshot();
  const auto it = std::find_if(views.begin(),views.end(),[&](const auto& v) { return v.id == id; });
  require(it != views.end(),"Calibration entity disappeared");
  return *it;
}
DefinitionRegistry changed(const DefinitionRegistry& defs, const char* rules, const char* replica = "") {
  auto r = defs.rules(), p = defs.replica();
  r.overlay(IniDocument::parse(rules,"calibration-rules.ini"));
  p.overlay(IniDocument::parse(replica,"calibration-profile.ini"));
  return DefinitionRegistry::compile(r,defs.art(),p);
}
void movement(const DefinitionRegistry& defs) {
  for (const auto& [type,arrival] : {std::pair{"HTNK",171}, {"MTNK",151}, {"AMCV",256}}) {
    World world(defs); const auto id = world.spawn(type,{});
    world.enqueue({1,0,1,CommandKind::Move,id,0,{10,0}});
    for (int i=1;i<arrival;++i) world.step();
    require(view(world,id).cell != TileCoord{10,0},"Straight movement arrived before its calibrated frame");
    world.step();
    require(view(world,id).cell == TileCoord{10,0},"Straight movement lost fractional distance across cells");
  }
  require(compat::movementLeptons(0)==0 && compat::movementLeptons(100)==255 && compat::movementLeptons(150)==255,
          "Original Speed import bounds changed");
}
void rotation(const DefinitionRegistry& defs) {
  World world(defs); const auto id = world.spawn("HTNK",{});
  world.enqueue({1,0,1,CommandKind::AttackGround,id,0,{0,3}});
  world.step(); require(view(world,id).turretHeading == 0,"Rotation advanced before its start frame");
  for(int i=0;i<6;++i) world.step();
  require(view(world,id).turretHeading == 16384/2,"Turn does not interpolate across its integer duration");
  for(int i=0;i<5;++i) world.step();
  require(world.shots().empty(),"Turret fired before its calibrated turn completed");
  world.step(); require(!world.shots().empty() && world.shots()[0].started==13,"ROT=5 quarter turn has the wrong duration");

  world.enqueue({world.tick(),0,2,CommandKind::AttackGround,id,0,{3,0}}); world.step(); world.step();
  world.enqueue({world.tick(),0,3,CommandKind::Stop,id}); world.step();
  const auto stopped = view(world,id).turretHeading;
  for(int i=0;i<30;++i) world.step();
  require(view(world,id).turretHeading==stopped,"Stop allowed a stale rotation timer to keep moving");
  world.enqueue({world.tick(),0,4,CommandKind::AttackGround,id,0,{3,0}}); world.step();
  require(view(world,id).turretHeading==stopped,"Resuming the same target reused an expired rotation");

  const auto instant = changed(defs,"[HTNK]\nROT=0");
  World noTurn(instant); const auto tank = noTurn.spawn("HTNK",{});
  noTurn.enqueue({1,0,1,CommandKind::AttackGround,tank,0,{0,3}}); noTurn.step();
  require(!noTurn.shots().empty(),"ROT=0 was treated as unable to turn");
}
void production(const DefinitionRegistry& defs) {
  require(defs.profile().productionFramesOverride==0,"Default still uses accelerated test production");
  for (const auto& [type,duration] : {std::pair{"HTNK",540}, {"MTNK",432}, {"AMCV",1890}}) {
    require(defs.unit(type).productionFrames==duration,"Cost/BuildSpeed was not quantized to factory stages");
    World world(defs);
    const auto factory=world.spawn(std::string_view(type)=="HTNK" ? "NAWEAP":"GAWEAP",{});
    world.enqueue({1,0,1,CommandKind::Produce,factory,0,{},type});
    for(int i=1;i<duration;++i) world.step();
    require(world.snapshot().size()==1 && view(world,factory).productionRemaining==1,"Product spawned early");
    world.step(); require(world.snapshot().size()==2,"Product missed its final production frame");
  }
  const auto custom = changed(defs,"[MTNK]\nCost=1000\nBuildTimeMultiplier=0.5\nPrerequisite=\n");
  require(custom.unit("MTNK").productionFrames==270,"Per-type BuildTimeMultiplier was ignored");
  const auto paid=changed(custom,"","[Simulation]\nSandbox=no\nStartingCredits=1000");
  World world(paid); const auto factory=world.spawn("GAWEAP",{-2,-1});
  world.enqueue({1,0,1,CommandKind::Produce,factory,0,{},"MTNK"}); world.step();
  require(view(world,factory).productionTotal==270 && world.credits()==0,"Sandbox flag still controls production speed");
  require(compat::productionFrames(0,700000,1000000)==54 && compat::productionFrames(10000000,700000,1000000)==13770,
          "Factory interval bounds changed");
}
void configuration(const DefinitionRegistry& defs) {
  require(defs.profile().buildupFrames==54,"BuildupTime .06 was not read as game minutes");
  const auto slow=changed(defs,"[General]\nBuildupTime=.12");
  World world(slow); const auto id=world.spawn("GAPOWR",{},0,true);
  for(int i=0;i<107;++i) world.step();
  require(view(world,id).activity==Activity::Constructing,"Buildup ended before configured duration");
  world.step(); require(view(world,id).activity==Activity::Idle,"Buildup never completed");
  for(const auto* invalid : {"[General]\nBuildSpeed=-1", "[General]\nBuildupTime=nan", "[MTNK]\nBuildTimeMultiplier=1oops"})
    requireRejected([&] { changed(defs,invalid); },"Malformed calibration field accepted");
  const auto art=ArtIni::fromDocument(defs.art());
  require(art.animation("GAPOWR_A").normalized && art.animation("GAPOWR_A").rate==220,"Animation normalization flags not imported");
  require(compat::animationDelay(220)==4 && compat::animationDelay(300)==3 && compat::animationDelay(400)==2 &&
          compat::animationDelay(0)==0,"Animation Rate still interpreted as milliseconds");
  require(compat::normalizedAnimationDelay(4,{GameSpeedMode::Multiplayer,6})==7 &&
          compat::normalizedAnimationDelay(4,{GameSpeedMode::Multiplayer,4})==5 &&
          compat::normalizedAnimationDelay(6,{GameSpeedMode::Multiplayer,4})==16,"Normalized animation delay table changed");
}
void nativeGlobalSections(const DefinitionRegistry& defs) {
  const auto custom=changed(defs,"[CombatDamage]\nIvanDamage=73\nIvanTimedDelay=21\nIvanIconFlickerRate=4\nC4Delay=.1\n[AudioVisual]\nGravity=3\nConditionRed=10%\nConditionYellow=40%\nPoseDir=6\nWarpOut=S_BANG34\n");
  require(custom.profile().gravity==3 && custom.profile().conditionRedMillionths==100000 && custom.profile().selfHealCapMillionths==400000,"Native AudioVisual values silently replaced by General fallbacks");
  require(custom.ivanBomb().damage==73 && custom.ivanBomb().delay==21 && custom.ivanBomb().flickerFrames==4,"Native CombatDamage bomb values silently replaced by General fallbacks");
  requireRejected([&]{changed(defs,"[CombatDamage]\nC4Delay=0");},"Native C4 delay validation read the wrong section");
  requireRejected([&]{changed(defs,"[AudioVisual]\nGravity=0");},"Native gravity validation read the wrong section");
  World w(custom);const auto airport=w.spawn("GAAIRC",{12,0}),plane=w.spawn("ORCA",{});require(airport && plane,"Native landing heading setup");
  require(view(w,plane).heading==24576,"Native AudioVisual.PoseDir was ignored");
  const auto chrono=w.spawn("CLEG",{});w.enqueue({1,0,1,CommandKind::Move,chrono,0,{4,0}});w.step();
  require(std::any_of(w.effects().begin(),w.effects().end(),[](const auto& effect){return effect.animation=="S_BANG34";}),"Native AudioVisual.WarpOut was ignored");
  World c4(custom);const auto commando=c4.spawn("TANY",{}),target=c4.spawn("GAPOWR",{2,0},1);c4.enqueue({1,0,1,CommandKind::AttackEntity,commando,target});
  Tick planted=0;for(int i=0;i<200 && !planted;++i){c4.step();for(const auto& e:c4.snapshot())if(e.id==target && e.c4Detonates)planted=e.c4Detonates;}
  require(planted && planted-c4.tick()==90,"Native CombatDamage.C4Delay did not change the planted charge");
}

}
int main(int argc,char** argv) {
  try {
    require(argc==2,"Expected settings path");
    const auto defs=DefinitionRegistry::load(argv[1]);
    movement(defs); rotation(defs); production(defs); configuration(defs);nativeGlobalSections(defs);
    std::cout << "Rule calibration regressions passed; configuration: " << defs.fingerprint() << '\n';
    return 0;
  } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
