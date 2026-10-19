#include "simulation/world.h"
#include "test_support.h"
#include <algorithm>
#include <iostream>
#include <set>
using namespace ra2;
namespace {
EntitySnapshot get(const World& world,EntityId id) {
  for(const auto& e:world.snapshot())if(e.id==id)return e;
  throw std::runtime_error("Missing entity "+std::to_string(id));
}
void run(World& world,int frames){for(int i=0;i<frames;++i)world.step();}
DefinitionRegistry fixture(const DefinitionRegistry& base,std::string rules="",std::string profile="") {
  auto r=base.rules(),p=base.replica();r.overlay(IniDocument::parse("[E1]\nCanPassiveAquire=no\n[E2]\nCanPassiveAquire=no\n[HTNK]\nCanPassiveAquire=no\n"+rules));
  p.overlay(IniDocument::parse("[Simulation]\nProductionFramesOverride=4\n"+profile));
  return DefinitionRegistry::compile(r,base.art(),p);
}
void occupancy(const DefinitionRegistry& base) {
  const auto defs=fixture(base);World world(defs);
  std::vector<EntityId> ids;for(int i=0;i<3;++i)ids.push_back(world.spawn("E1",{}));
  require(ids[0] && ids[1] && ids[2] && !world.spawn("E2",{}),"Infantry cell capacity is not three");
  require(!world.spawn("HTNK",{}) && !world.spawn("E2",{},1),"Full-cell/enemy placement overlaps infantry");
  requireRejected<std::invalid_argument>([&]{world.setTerrain({},{});},"Terrain edited under infantry");
  require(world.occupants({}).size()==3,"Occupancy query loses same-cell units");
  Command command{1,0,1,CommandKind::GroupMove,0,0,{5,0}};command.members=ids;require(world.enqueue(command),"Group rejected");
  run(world,230);
  std::set<int> slots;for(auto id:ids){const auto e=get(world,id);require(e.cell==TileCoord{5,0},"Group failed to share destination");slots.insert(e.infantry->subCell);}
  require(slots.size()==3,"Group reused a reserved subcell");
  require(world.spawn("HTNK",{})!=0,"Departed infantry kept the source occupied");
  world.enqueue({world.tick()+1,0,2,CommandKind::Move,ids[0],0,{12,0}});run(world,5);
  world.enqueue({world.tick()+1,0,3,CommandKind::Stop,ids[0]});run(world,40);
  const auto stopped=get(world,ids[0]).position;run(world,20);require(get(world,ids[0]).position==stopped,"Stop did not finish the committed stride");
}
void fireAndDeploy(const DefinitionRegistry& base) {
  const auto defs=fixture(base,"[E2]\nFearless=yes\n");World world(defs);
  const auto attacker=world.spawn("E1",{}),a=world.spawn("E2",{2,0},1),b=world.spawn("E2",{2,0},1);
  world.enqueue({1,0,1,CommandKind::AttackEntity,attacker,a});
  bool fired=false;for(int i=0;i<50;++i){world.step();for(const auto& shot:world.shots())if(shot.weaponId=="M60")fired=true;}
  require(fired && get(world,a).hp<125 && get(world,b).hp==125,"Direct bullet damaged a neighbouring subcell or failed to fire");
  world.enqueue({world.tick()+1,0,2,CommandKind::Deploy,attacker});run(world,16);
  require(get(world,attacker).infantry->posture==InfantryPosture::Deployed,"GI did not deploy");
  const auto before=get(world,attacker).position;
  world.enqueue({world.tick()+1,0,3,CommandKind::Move,attacker,0,{8,0}});run(world,30);
  require(get(world,attacker).position==before,"Deployed GI walked");
  world.enqueue({world.tick()+1,0,4,CommandKind::AttackEntity,attacker,b});
  bool para=false;for(int i=0;i<30;++i){world.step();for(const auto& shot:world.shots())para|=shot.weaponId=="PARA";}
  require(para,"GI did not switch to secondary weapon");
  world.enqueue({world.tick()+1,0,5,CommandKind::Deploy,attacker});run(world,4);
  require(get(world,attacker).infantry->posture==InfantryPosture::Standing,"GI did not undeploy");
}
void obstacles(const DefinitionRegistry& base) {
  const auto defs=fixture(base,"[E2]\nFearless=yes\n");World world(defs);
  const auto a=world.spawn("E1",{}),b=world.spawn("E2",{3,0},1);
  TerrainSurface wall;wall.wallHeightLeptons=256;world.setTerrain({1,0},wall);
  world.enqueue({1,0,1,CommandKind::AttackEntity,a,b});run(world,100);
  require(get(world,b).hp==125,"InvisibleLow fired through a wall");
}
void production(const DefinitionRegistry& base) {
  const auto defs=fixture(base);World world(defs);
  const auto barracks=world.spawn("GAPILE",{});require(barracks!=0,"Barracks spawn failed");
  std::vector<EntityId> blockers;for(int i=0;i<3;++i)blockers.push_back(world.spawn("E1",{1,2}));
  for(auto id:blockers)require(id!=0,"Barracks exit fixture overlaps foundation");
  world.enqueue({1,0,1,CommandKind::Produce,barracks,0,{},"E1"});run(world,12);
  require(world.snapshot().size()==4 && get(world,barracks).queueSize==1 && get(world,barracks).factoryPhase==FactoryExitPhase::Blocked,"Blocked exit lost or duplicated production");
  world.enqueue({world.tick()+1,0,2,CommandKind::Move,blockers[0],0,{8,2}});run(world,70);
  require(world.snapshot().size()==5 && get(world,barracks).queueSize==0,"Barracks did not retry available exit");
  const auto paid=fixture(base,"","[Simulation]\nSandbox=no\nStartingCredits=500\n");
  for(const bool soviet:{false,true}) {
    World side(paid);const auto factory=side.spawn(soviet?"NAHAND":"GAPILE",{});
    side.enqueue({1,0,1,CommandKind::Move,factory,0,{5,4}});
    side.enqueue({1,0,2,CommandKind::Produce,factory,0,{},soviet?"E2":"E1"});run(side,220);
    require(side.credits()==(soviet?400:300),"Infantry production charged the wrong cost");
    const auto produced=side.snapshot();
    require(produced.size()==2 && produced.back().infantry && produced.back().cell==TileCoord{5,4},"Barracks production/rally failed");
  }
}
void proneAndDeath(const DefinitionRegistry& base) {
  const auto defs=fixture(base);World world(defs);
  const auto a=world.spawn("E1",{}),b=world.spawn("E2",{2,0},1);
  world.enqueue({1,0,1,CommandKind::AttackEntity,a,b});
  std::vector<int> damage;int hp=125;bool prone=false,corpse=false;
  for(int i=0;i<600;++i){world.step();for(const auto& e:world.snapshot())if(e.id==b){if(e.hp<hp)damage.push_back(hp-e.hp);hp=e.hp;prone|=e.infantry->posture==InfantryPosture::Prone;}corpse|=!world.infantryCorpses().empty();}
  require(damage.size()>2 && damage[0]==12 && damage[1]==8 && prone,"SA prone damage did not use 70% warhead modifier");
  require(corpse && world.occupants({2,0}).empty(),"Death did not release occupancy/play a corpse effect");
}
void crush(const DefinitionRegistry& base) {
  const auto defs=fixture(base);World world(defs);
  const auto tank=world.spawn("HTNK",{}),soldier=world.spawn("E2",{2,0},1);
  require(defs.unit("HTNK").crusher,"HTNK crush flag missing");
  world.enqueue({1,0,1,CommandKind::Move,tank,0,{4,0}});run(world,200);
  const auto all=world.snapshot();require(std::none_of(all.begin(),all.end(),[&](const auto& e){return e.id==soldier;}),"Moving tank did not crush enemy infantry");
}
void protectedInfantry(const DefinitionRegistry& base) {
  const auto defs=fixture(base);
  for(const bool deployed:{false,true}) {
    World world(defs);
    const auto tank=world.spawn("HTNK",{}),soldier=world.spawn("E1",{2,0},deployed?1:0);
    if(deployed) {world.enqueue({1,1,1,CommandKind::Deploy,soldier});run(world,16);}
    world.enqueue({world.tick()+1,0,1,CommandKind::Move,tank,0,{4,0}});run(world,250);
    require(get(world,soldier).hp==125,"Tank crushed friendly infantry or a deployed GI");
    require(get(world,tank).cell==TileCoord{4,0},"Tank failed to route around protected infantry");
  }
}
void deathDuringStride(const DefinitionRegistry& base) {
  const auto defs=fixture(base,"[M60]\nDamage=1000\n");World world(defs);
  const auto attacker=world.spawn("E1",{0,2}),victim=world.spawn("E2",{2,0},1);
  world.enqueue({1,1,1,CommandKind::Move,victim,0,{8,0}});
  world.enqueue({1,0,1,CommandKind::AttackEntity,attacker,victim});run(world,40);
  const auto all=world.snapshot();
  require(std::none_of(all.begin(),all.end(),[&](const auto& e){return e.id==victim;}),"Stride death fixture failed to kill its target");
  require(world.spawn("HTNK",{2,0}) && world.spawn("HTNK",{3,0}),"Dead infantry retained source/target reservations");
}
void ballisticMiss(const DefinitionRegistry& base) {
  const auto defs=fixture(base,"[AP]\nCellSpread=0\n[AMCV]\nStrength=5000\n[AudioVisual]\nGravity=1\n");World world(defs);
  const auto attacker=world.spawn("HTNK",{}),target=world.spawn("AMCV",{4,0},1);
  world.enqueue({1,0,1,CommandKind::AttackEntity,attacker,target});world.step();
  require(world.shots().size()==1,"Ballistic miss fixture failed to launch");
  world.enqueue({2,0,2,CommandKind::Stop,attacker});
  world.enqueue({2,1,1,CommandKind::Move,target,0,{10,0}});run(world,60);
  require(world.shots().empty() && get(world,target).hp==5000,"Zero-spread ballistic miss still damaged its former target");
}
void conscriptTiming(const DefinitionRegistry& base) {
  const auto defs=fixture(base,"[E1]\nFearless=yes\n");World world(defs);
  const auto a=world.spawn("E2",{}),b=world.spawn("E1",{2,0},1);
  world.enqueue({1,0,1,CommandKind::AttackEntity,a,b});
  Tick started=0;bool found=false;
  for(int i=0;i<30;++i) {
    world.step();const auto actor=get(world,a);
    if(actor.infantry->sequence=="FIREUP")started=actor.infantry->animationStarted;
    for(const auto& shot:world.shots())if(shot.sourceType=="E2") {
      require(shot.started==started+6,"Conscript FireUp=6 event was clamped/skipped");found=true;
    }
  }
  require(found,"Conscript did not fire");
}
void areaDamage(const DefinitionRegistry& base) {
  const auto defs=fixture(base,"[SA]\nCellSpread=1\nPercentAtMax=1\n[E2]\nFearless=yes\n");World world(defs);
  const auto a=world.spawn("E1",{});std::vector<EntityId> targets;
  for(int i=0;i<3;++i)targets.push_back(world.spawn("E2",{2,0},1));
  world.enqueue({1,0,1,CommandKind::AttackEntity,a,targets[0]});
  for(int i=0;i<40 && get(world,targets[0]).hp==125;++i)world.step();
  for(auto id:targets)require(get(world,id).hp==113,"Area damage did not hit each same-cell entity exactly once");
}
void renderRate(const DefinitionRegistry& base) {
  const auto defs=fixture(base);std::uint64_t expected=0;
  for(const int hz:{30,60,144}) {
    World world(defs);world.setGameSpeed({GameSpeedMode::Multiplayer,6});
    const auto a=world.spawn("E1",{}),b=world.spawn("E2",{2,0},1);
    world.enqueue({1,0,1,CommandKind::AttackEntity,a,b});
    for(int frame=1;frame<=hz*5;++frame) {
      const auto before=std::uint64_t(frame-1)*1000000/hz,after=std::uint64_t(frame)*1000000/hz;
      world.advance(after-before);
    }
    require(world.tick()==300,"Frame-rate fixture did not simulate equal time");
    if(expected)require(expected==world.stateHash(),"Render frequency changed infantry combat");else expected=world.stateHash();
  }
}
void replay(const DefinitionRegistry& base) {
  const auto defs=fixture(base);World a(defs),b(defs);
  for(auto* w:{&a,&b}) {auto id=w->spawn("E1",{});w->enqueue({1,0,1,CommandKind::Move,id,0,{7,3}});w->enqueue({180,0,2,CommandKind::Deploy,id});}
  for(int i=0;i<240;++i){a.step();b.step();require(a.stateHash()==b.stateHash(),"Infantry simulation is not deterministic");}
}
}
int main(int argc,char** argv) {
  try {
    require(argc==2,"Expected settings path");const auto defs=DefinitionRegistry::load(argv[1]);
    require(defs.unit("E1").unsupported.empty() && defs.unit("E2").unsupported.empty(),"Basic infantry is unsupported");
    require(defs.unit("E1").infantry->bodyFrames==372 && defs.unit("E2").infantry->bodyFrames==301,"Invalid body-frame count");
    require(defs.unit("E1").infantry->sequence("PRONE").frame(7,0,true)==128,"FacingMultiplier ignored");
    require(infantryFacing(0)==5 && infantryFacing(16384)==3 && infantryFacing(32768)==1 && infantryFacing(49152)==7,"SHP direction order is mirrored");
    conscriptTiming(defs);areaDamage(defs);renderRate(defs);
    occupancy(defs);fireAndDeploy(defs);obstacles(defs);production(defs);proneAndDeath(defs);crush(defs);replay(defs);
    protectedInfantry(defs);deathDuringStride(defs);ballisticMiss(defs);
    std::cout<<"Infantry regressions passed\n";
  } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
