#include "simulation/world.h"
#include "test_support.h"
#include <algorithm>
#include <iostream>

using namespace ra2;
namespace {
EntitySnapshot entity(const World& world, EntityId id) {
  for (const auto& view:world.snapshot()) if (view.id==id) return view;
  throw std::runtime_error("Missing mining entity");
}
void steps(World& world,int n) { for(int i=0;i<n;++i) world.step(); }
template<class Predicate> void until(World& world,Predicate predicate,const char* message,int limit=10000) {
  for(int i=0;i<limit && !predicate();++i) world.step();
  require(predicate(),message);
}
DefinitionRegistry fixture(const DefinitionRegistry& base,std::string extra={},std::string profile={}) {
  auto rules=base.rules();
  rules.overlay(IniDocument::parse("[GAREFN]\nFreeUnit=none\n[NAREFN]\nFreeUnit=none\n"+extra));
  auto replica=base.replica();
  replica.overlay(IniDocument::parse("[Simulation]\nStartingCredits=0\nSandbox=yes\nProductionFramesOverride=1\n[Harvester.HARV]\nHarvestFrames=2\nUnloadFrames=1\n[Harvester.CMIN]\nHarvestFrames=2\nUnloadFrames=1\n"+profile));
  return DefinitionRegistry::compile(rules,base.art(),replica);
}
void conservation(const DefinitionRegistry& defs) {
  for(const auto* type:{"HARV","CMIN"}) {
    World world(defs); world.spawn("GAREFN",{-1,-1}); const auto miner=world.spawn(type,{5,0});
    require(miner!=0,"Configured miner rejected");
    const auto hash=world.stateHash(); world.setResource({5,0},"Riparius",3);
    require(world.stateHash()!=hash,"Resources excluded from replay hash");
    world.setResource({6,0},"Cruentus",4);
    until(world,[&]{return entity(world,miner).harvesting->cargo==7;},"Mixed cargo not collected");
    require(world.credits()==0 && entity(world,miner).harvesting->value==275,"Income credited before delivery or mixed mineral value lost");
    bool teleported=false;
    until(world,[&]{teleported=teleported||entity(world,miner).harvesting->phase==HarvestPhase::Teleporting;return world.credits()==275;},"Partial load was not delivered");
    require(teleported==(std::string_view(type)=="CMIN"),"Teleport capability not respected");
    require(world.resources().empty(),"Depleted resource persisted");
    steps(world,500); require(world.credits()==275 && entity(world,miner).harvesting->cargo==0,"Duplicated delivery");
    require(entity(world,miner).cell!=TileCoord{3,0},"Empty miner stranded on dock");
  }
  World full(defs); full.spawn("NAREFN",{-1,-1}); const auto id=full.spawn("HARV",{6,0}); full.setResource({6,0},"Riparius",100);
  until(full,[&]{return entity(full,id).harvesting->phase==HarvestPhase::SeekingRefinery;},"Full load never returns");
  require(entity(full,id).harvesting->cargo==40 && full.resourceAt({6,0})->amount==60,"Storage=40 ignored");
}
void commandsAndOwnership(const DefinitionRegistry& defs) {
  World world(defs); const auto own=world.spawn("GAREFN",{-1,-1}); const auto enemy=world.spawn("GAREFN",{11,-1},1);
  const auto miner=world.spawn("HARV",{5,0}); world.setResource({5,0},"Riparius",25);
  until(world,[&]{return entity(world,miner).harvesting->cargo==3;},"Could not begin harvesting");
  world.enqueue({world.tick(),0,1,CommandKind::Stop,miner}); world.step();
  const auto retained=entity(world,miner).harvesting->cargo; const auto amount=world.resourceAt({5,0})->amount;
  steps(world,300); require(entity(world,miner).harvesting->phase==HarvestPhase::Stopped && world.resourceAt({5,0})->amount==amount,"Stop resumed mining");
  world.enqueue({world.tick(),0,2,CommandKind::ReturnToRefinery,miner,enemy}); steps(world,50);
  require(entity(world,miner).harvesting->cargo==retained && world.credits(1)==0,"Enemy refinery accepted cargo");
  world.enqueue({world.tick(),0,3,CommandKind::ReturnToRefinery,miner,own});
  until(world,[&]{return world.credits()==retained*25;},"Manual partial return failed");
  world.enqueue({world.tick(),0,4,CommandKind::Move,miner,0,{8,4}}); steps(world,350);
  require(entity(world,miner).cell==TileCoord{8,4} && entity(world,miner).harvesting->phase==HarvestPhase::Stopped,"Manual move overridden by mining");
  world.enqueue({world.tick(),0,5,CommandKind::Harvest,miner,0,{5,0}});
  until(world,[&]{return world.credits()==625;},"Harvest command failed to resume");
}
void contentionAndBlockedPaths(const DefinitionRegistry& defs) {
  World world(defs); world.spawn("NAREFN",{-1,-1});
  std::vector<EntityId> miners;
  for(int i=0;i<3;++i) { miners.push_back(world.spawn("HARV",{6,i*2})); world.setResource({6,i*2},"Riparius",7); }
  until(world,[&]{return world.credits()==525;},"Multiple miners deadlocked at one dock",15000);
  steps(world,200); for(auto id:miners) require(entity(world,id).harvesting->cargo==0,"Miner starved by dock queue");

  World blocked(defs); const auto refinery=blocked.spawn("GAREFN",{-1,-1}); (void)refinery;
  const auto miner=blocked.spawn("HARV",{6,0}); blocked.setResource({6,0},"Riparius",2);
  const auto obstacle=blocked.spawn("HTNK",{3,0});
  steps(blocked,700); require(blocked.credits()==0 && entity(blocked,miner).harvesting->cargo==2,"Unloaded through occupied dock");
  blocked.enqueue({blocked.tick(),0,1,CommandKind::Move,obstacle,0,{0,5}});
  until(blocked,[&]{return blocked.credits()==50;},"Dock did not recover after obstruction cleared");

  World alternate(defs); alternate.spawn("GAREFN",{-1,-1}); alternate.spawn("GAREFN",{11,-1});
  alternate.spawn("HTNK",{3,0}); alternate.spawn("HARV",{5,0}); alternate.setResource({5,0},"Riparius",2);
  until(alternate,[&]{return alternate.credits()==50;},"Blocked nearest refinery prevented fallback");

  World ore(defs); ore.spawn("GAREFN",{-1,-1}); ore.spawn("HARV",{8,0});
  ore.setResource({9,0},"Riparius",4); ore.setResource({8,4},"Cruentus",2);
  for(int x=8;x<=10;++x)for(int y=-1;y<=1;++y)if(TileCoord{x,y}!=TileCoord{9,0} && TileCoord{x,y}!=TileCoord{8,0})ore.setBuildable({x,y},false);
  // Block the near target itself, leaving a farther accessible patch.
  ore.setBuildable({9,0},false);
  until(ore,[&]{return ore.credits()==100;},"Unreachable nearest ore prevented fallback");
  require(ore.resourceAt({9,0})->amount==4,"Harvested inaccessible ore");
}
void refineryLifecycle(const DefinitionRegistry& base) {
  const auto freeDefs=fixture(base,"[GAREFN]\nFreeUnit=CMIN\n");
  World free(freeDefs); const auto refinery=free.spawn("GAREFN",{-1,-1},0,true);
  steps(free,freeDefs.profile().buildupFrames-1); require(free.snapshot().size()==1,"Free miner created before construction completed");
  steps(free,2); require(free.snapshot().size()==2,"FreeUnit not created");
  require(entity(free,refinery).activity==Activity::Idle,"Refinery construction failed");
  steps(free,400); require(free.snapshot().size()==2,"FreeUnit duplicated");

  World obstructed(freeDefs); obstructed.spawn("GAREFN",{-1,-1},0,true);
  for(int x=-1;x<=7;++x) for(int y=-4;y<=4;++y) if(!obstructed.occupant({x,y})) obstructed.setBuildable({x,y},false);
  steps(obstructed,100); require(obstructed.snapshot().size()==1,"FreeUnit spawned through blocked terrain");
  obstructed.setBuildable({4,0},true); steps(obstructed,2);
  require(obstructed.snapshot().size()==2 && obstructed.occupant({4,0})!=0,"Blocked FreeUnit was lost instead of retried");
  steps(obstructed,100); require(obstructed.snapshot().size()==2,"Blocked FreeUnit retry duplicated miner");

  // The scripted demolition gun must remain idle after Stop; automatic target
  // acquisition has its own combat regressions and would kill the waiting miner.
  const auto defs=fixture(base,"[HTNK]\nCanPassiveAquire=no\n[120mm]\nDamage=100000\nROF=1\nRange=30\n");
  World world(defs); world.spawn("GAREFN",{-1,-1}); const auto miner=world.spawn("HARV",{7,0}); world.setResource({7,0},"Riparius",4);
  until(world,[&]{return entity(world,miner).harvesting->cargo==4;},"Death fixture cargo missing");
  const auto killer=world.spawn("HTNK",{0,5},1);
  world.enqueue({world.tick(),1,1,CommandKind::AttackGround,killer,0,{0,0}});
  until(world,[&]{return world.structureAt({0,0})==0;},"Refinery not destroyed");
  world.enqueue({world.tick(),1,2,CommandKind::Stop,killer});
  steps(world,250); require(world.credits()==0 && entity(world,miner).harvesting->cargo==4,"Cargo lost/credited after refinery death");
  require(world.spawn("NAREFN",{11,-1})!=0,"Replacement refinery blocked");
  until(world,[&]{return world.credits()==100;},"Miner did not discover replacement refinery");

  World killed(defs); killed.spawn("GAREFN",{-1,-1}); const auto victim=killed.spawn("HARV",{8,0});
  killed.setResource({8,0},"Riparius",40); const auto gun=killed.spawn("HTNK",{8,5},1);
  killed.enqueue({0,1,1,CommandKind::AttackEntity,gun,victim,{8,0}});
  until(killed,[&]{return killed.occupant({8,0})==0;},"Miner not killed");
  killed.enqueue({killed.tick(),1,2,CommandKind::Stop,gun}); steps(killed,30);
  const auto remaining=killed.resourceAt({8,0})->amount;
  killed.spawn("HARV",{6,0});
  until(killed,[&]{return killed.credits()==remaining*25;},"Dead miner leaked its resource claim");
}
void configurationAndFactory(const DefinitionRegistry& base) {
  std::string extra="[VehicleTypes]\n999=TEST_MINER\n[TEST_MINER]\n";
  for(const auto& [field,value]:base.rules().section("HARV")) extra+=field+"="+value.text+"\n";
  extra+="Storage=3\nDock=NAREFN\n";
  const auto defs=fixture(base,extra);
  World world(defs); world.spawn("NAREFN",{-1,-1}); const auto miner=world.spawn("TEST_MINER",{5,0}); world.setResource({5,0},"Riparius",4);
  until(world,[&]{return world.credits()==100;},"New configured harvester requires C++ changes");
  require(entity(world,miner).harvesting->capacity==3,"New type storage ignored");
  World whitelist(defs); const auto wrong=whitelist.spawn("GAREFN",{-1,-1}); const auto custom=whitelist.spawn("TEST_MINER",{5,0});
  whitelist.setResource({5,0},"Riparius",2);
  until(whitelist,[&]{return entity(whitelist,custom).harvesting->cargo==2;},"Whitelist fixture failed");
  whitelist.enqueue({whitelist.tick(),0,1,CommandKind::ReturnToRefinery,custom,wrong}); steps(whitelist,300);
  require(whitelist.credits()==0,"Dock whitelist ignored for same-owner building");
  whitelist.spawn("NAREFN",{11,-1});
  until(whitelist,[&]{return whitelist.credits()==50;},"Compatible configured dock not discovered");
  requireRejected([&]{fixture(base,"[HARV]\nStorage=-1\n");},"Invalid capacity accepted");
  requireRejected([&]{fixture(base,"[HARV]\nDock=GAPOWR\n");},"Non-refinery dock accepted");
  requireRejected([&]{fixture(base,"[HARV]\nDock=\n");},"Empty dock list accepted");
  requireRejected([&]{fixture(base,{},"[Refinery.NAREFN]\nDockCells=-2147483648:0\n");},"Out-of-range dock offset accepted");
  requireRejected([&]{world.setResource({0,0},"MISSING",1);},"Unknown resource accepted");
  requireRejected<std::invalid_argument>([&]{world.setResource({0,0},"Riparius",-1);},"Negative resource accepted");

  World factory(defs); const auto factoryId=factory.spawn("NAWEAP",{-2,-1}); factory.spawn("NAREFN",{9,-1}); factory.setResource({5,0},"Riparius",10);
  factory.enqueue({0,0,1,CommandKind::Produce,factoryId,0,{},"HARV"});
  EntityId produced=0; bool sawExit=false;
  for(int i=0;i<600;++i) {
    factory.step(); for(const auto& view:factory.snapshot()) if(view.type=="HARV") {
      produced=view.id;
      if(view.activity==Activity::LeavingFactory) { sawExit=true; require(view.harvesting->cargo==0,"Miner collected while inside factory"); }
    }
  }
  require(produced && sawExit,"Harvester factory production unsupported");
  until(factory,[&]{return factory.credits()==250;},"Produced miner did not start mining");

  World stopped(defs); const auto stoppedFactory=stopped.spawn("NAWEAP",{-2,-1}); stopped.setResource({5,0},"Riparius",10);
  stopped.enqueue({0,0,1,CommandKind::Produce,stoppedFactory,0,{},"HARV"}); stopped.step();
  EntityId stopMiner=0;for(const auto& view:stopped.snapshot())if(view.type=="HARV")stopMiner=view.id;
  require(stopMiner!=0,"Stop-during-egress fixture failed");
  stopped.enqueue({stopped.tick(),0,2,CommandKind::Stop,stopMiner}); steps(stopped,500);
  require(entity(stopped,stopMiner).activity==Activity::Idle && entity(stopped,stopMiner).harvesting->phase==HarvestPhase::Stopped,
    "Factory exit resumed auto mining after Stop");
}
void incomeAndMultipleDocks(const DefinitionRegistry& base) {
  const auto paid=fixture(base,{},"[Simulation]\nSandbox=no\n");
  World economy(paid); const auto factory=economy.spawn("NAWEAP",{-2,-1}); economy.spawn("NAREFN",{9,-1});
  economy.enqueue({0,0,1,CommandKind::Produce,factory,0,{},"HTNK"}); economy.step();
  require(entity(economy,factory).queueSize==0,"Zero-credit production accepted");
  economy.spawn("HARV",{8,4}); economy.setResource({8,4},"Riparius",40);
  until(economy,[&]{return economy.credits()==1000;},"Income did not reach shared player economy");
  economy.enqueue({economy.tick(),0,2,CommandKind::Produce,factory,0,{},"HTNK"}); economy.step();
  require(economy.credits()==100,"Production did not spend delivered mining income");

  const auto multiple=fixture(base,"[NAREFN]\nNumberOfDocks=2\n","[Refinery.NAREFN]\nDockCells=4:1,4:3\n");
  World docks(multiple); docks.spawn("NAREFN",{-1,-1});
  for(int y:{0,2,4,6}) {docks.spawn("CMIN",{6,y});docks.setResource({6,y},"Cruentus",20);}
  bool concurrent=false;
  until(docks,[&]{int unloading=0;for(const auto& v:docks.snapshot())if(v.harvesting && v.harvesting->phase==HarvestPhase::Unloading)++unloading;
    concurrent=concurrent || unloading==2;return docks.credits()==4000;},"Multiple configured docks deadlocked");
  require(concurrent,"NumberOfDocks did not allow concurrent unloading");
}
void animationTiming(const DefinitionRegistry& defs) {
  World world(defs); const auto miner=world.spawn("HARV",{5,0}); world.setResource({5,0},"Riparius",10);
  until(world,[&]{return entity(world,miner).harvesting->phase==HarvestPhase::Harvesting;},"Mining animation never began");
  const auto started=world.tick();
  require(entity(world,miner).harvesting->cycleStarted==started,"Animation did not start at the first harvest stroke");
  const auto hash=world.stateHash(); (void)world.snapshot();
  require(world.stateHash()==hash,"Reading animation timing mutated simulation state");
  until(world,[&]{return entity(world,miner).harvesting->cargo==1;},"First harvest stroke never completed");
  require(world.tick()-started==static_cast<GameFrame>(defs.unit("HARV").harvester->harvestFrames) &&
          entity(world,miner).harvesting->cycleStarted==world.tick(),"Animation drifted from the next harvest stroke");
  world.enqueue({world.tick(),0,1,CommandKind::Stop,miner}); world.step();
  require(entity(world,miner).harvesting->cycleStarted==0,"Stopped miner retained an active harvest animation");
}
void determinism(const DefinitionRegistry& defs) {
  World a(defs),b(defs);
  for(auto* world:{&a,&b}) { world->spawn("GAREFN",{-1,-1}); world->spawn("HARV",{5,0}); world->spawn("CMIN",{7,1}); world->setResource({5,0},"Riparius",41); world->setResource({7,1},"Cruentus",21); }
  a.setGameSpeed({GameSpeedMode::Multiplayer,1}); b.setGameSpeed({GameSpeedMode::Singleplayer,6});
  for(int i=0;i<5000;++i) {a.step();b.step(); require(a.stateHash()==b.stateHash(),"Mining outcome depends on game-speed/presentation");}
  require(a.credits()==2075,"Deterministic mining scenario did not finish");
}
}
int main(int argc,char** argv) {
  try {
    require(argc==2,"Expected settings path"); const auto base=DefinitionRegistry::load(argv[1]); const auto defs=fixture(base);
    conservation(defs); commandsAndOwnership(defs); contentionAndBlockedPaths(defs); animationTiming(defs);
    refineryLifecycle(base); configurationAndFactory(base); incomeAndMultipleDocks(base); determinism(defs);
    std::cout<<"Harvesting regressions passed\n";return 0;
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
