#include "simulation/world.h"
#include "test_support.h"
#include <algorithm>
#include <iostream>
using namespace ra2;
namespace {
std::optional<EntitySnapshot> find(const World& w,EntityId id) {
  for(const auto& e:w.snapshot())if(e.id==id)return e;
  return {};
}
void run(World& w,int n){while(n-->0)w.step();}
DefinitionRegistry fixture(const DefinitionRegistry& base,std::string rules="",std::string profile="") {
  auto r=base.rules(),p=base.replica();
  r.overlay(IniDocument::parse("[E1]\nCanPassiveAquire=no\n[E2]\nCanPassiveAquire=no\n[HTNK]\nCanPassiveAquire=no\n"+rules));
  p.overlay(IniDocument::parse("[Simulation]\nProductionFramesOverride=4\n"+profile));
  return DefinitionRegistry::compile(r,base.art(),p);
}
void definitions(const DefinitionRegistry& d) {
  for(const auto id:{"ENGINEER","SENGINEER","SNIPE"})require(d.unit(id).unsupported.empty() && d.unit(id).infantry->bodyFrames>0,"New infantry definition/assets rejected");
  require(d.sidebarUnit("engnicon","Allied")->id=="ENGINEER" && d.sidebarUnit("engnicon","Soviet")->id=="SENGINEER","Engineer faction mapping is ambiguous");
  require(d.sidebarUnit("rtnkicon","Allied")->id=="MGTK" && d.sidebarUnit("falcicon","Allied")->id=="ORCA","Image name was used as a type ID");
  require(d.unit("SREF").primary=="COMET" && !d.unit("SREF").weaponSlots.empty() && d.weapon("COMET").laser && d.unit("SREF").support()!=SupportStatus::Unsupported,"Weapon1 / prism laser was silently discarded");
  require(d.unit("DTRUCK").support()!=SupportStatus::Unsupported && d.weapon("DEMOBOMB").supported && d.weapon("DEMOBOMB").suicide && d.unit("DTRUCK").deathWeapon=="DEMOBOMB","Suicide capability was lost or enabled as an ordinary gun");
  require(d.unit("HTK").support()==SupportStatus::Partial && d.unit("CMIN").support()==SupportStatus::Ready &&
    d.unit("CMIN").selfHealing,"Capability statuses are misleading");
  require(d.unit("ENGINEER").visual.kind==VisualKind::Sprite && d.unit("MTNK").visual.kind==VisualKind::Voxel,"Visual backend depends on the wrong category");
  require(d.allowedHouse(d.unit("SNIPE"),"British") && !d.allowedHouse(d.unit("SNIPE"),"Americans"),"Sniper national restriction ignored");
  require(d.factoryMatches(d.unit("GAPILE"),d.unit("ENGINEER")) && !d.allowedHouse(d.unit("SENGINEER"),"British"),"Engineer factory/nation rules were discarded");
}
void production(const DefinitionRegistry& base) {
  for(const bool soviet:{false,true}) {
    const auto d=fixture(base,"",std::string("[Simulation]\nSandbox=no\nStartingCredits=1000\nPlayerHouse=")+(soviet?"Russians":"British"));
    World w(d);const auto barracks=w.spawn(soviet?"NAHAND":"GAPILE",{});
    w.enqueue({1,0,1,CommandKind::Produce,barracks,0,{},soviet?"SENGINEER":"ENGINEER"});run(w,180);
    require(w.snapshot().size()==2 && w.credits()==500,"Engineer production/cost failed");
    w.enqueue({w.tick()+1,0,2,CommandKind::Produce,barracks,0,{},soviet?"ENGINEER":"SENGINEER"});run(w,8);
    require(w.snapshot().size()==2 && w.credits()==500,"Cross-faction engineer was produced");
  }
  for(const auto country:{"British","Americans"}) {
    const auto d=fixture(base,"",std::string("[Simulation]\nSandbox=no\nStartingCredits=1000\nPlayerHouse=")+country);
    World w(d);const auto barracks=w.spawn("GAPILE",{});w.spawn("GAAIRC",{8,8});
    w.enqueue({1,0,1,CommandKind::Produce,barracks,0,{},"SNIPE"});run(w,180);
    require(w.credits()==(std::string_view(country)=="British"?400:1000),"Sniper nation/cost gate failed");
  }
}
void capture(const DefinitionRegistry& base) {
  const auto d=fixture(base);
  for(const auto type:{"ENGINEER","SENGINEER"}) {
    World w(d);const auto engineer=w.spawn(type,{}),building=w.spawn("GAPOWR",{5,0},1);
    w.enqueue({1,0,1,CommandKind::EnterBuilding,engineer,building});bool entering=false;
    for(int i=0;i<300;++i){w.step();if(const auto e=find(w,engineer))entering|=e->buildingInteraction && e->buildingInteraction->entering;}
    require(entering && !find(w,engineer) && find(w,building)->owner==0,"Engineer did not enter and capture the building");
    require(w.infantryCorpses().empty() && w.spawn("HTNK",{4,0}),"Consumed engineer left a corpse or a reservation");
  }
  World w(d);const auto a=w.spawn("ENGINEER",{}),b=w.spawn("ENGINEER",{}),target=w.spawn("GAPOWR",{4,0},1);
  w.enqueue({1,0,1,CommandKind::EnterBuilding,a,target});w.enqueue({1,0,2,CommandKind::EnterBuilding,b,target});run(w,350);
  require(bool(find(w,a))+bool(find(w,b))==1 && find(w,target)->owner==0,"Two engineers were consumed for one capture");
}
void repairAndReject(const DefinitionRegistry& base) {
  const auto d=fixture(base);World w(d);const auto engineer=w.spawn("ENGINEER",{}),target=w.spawn("GAPOWR",{5,0}),gun=w.spawn("E1",{3,-2});
  const auto maxHp=d.unit("GAPOWR").strength;
  w.enqueue({1,0,1,CommandKind::AttackGround,gun,0,{5,0}});
  for(int i=0;i<60 && find(w,target)->hp==maxHp;++i)w.step();
  require(find(w,target)->hp<maxHp,"Repair fixture did not damage the building");
  w.enqueue({w.tick()+1,0,2,CommandKind::Stop,gun});w.enqueue({w.tick()+1,0,3,CommandKind::EnterBuilding,engineer,target});run(w,300);
  require(!find(w,engineer) && find(w,target)->hp==maxHp,"Engineer did not restore the friendly building");
  const auto replacement=w.spawn("ENGINEER",{}),uncapturable=w.spawn("GTGCAN",{8,0},1);
  w.enqueue({w.tick()+1,0,4,CommandKind::EnterBuilding,replacement,target});run(w,4);
  require(find(w,replacement) && !find(w,replacement)->buildingInteraction,"Full-health building consumed an engineer");
  w.enqueue({w.tick()+1,0,5,CommandKind::EnterBuilding,replacement,uncapturable});run(w,4);
  require(find(w,replacement) && !find(w,replacement)->buildingInteraction && find(w,uncapturable)->owner==1,"Uncapturable target accepted engineer");
  require(w.shots().empty(),"Engineer fired its scanner/disarm tool at a unit");
}
void cancellationAndDeath(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[M60]\nDamage=100000\n","[Simulation]\nEngineerEntryFrames=90\n");
  for(const bool kill:{false,true}) {
    World w(d);const auto engineer=w.spawn("ENGINEER",{}),target=w.spawn("GAPOWR",{4,0},1),gun=w.spawn("E1",{3,-3},1);
    w.enqueue({1,0,1,CommandKind::EnterBuilding,engineer,target});
    for(int i=0;i<240 && !(find(w,engineer)->buildingInteraction && find(w,engineer)->buildingInteraction->entering);++i)w.step();
    require(find(w,engineer)->buildingInteraction->entering,"Engineer never reached entry phase");
    const auto approach=find(w,engineer)->cell;
    if(kill)w.enqueue({w.tick()+1,1,1,CommandKind::AttackEntity,gun,engineer});
    else w.enqueue({w.tick()+1,0,2,CommandKind::Stop,engineer});
    run(w,120);require(find(w,target)->owner==1,"Cancelled/dead engineer captured its target");
    if(kill)require(!find(w,engineer) && w.spawn("HTNK",approach),"Dead engineer retained its entrance slot");
    else {
      require(find(w,engineer) && !find(w,engineer)->buildingInteraction,"Stopped entry retained its intent");
      w.enqueue({w.tick()+1,0,3,CommandKind::Move,engineer,0,{0,3}});run(w,200);
      require(find(w,engineer)->cell==TileCoord{0,3},"Cancelled engineer was stranded inside its target");
    }
  }
}
void capturedFactory(const DefinitionRegistry& base) {
  const auto d=fixture(base,"","[Simulation]\nSandbox=no\nStartingCredits=1000\nProductionFramesOverride=10000\n");World w(d);
  const auto factory=w.spawn("GAWEAP",{4,0},1),engineer=w.spawn("ENGINEER",{});
  w.enqueue({1,1,1,CommandKind::Produce,factory,0,{},"MTNK"});w.enqueue({1,0,1,CommandKind::EnterBuilding,engineer,factory});run(w,300);
  require(find(w,factory)->owner==0 && find(w,factory)->queueSize==0 && w.credits(1)==1000,"Capture retained/refunded the old factory queue incorrectly");
  const auto fast=fixture(base);World exiting(fast);const auto f=exiting.spawn("GAWEAP",{4,0},1),e=exiting.spawn("ENGINEER",{3,0});
  exiting.enqueue({1,1,1,CommandKind::Produce,f,0,{},"MTNK"});exiting.enqueue({1,0,1,CommandKind::EnterBuilding,e,f});run(exiting,220);
  int vehicles=0;for(const auto& unit:exiting.snapshot())if(unit.type=="MTNK") {
    ++vehicles;require(unit.owner==1 && !unit.exitFactory,"Captured factory stole or stranded an exiting vehicle");
  }
  require(vehicles==1 && find(exiting,f)->owner==0,"Factory capture duplicated production");
}
void blockedAndLostTarget(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[M60]\nDamage=100000\n","[Simulation]\nEngineerEntryFrames=90\n");
  World blocked(d);const auto engineer=blocked.spawn("ENGINEER",{}),target=blocked.spawn("GAPOWR",{4,0},1);
  for(const auto cell:{TileCoord{3,0},{3,1},{6,0},{6,1},{4,-1},{5,-1},{4,2},{5,2}})blocked.setBuildable(cell,false);
  blocked.enqueue({1,0,1,CommandKind::EnterBuilding,engineer,target});run(blocked,70);
  require(find(blocked,engineer) && find(blocked,target)->owner==1,"Blocked entrance consumed an engineer");
  blocked.setBuildable({3,0},true);run(blocked,300);
  require(!find(blocked,engineer) && find(blocked,target)->owner==0,"Engineer did not retry a cleared entrance");
  World lost(d);const auto a=lost.spawn("ENGINEER",{}),b=lost.spawn("GAPOWR",{4,0},1),gun=lost.spawn("E1",{6,0});
  lost.enqueue({1,0,1,CommandKind::EnterBuilding,a,b});
  for(int i=0;i<240 && !(find(lost,a)->buildingInteraction && find(lost,a)->buildingInteraction->entering);++i)lost.step();
  lost.enqueue({lost.tick()+1,0,2,CommandKind::AttackEntity,gun,b});run(lost,120);
  require(!find(lost,b) && find(lost,a) && !find(lost,a)->buildingInteraction,"Destroyed target consumed or trapped an engineer");
  lost.enqueue({lost.tick()+1,0,3,CommandKind::Move,a,0,{0,3}});run(lost,220);
  require(find(lost,a)->cell==TileCoord{0,3},"Engineer could not move after its target disappeared");
}
void capturedRefinery(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[GAREFN]\nFreeUnit=none\n","[Simulation]\nStartingCredits=0\n[Harvester.HARV]\nHarvestFrames=1\nUnloadFrames=1000\n");
  World w(d);const auto refinery=w.spawn("GAREFN",{4,0},1),miner=w.spawn("HARV",{10,0},1),engineer=w.spawn("ENGINEER",{2,0});
  w.setResource({10,0},"Riparius",2);
  for(int i=0;i<1200 && find(w,miner)->harvesting->phase!=HarvestPhase::Unloading;++i)w.step();
  require(find(w,miner)->harvesting->phase==HarvestPhase::Unloading,"Capture fixture never docked its loaded miner");
  w.enqueue({w.tick()+1,0,1,CommandKind::EnterBuilding,engineer,refinery});run(w,300);
  const auto retained=find(w,miner);
  require(find(w,refinery)->owner==0 && !find(w,engineer) && retained->owner==1 && retained->harvesting->cargo==2 && retained->harvesting->refinery!=refinery,
    "Refinery capture stole a miner, lost cargo or retained its old dock");
  require(w.credits(0)==0 && w.credits(1)==0,"Captured refinery credited another player's cargo");
  w.spawn("GAREFN",{16,0},1);
  for(int i=0;i<5000 && w.credits(1)!=50;++i)w.step();
  require(w.credits(0)==0 && w.credits(1)==50 && find(w,miner)->harvesting->cargo==0,"Old miner could not deliver to its replacement refinery");

  const auto freeDefs=fixture(base);World free(freeDefs);
  const auto target=free.spawn("GAREFN",{4,0},1),actor=free.spawn("ENGINEER",{});
  free.enqueue({1,0,1,CommandKind::EnterBuilding,actor,target});run(free,450);
  int miners=0;for(const auto& unit:free.snapshot())if(unit.type=="CMIN") {++miners;require(unit.owner==1,"Capture stole the refinery's free miner");}
  require(find(free,target)->owner==0 && miners==1,"Refinery capture duplicated its free miner");
}
void sniper(const DefinitionRegistry& base) {
  const auto d=fixture(base);World w(d);const auto shooter=w.spawn("SNIPE",{}),tank=w.spawn("HTNK",{1,0},1),victim=w.spawn("E2",{3,0},1);
  Tick start=0;int shots=0;
  for(int i=0;i<320;++i) {
    w.step();const auto e=find(w,shooter);if(e->infantry->sequence=="FIREUP")start=e->infantry->animationStarted;
    for(const auto& shot:w.shots())if(shot.sourceType=="SNIPE") {require(shot.started==start+5,"Sniper FireUp event shifted");++shots;}
  }
  require(shots==1 && !find(w,victim) && find(w,tank)->hp==400,"Sniper auto-targeting selected ineffective vehicle armor");
  const auto durable=fixture(base,"[E2]\nStrength=250\nFearless=yes\n");World repeat(durable);
  const auto a=repeat.spawn("SNIPE",{}),b=repeat.spawn("E2",{3,0},1);repeat.enqueue({1,0,1,CommandKind::AttackEntity,a,b});
  std::vector<Tick> times;for(int i=0;i<300;++i){repeat.step();for(const auto& shot:repeat.shots())if(shot.sourceType=="SNIPE")times.push_back(shot.started);}
  require(times.size()==2 && times[1]-times[0]==150 && !find(repeat,b),"Sniper ROF or anti-infantry damage is wrong");
}
void replay(const DefinitionRegistry& base) {
  const auto d=fixture(base);std::uint64_t expected=0;
  for(const int hz:{30,60,144}) {
    World w(d);w.setGameSpeed({GameSpeedMode::Multiplayer,6});
    const auto engineer=w.spawn("ENGINEER",{}),building=w.spawn("GAPOWR",{4,0},1);w.spawn("SNIPE",{0,4});w.spawn("E2",{4,4},1);
    w.enqueue({1,0,1,CommandKind::EnterBuilding,engineer,building});
    for(int f=1;f<=hz*6;++f)w.advance(std::uint64_t(f)*1000000/hz-std::uint64_t(f-1)*1000000/hz);
    require(w.tick()==360 && find(w,building)->owner==0,"Replay did not finish engineer interaction");
    if(expected)require(w.stateHash()==expected,"Render rate changed capture/sniper outcome");else expected=w.stateHash();
  }
}
}
int main(int argc,char** argv) {
  try {
    require(argc==2,"Expected settings path");const auto d=DefinitionRegistry::load(argv[1]);
    definitions(d);production(d);capture(d);repairAndReject(d);cancellationAndDeath(d);capturedFactory(d);capturedRefinery(d);blockedAndLostTarget(d);sniper(d);replay(d);
    std::cout<<"Support infantry regressions passed\n";
  } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
