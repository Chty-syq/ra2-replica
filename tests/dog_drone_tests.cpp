#include "simulation/world.h"
#include "simulation/leap.h"
#include "test_support.h"
#include <algorithm>
#include <iostream>
using namespace ra2;
namespace {
std::optional<EntitySnapshot> find(const World& w,EntityId id){for(const auto& e:w.snapshot())if(e.id==id)return e;return {};}
void run(World& w,int n){while(n-->0)w.step();}
template<class P>void until(World& w,P p,const char* message,int limit=900){for(int i=0;i<limit && !p();++i)w.step();require(p(),message);}
DefinitionRegistry fixture(const DefinitionRegistry& d,std::string extra={},std::string profile={}) {
  auto r=d.rules(),a=d.art(),p=d.replica();
  for(const auto id:{"DOG","ADOG","DRON","HTNK","MTNK","FV","E1","E2","JUMPJET","SREF"})r.overlay(IniDocument::parse(std::string("[")+id+"]\nCanPassiveAquire=no\n"));
  r.overlay(IniDocument::parse(extra));p.overlay(IniDocument::parse("[Simulation]\nProductionFramesOverride=4\n"+profile));
  for(const auto& [id,anim]:d.effectAnimations())if(anim.frameCount>0)a.overlay(IniDocument::parse("["+id+"]\nEnd="+std::to_string(anim.frameCount-1)));
  return DefinitionRegistry::compile(r,a,p);
}
void definitions(const DefinitionRegistry& d) {
  for(const auto id:{"DOG","ADOG","DRON"})require(d.unit(id).unsupported.empty() && d.unit(id).leap && d.weapon(d.unit(id).primary).leap,"Leap type disabled");
  require(d.unit("DOG").infantry->bodyFrames==172 && d.unit("ADOG").infantry->bodyFrames==172,"Native dog sequences not validated");
  require(!d.unit("DRON").infantry && d.unit("DRON").spriteVehicle->bodyFrames==88 && !d.unit("DRON").parasiteable,"Sprite vehicle became foot infantry or native immunity lost");
  require(d.unit("DRON").spriteVehicle->sequence("READY").start==48 && d.unit("DRON").spriteVehicle->sequence("FIREUP").start==56,"Drone sprite offsets wrong");
  require(d.weapon("VIRTUALSCANNER").neverUse && !d.weapon("VIRTUALSCANNER").supported && d.unit("DOG").leap->scanRange==5000,"Scanner became a damage weapon");
  require(d.sidebarUnit("adogicon","Allied")->id=="ADOG" && d.sidebarUnit("dogicon","Soviet")->id=="DOG" && d.sidebarUnit("dronicon","Soviet")->id=="DRON","New sidebar mapping wrong");
}
void dogCombat(const DefinitionRegistry& base) {
  const auto d=fixture(base);
  for(const auto type:{"DOG","ADOG","DRON"}) {
    World w(d);const auto dog=w.spawn(type,{}),victim=w.spawn("E2",{1,0},1);require(dog && victim,"Leap setup failed");
    w.enqueue({1,0,1,CommandKind::AttackEntity,dog,victim});bool airborne=false;int peak=0;
    until(w,[&]{const auto u=find(w,dog);if(u && u->leaping){airborne=true;peak=std::max(peak,u->heightLeptons);require(w.occupant({})!=dog,"Jump left source occupied");}
      return !find(w,victim) && u && !u->leaping;},"Leap did not kill infantry and return");
    require(airborne && peak>0 && find(w,dog)->hp==100 && w.shots().empty(),"Leap was replaced by ordinary bullet damage");
    require(!w.infantryCorpses().empty() && w.infantryCorpses().back().type=="E2","Leap victim has no native death animation");
    w.enqueue({w.tick()+1,0,2,CommandKind::Move,dog,0,{4,2}});until(w,[&]{return find(w,dog)->cell==TileCoord{4,2};},"Returned attacker could not move");
  }
  for(const auto type:{"HTNK","GAPOWR","JUMPJET","DRON"}) {
    World w(d);const auto actor=w.spawn("DOG",{}),target=w.spawn(type,{1,0},1);require(target,"Target fixture failed");
    w.enqueue({1,0,1,CommandKind::AttackEntity,actor,target});run(w,140);
    require(find(w,target)->hp==find(w,target)->maxHp && !find(w,actor)->leaping && w.shots().empty(),"Dog attacked a forbidden target");
  }
  const auto autoDefs=fixture(base,"[DOG]\nCanPassiveAquire=yes\n");World guard(autoDefs);const auto dog=guard.spawn("DOG",{}),victim=guard.spawn("E2",{4,0},1);
  until(guard,[&]{return !find(guard,victim);},"Virtual scanner did not chase infantry beyond bite range");require(find(guard,dog).has_value(),"Guard dog disappeared");
  World outside(autoDefs);const auto idle=outside.spawn("DOG",{});outside.spawn("E2",{7,0},1);run(outside,150);require(find(outside,idle)->cell==TileCoord{},"Scanner acquired outside native range");
}
void infestation(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[HTNK]\nStrength=1000\n");World w(d);const auto drone=w.spawn("DRON",{}),host=w.spawn("HTNK",{1,0},1);
  w.enqueue({1,0,1,CommandKind::AttackEntity,drone,host});until(w,[&]{return find(w,drone)->parasiteHost==host;},"Drone did not enter vehicle");
  const auto entered=w.tick();require(find(w,host)->hp==1000 && find(w,host)->infestedBy==drone,"Infestation hit early or reverse link missing");
  require(w.occupant({})!=drone && w.occupant({1,0})==host,"Hidden drone occupied map");
  w.enqueue({w.tick()+1,1,1,CommandKind::Move,host,0,{5,0}});run(w,59);
  require(find(w,host)->hp==1000 && find(w,host)->position==find(w,drone)->position,"Parasite damaged early or failed to follow host");
  w.step();require(w.tick()==entered+60 && find(w,host)->hp==950,"Native DroneJump damage/ROF lost");run(w,120);require(find(w,host)->hp==850,"Parasite damage did not repeat");
  w.enqueue({w.tick()+1,0,2,CommandKind::Move,drone,0,{20,20}});run(w,8);require(find(w,drone)->parasiteHost==host,"Command pulled drone out of vehicle");
  const auto lethal=fixture(base,"[HTNK]\nStrength=100\n");World death(lethal);const auto survivor=death.spawn("DRON",{}),victim=death.spawn("HTNK",{1,0},1);
  death.enqueue({1,0,1,CommandKind::AttackEntity,survivor,victim});until(death,[&]{return !find(death,victim);},"Infested host never died");
  require(find(death,survivor) && !find(death,survivor)->parasiteHost && !find(death,survivor)->leaping && death.occupant(find(death,survivor)->cell)==survivor,"Surviving drone did not regain vehicle occupancy");
  World race(d);const auto a=race.spawn("DRON",{}),b=race.spawn("DRON",{1,1}),target=race.spawn("HTNK",{1,0},1);
  race.enqueue({1,0,1,CommandKind::AttackEntity,a,target});race.enqueue({1,0,2,CommandKind::AttackEntity,b,target});run(race,100);
  int inside=0;for(const auto& e:race.snapshot())inside+=e.parasiteHost==target;require(inside==1 && find(race,target)->hp==950,"Multiple drones entered the same host");
}
void cancellation(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[SNIPE]\nCanPassiveAquire=no\n");World w(d);const auto dog=w.spawn("DOG",{}),victim=w.spawn("E2",{1,0},1),sniper=w.spawn("SNIPE",{4,0});
  w.enqueue({1,0,1,CommandKind::AttackEntity,dog,victim});until(w,[&]{return find(w,dog)->leaping;},"Cancellation setup did not jump");
  w.enqueue({w.tick()+1,0,2,CommandKind::AttackEntity,sniper,victim});run(w,100);require(find(w,dog) && !find(w,dog)->leaping,"Lost victim stranded dog in limbo");
  const auto walls=fixture(base,"[BadTeeth]\nRange=2.5\n");World wall(walls);const auto jumper=wall.spawn("DOG",{}),target=wall.spawn("E2",{2,0},1);
  wall.enqueue({1,0,1,CommandKind::AttackEntity,jumper,target});until(wall,[&]{return find(wall,jumper)->leaping;},"Wall fixture did not jump");
  auto terrain=TerrainSurface{};terrain.wallHeightLeptons=256;wall.setTerrain({1,0},terrain);run(wall,100);
  require(find(wall,jumper) && !find(wall,jumper)->leaping && find(wall,target)->hp==find(wall,target)->maxHp,"Jump ignored wall or lost actor");
}
void repairAndTransport(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[HTNK]\nStrength=1000\n[SREF]\nROT=0\n[COMET]\nDamage=10\nROF=1000\n[COMETFRAGMENT]\nDamage=0\n[CometWH]\nVerses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%\n");
  World heal(d);const auto host=heal.spawn("HTNK",{}),drone=heal.spawn("DRON",{1,0},1),ifv=heal.spawn("FV",{0,2}),engineer=heal.spawn("ENGINEER",{0,3});
  heal.enqueue({1,0,1,CommandKind::EnterTransport,engineer,ifv});run(heal,130);require(find(heal,engineer)->containedBy==ifv,"Repair IFV fixture failed");
  heal.enqueue({heal.tick()+1,1,1,CommandKind::AttackEntity,drone,host});until(heal,[&]{return find(heal,drone)->parasiteHost==host;},"Repair parasite fixture failed");
  require(find(heal,host)->hp==1000,"Host damaged before repair order");
  heal.enqueue({heal.tick()+1,0,2,CommandKind::AttackEntity,ifv,host});until(heal,[&]{return !find(heal,drone);},"Repair IFV did not remove parasite at full HP");
  require(!find(heal,host)->infestedBy && find(heal,host)->hp==1000,"Repair left stale infestation or damaged healthy host");
  World suppression(d);const auto parasite=suppression.spawn("DRON",{}),victim=suppression.spawn("HTNK",{1,0},1),gun=suppression.spawn("SREF",{4,0});
  suppression.enqueue({1,0,1,CommandKind::AttackEntity,parasite,victim});until(suppression,[&]{return find(suppression,parasite)->parasiteHost==victim;},"Suppression fixture failed");
  run(suppression,42);suppression.enqueue({suppression.tick()+1,0,2,CommandKind::AttackEntity,gun,victim});until(suppression,[&]{return find(suppression,victim)->hp==990;},"Suppression weapon did not hit");
  run(suppression,49);require(find(suppression,victim)->hp==990,"Significant host damage did not suppress parasite");suppression.step();require(find(suppression,victim)->hp==940,"Parasite failed to resume after suppression");
  World cargo(d);const auto carrier=cargo.spawn("HTK",{2,0}),dog=cargo.spawn("DOG",{1,0}),robot=cargo.spawn("DRON",{0,1});
  cargo.enqueue({1,0,1,CommandKind::EnterTransport,dog,carrier});run(cargo,130);cargo.enqueue({cargo.tick()+1,0,2,CommandKind::EnterTransport,robot,carrier});
  until(cargo,[&]{return find(cargo,robot)->containedBy==carrier;},"Sprite vehicle could not board flak track");require(find(cargo,carrier)->cargo->used==3 && cargo.occupant({0,1})!=robot,"Drone size or boarding occupancy wrong");
  cargo.enqueue({cargo.tick()+1,0,3,CommandKind::Unload,carrier});until(cargo,[&]{return !find(cargo,dog)->containedBy && !find(cargo,robot)->containedBy;},"New passengers could not unload");
  require(cargo.occupant(find(cargo,robot)->cell)==robot,"Unloaded drone became an infantry subcell");
  World small(d);const auto fv=small.spawn("FV",{2,0}),large=small.spawn("DRON",{});small.enqueue({1,0,1,CommandKind::EnterTransport,large,fv});run(small,100);require(!find(small,large)->containedBy && find(small,fv)->cargo->used==0,"Drone bypassed IFV SizeLimit=1");
}
void production(const DefinitionRegistry& base) {
  for(const auto type:{"ADOG","DOG","DRON"}) {
    const bool allied=std::string(type)=="ADOG",vehicle=std::string(type)=="DRON";
    const auto d=fixture(base,{},std::string("[Simulation]\nSandbox=no\nStartingCredits=2000\nPlayerHouse=")+(allied?"British":"Russians"));World w(d);
    const auto factory=w.spawn(vehicle?"NAWEAP":allied?"GAPILE":"NAHAND",{});
    w.enqueue({1,0,1,CommandKind::Produce,factory,0,{},type});run(w,200);int count=0;for(const auto& e:w.snapshot())count+=e.type==type;
    require(count==1 && w.credits()==2000-d.unit(type).cost,"Native production, cost or exit failed");
  }
}
void replay(const DefinitionRegistry& base) {
  const auto d=fixture(base);World a(d),b(d);
  for(auto* w:{&a,&b}){const auto drone=w->spawn("DRON",{}),host=w->spawn("HTNK",{1,0},1);w->enqueue({1,0,1,CommandKind::AttackEntity,drone,host});}
  for(int i=0;i<700;++i){a.step();b.step();require(a.stateHash()==b.stateHash(),"Leap/parasite replay diverged");}
}
}
int main(int argc,char** argv)try{const auto d=DefinitionRegistry::load(argc>1?argv[1]:"assets/settings");definitions(d);dogCombat(d);infestation(d);cancellation(d);repairAndTransport(d);production(d);replay(d);std::cout<<"Dog/drone regressions passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
