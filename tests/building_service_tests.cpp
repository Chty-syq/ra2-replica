#include "simulation/world.h"
#include "presentation/world_view.h"
#include "compat/rules_timing.h"
#include "sidebar.h"
#include "test_support.h"
#include <iostream>
using namespace ra2;
namespace {
std::optional<EntitySnapshot> find(const World& w,EntityId id){for(const auto& e:w.snapshot())if(e.id==id)return e;return {};}
void run(World& w,int count){while(count-->0)w.step();}
DefinitionRegistry fixture(const DefinitionRegistry& base,std::string extra={},std::string settings={}) {
  auto rules=base.rules(),profile=base.replica();
  rules.overlay(IniDocument::parse("[GAPOWR]\nStrength=800\nCost=800\n[SREF]\nCanPassiveAquire=no\n[MTNK]\nCanPassiveAquire=no\n[HTNK]\nCanPassiveAquire=no\n[COMET]\nDamage=100\nWarhead=ServiceTestWH\n[ServiceTestWH]\nVerses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%\n"+extra));
  profile.overlay(IniDocument::parse(settings));return DefinitionRegistry::compile(rules,base.art(),profile);
}
void damage(World& w,EntityId target) {
  const auto before=find(w,target);require(before.has_value(),"Missing damage fixture target");
  const auto gun=w.spawn("SREF",{before->cell.x+5,before->cell.y},1);
  require(gun && w.enqueue({w.tick()+1,1,1,CommandKind::AttackEntity,gun,target}),"Damage setup failed");
  for(int i=0;i<100 && find(w,target)->hp==before->hp;++i)w.step();
  require(find(w,target)->hp==before->hp-100,"Damage fixture did not hit building once");
  w.enqueue({w.tick()+1,1,2,CommandKind::Stop,gun});w.step();
}
void rules(const DefinitionRegistry& d) {
  require(d.profile().repairStep==8 && d.profile().repairFrames==14 && d.profile().repairPercentMillionths==150000 && d.profile().refundPercentMillionths==500000,"Original repair/refund rules not loaded");
  require(compat::buildingRefund(10000000,10000000,10000000,500000)==5000000 &&
          compat::buildingRepairCost(10000000,10000000,10000000,150000)==1500000,"Health-dependent money overflowed");
  require(compat::buildingRefund(800,800,700,500000)==350 && compat::buildingRepairCost(800,800,8,150000)==1,"Repair/refund rounding changed");
  for(const auto invalid:{"RepairStep=0","RepairPercent=101%","RefundPercent=-1%","RepairRate=-.1"})
    requireRejected([&]{fixture(d,std::string("[General]\n")+invalid+"\n");},"Invalid service rules accepted");
}
void repair(const DefinitionRegistry& d) {
  World w(d);const auto id=w.spawn("GAPOWR",{});damage(w,id);
  require(w.canRepair(id) && !w.canRepair(id,1) && !w.canRepair(99999),"Repair capability/ownership filter failed");
  const auto money=w.credits();w.enqueue({w.tick()+1,0,1,CommandKind::ToggleRepair,id});w.step();
  require(find(w,id)->hp==708 && find(w,id)->repairing && w.credits()==money-1,"First repair step did not heal/pay");
  run(w,13);require(find(w,id)->hp==708,"Repair ran faster than original interval");w.step();require(find(w,id)->hp==716,"Repair interval was delayed");
  w.enqueue({w.tick()+1,0,2,CommandKind::ToggleRepair,id});w.step();const auto hp=find(w,id)->hp,cash=w.credits();run(w,30);
  require(!find(w,id)->repairing && find(w,id)->hp==hp && w.credits()==cash,"Cancelling repair did not stop charges/healing");
  w.enqueue({w.tick()+1,0,3,CommandKind::ToggleRepair,id});run(w,200);
  require(find(w,id)->hp==800 && !find(w,id)->repairing && w.credits()==money-13,"Repair did not cap at full HP and stop spending");
  run(w,60);require(w.credits()==money-13 && !w.canRepair(id),"Full building continued repairing");
}
void moneyPause(const DefinitionRegistry& base) {
  const auto d=fixture(base,{},"[Simulation]\nStartingCredits=0\n");World w(d);
  const auto id=w.spawn("GAPOWR",{}),sell=w.spawn("GAPOWR",{10,10});damage(w,id);
  w.enqueue({w.tick()+1,0,1,CommandKind::ToggleRepair,id});run(w,20);
  require(find(w,id)->hp==700 && find(w,id)->repairing && find(w,id)->repairPaused && w.credits()==0,"Insufficient funds caused free healing or cancelled repair");
  w.enqueue({w.tick()+1,0,2,CommandKind::SellBuilding,sell});run(w,d.profile().buildupFrames+210);
  require(!find(w,sell) && find(w,id)->hp==800 && !find(w,id)->repairing && w.credits()==387,"Repair did not resume after sale income");
  const auto limited=fixture(base,{},"[Simulation]\nStartingCredits=1\n");World lastCredit(limited);const auto one=lastCredit.spawn("GAPOWR",{});damage(lastCredit,one);
  lastCredit.enqueue({lastCredit.tick()+1,0,1,CommandKind::ToggleRepair,one});run(lastCredit,60);
  require(find(lastCredit,one)->hp==708 && find(lastCredit,one)->repairPaused && lastCredit.credits()==0,"Repair overdrew the final credit or healed for free");
}
void sellingUnderFire(const DefinitionRegistry& base) {
  for(const int hit:{100,1000}) {
    const auto d=fixture(base,"[COMET]\nDamage="+std::to_string(hit)+"\n");World w(d);const auto id=w.spawn("GAPOWR",{}),gun=w.spawn("SREF",{-5,0},1);
    const auto funds=w.credits();w.enqueue({1,0,1,CommandKind::SellBuilding,id});w.enqueue({1,1,1,CommandKind::AttackEntity,gun,id});w.enqueue({3,1,2,CommandKind::Stop,gun});
    run(w,d.profile().buildupFrames+5);
    require(!find(w,id) && w.credits()==funds+(hit==100?350:0),"Sale ignored later damage or refunded a destroyed building");
  }
}
void captureAndRefinery(const DefinitionRegistry& base) {
  World capture(base);const auto id=capture.spawn("GAPOWR",{});damage(capture,id);
  capture.enqueue({capture.tick()+1,0,1,CommandKind::ToggleRepair,id});
  const auto engineer=capture.spawn("SENGINEER",{3,0},1);capture.enqueue({capture.tick()+1,1,3,CommandKind::EnterBuilding,engineer,id});
  for(int i=0;i<350 && find(capture,id)->owner==0;++i)capture.step();
  require(find(capture,id)->owner==1 && !find(capture,id)->repairing,"Capture kept the old owner's paid repair active");
  const auto credits=capture.credits();run(capture,80);require(capture.credits()==credits,"Captured repair kept charging old owner");
  const auto d=fixture(base,"[GAREFN]\nFreeUnit=\n[CMIN]\nDock=GAREFN\n");World refinery(d);
  const auto host=refinery.spawn("GAREFN",{}),miner=refinery.spawn("CMIN",{8,0});refinery.setResource({8,0},"Riparius",4);
  refinery.enqueue({1,0,1,CommandKind::Harvest,miner,0,{8,0}});
  for(int i=0;i<500 && (!find(refinery,miner)->harvesting || find(refinery,miner)->harvesting->cargo<4);++i)refinery.step();
  require(find(refinery,miner)->harvesting->cargo==4,"Refinery sale cargo fixture failed");
  refinery.enqueue({refinery.tick()+1,0,2,CommandKind::SellBuilding,host});run(refinery,100);
  require(!find(refinery,host) && find(refinery,miner) && find(refinery,miner)->harvesting->cargo==4,"Selling refinery destroyed a miner or credited/lost its cargo");
  require(refinery.spawn("GAREFN",{}),"Sold refinery footprint was not released");
  for(int i=0;i<600 && find(refinery,miner)->harvesting->cargo;++i)refinery.step();
  require(find(refinery,miner)->harvesting->cargo==0,"Miner did not rediscover the replacement refinery");
}
void sale(const DefinitionRegistry& d) {
  World w(d);const auto id=w.spawn("GAPOWR",{});damage(w,id);const auto money=w.credits();
  require(w.canSell(id) && !w.canSell(id,1) && w.saleRefund(id)==350,"Sell permission or damaged refund failed");
  w.enqueue({w.tick()+1,0,1,CommandKind::ToggleRepair,id});w.enqueue({w.tick()+1,0,2,CommandKind::SellBuilding,id});w.step();
  require(find(w,id)->activity==Activity::Selling && !find(w,id)->repairing && !w.canSell(id) && w.credits()==money,"Sale did not stop repair or paid prematurely");
  const auto visual=makeWorldView(w,d,0).buildings.front();
  require(visual.selling && visual.state==BuildingState::Packing && buildingBuildupFrameIndex(visual,30,gameMilliseconds(w.tick()))==29,"Sale did not start reversed original buildup");
  require(w.structureAt({})==id && !w.spawn("GAPOWR",{}) && w.power().produced==0,"Sale freed footprint early or retained power");
  w.enqueue({w.tick()+1,0,3,CommandKind::SellBuilding,id});run(w,d.profile().buildupFrames-1);
  require(find(w,id) && w.credits()==money,"Repeated sell or early refund accepted");w.step();
  require(!find(w,id) && w.credits()==money+350 && !w.map().cell({}).occupied && w.structureAt({})==0 && w.spawn("GAPOWR",{}),"Sale refund/removal/rebuild failed");
  const auto wall=w.spawn("GAWALL",{10,0});const auto refund=w.saleRefund(wall),before=w.credits();
  w.enqueue({w.tick()+1,0,4,CommandKind::SellBuilding,wall});w.step();
  require(!find(w,wall) && w.credits()==before+refund && !w.map().cell({10,0}).surface.wallHeightLeptons,"Wall sale kept a collision obstacle");
}
void permissions(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[NAPOWR]\nClickRepairable=no\nUnsellable=yes\n");World w(d);
  const auto id=w.spawn("NAPOWR",{}),building=w.spawn("GAPOWR",{10,0},0,true),vehicle=w.spawn("MTNK",{15,0}),house=w.spawn("CAHSE01",{20,0});damage(w,id);
  require(!w.canRepair(id) && !w.canSell(id) && d.unit("NAPOWR").repairable,"ClickRepairable/Unsellable ignored or engineer repair disabled");
  require(!w.canRepair(building) && !w.canSell(building) && !w.canRepair(vehicle) && !w.canSell(vehicle) && !w.canSell(house),"Construction/vehicle/garrison target accepted");
  const auto hp=find(w,id)->hp,money=w.credits();w.enqueue({w.tick()+1,0,1,CommandKind::ToggleRepair,id});w.enqueue({w.tick()+1,0,2,CommandKind::SellBuilding,id});run(w,60);
  require(find(w,id)->hp==hp && !find(w,id)->repairing && w.credits()==money,"Forbidden action changed building or credits");
  const auto enemy=w.spawn("GAPOWR",{25,0},1);w.enqueue({w.tick()+1,0,3,CommandKind::SellBuilding,enemy});w.enqueue({w.tick()+1,0,4,CommandKind::ToggleRepair,enemy});run(w,60);
  require(find(w,enemy) && !find(w,enemy)->repairing && w.credits()==money,"Foreign building accepted service commands");
}
void factory(const DefinitionRegistry& base) {
  const auto d=fixture(base,{},"[Simulation]\nSandbox=no\nProductionFramesOverride=10000\n");World w(d);const auto id=w.spawn("GAWEAP",{});const auto initial=w.credits();
  w.enqueue({1,0,1,CommandKind::Produce,id,0,{},"MTNK"});w.enqueue({1,0,2,CommandKind::Produce,id,0,{},"MTNK"});w.step();
  require(find(w,id)->queueSize==2 && w.credits()==initial-2*d.unit("MTNK").cost,"Factory sale fixture failed");
  w.enqueue({2,0,3,CommandKind::SellBuilding,id});w.step();
  require(find(w,id)->queueSize==0 && w.credits()==initial,"Selling factory did not refund its paid production queue");
  run(w,d.profile().buildupFrames);require(!find(w,id) && w.credits()==initial+d.unit("GAWEAP").cost/2,"Factory refunded twice or lost sale value");
  const auto fast=fixture(base,{},"[Simulation]\nProductionFramesOverride=1\n");World busy(fast);const auto factory=busy.spawn("GAWEAP",{});
  busy.enqueue({1,0,1,CommandKind::Produce,factory,0,{},"MTNK"});busy.step();
  require(find(busy,factory)->factoryPhase!=FactoryExitPhase::Idle && !busy.canSell(factory),"Factory can sell over a unit exiting inside it");
  const auto unit=find(busy,factory)->exitingUnit;busy.enqueue({2,0,2,CommandKind::SellBuilding,factory});busy.step();
  require(find(busy,factory)->activity!=Activity::Selling,"Busy factory started selling");run(busy,350);
  require(unit && find(busy,unit) && !find(busy,unit)->exitFactory && busy.canSell(factory),"Rejected sale stranded factory exit");
}
std::uint64_t replay(const DefinitionRegistry& d,int fps) {
  World w(d);const auto a=w.spawn("GAPOWR",{}),b=w.spawn("GAPOWR",{10,10});damage(w,a);
  w.enqueue({w.tick()+1,0,1,CommandKind::ToggleRepair,a});w.enqueue({w.tick()+40,0,2,CommandKind::SellBuilding,b});
  for(int i=0;i<fps*8;++i)w.advance(std::uint64_t(i+1)*1000000/fps-std::uint64_t(i)*1000000/fps);
  return w.stateHash();
}
void modes() {
  SidebarState state;SidebarAssets assets;
  applySidebarClick(state,assets,{true,SidebarClickAction::ToggleRepair,SidebarTab::Base,{},BuildFaction::Allied});require(state.repairSelected && !state.sellSelected,"Repair mode did not activate");
  applySidebarClick(state,assets,{true,SidebarClickAction::ToggleSell,SidebarTab::Base,{},BuildFaction::Allied});require(!state.repairSelected && state.sellSelected,"Modes are not exclusive");
  applySidebarClick(state,assets,{true,SidebarClickAction::ToggleSell,SidebarTab::Base,{},BuildFaction::Allied});require(!state.sellSelected,"Sell button did not toggle off");
  applySidebarClick(state,assets,{true,SidebarClickAction::ToggleRepair,SidebarTab::Base,{},BuildFaction::Allied});applySidebarClick(state,assets,{true,SidebarClickAction::ClickIcon,SidebarTab::Base,{},BuildFaction::Allied});require(!state.repairSelected && !state.sellSelected,"Build icon kept service mode active");
}
}
int main(int argc,char** argv) {
  try {require(argc==2,"Expected settings folder");const auto base=DefinitionRegistry::load(argv[1]);const auto d=fixture(base);
    rules(base);repair(d);moneyPause(base);sellingUnderFire(base);captureAndRefinery(d);sale(d);permissions(base);factory(base);modes();
    require(replay(d,30)==replay(d,60) && replay(d,60)==replay(d,144),"Building services depend on rendering FPS");
    std::cout<<"Building service regressions passed: repair/cancel/funds/resume, sale animation/refund/occupancy, queues/exits, permissions, deterministic replay\n";return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
