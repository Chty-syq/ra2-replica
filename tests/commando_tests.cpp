#include "simulation/world.h"
#include "test_support.h"
#include <algorithm>
#include <iostream>
using namespace ra2;
namespace {
std::optional<EntitySnapshot> find(const World& w,EntityId id){for(const auto& e:w.snapshot())if(e.id==id)return e;return {};}
void run(World& w,int n){while(n-->0)w.step();}
void reach(World& w,Tick t){while(w.tick()<t)w.step();}
template<class P>void until(World& w,P p,const char* error,int limit=1600){for(int i=0;i<limit && !p();++i)w.step();require(p(),error);}
DefinitionRegistry fixture(const DefinitionRegistry& base,std::string extra={}) {
  auto r=base.rules(),a=base.art(),p=base.replica();
  for(const auto& [id,type]:base.units()){(void)type;r.overlay(IniDocument::parse("["+id+"]\nCanPassiveAquire=no\n"));}
  r.overlay(IniDocument::parse(extra));p.overlay(IniDocument::parse("[Simulation]\nProductionFramesOverride=4\n"));
  for(const auto& [id,anim]:base.effectAnimations())if(anim.frameCount>0)a.overlay(IniDocument::parse("["+id+"]\nEnd="+std::to_string(anim.frameCount-1)));
  return DefinitionRegistry::compile(r,a,p);
}
void command(World& w,CommandKind kind,EntityId actor,EntityId target=0,TileCoord cell={},std::uint32_t owner=0){require(w.enqueue({w.tick()+1,owner,w.tick()+1,kind,actor,target,cell}),"Command rejected");}
void definitions(const DefinitionRegistry& d) {
  for(const auto id:{"GHOST","TANY","SPY","CLEG"}){const auto& t=d.unit(id);if(t.support()==SupportStatus::Unsupported)for(auto& s:t.unsupported)std::cerr<<id<<": "<<s<<'\n';require(t.support()!=SupportStatus::Unsupported,"Commando unsupported");require(t.infantry->bodyFrames>100 && t.cost>=1000,"Missing native art/stats");require(d.sidebarUnit(t.visual.cameo,"Allied")->id==id,"Missing sidebar mapping");}
  require(d.unit("GHOST").techLevel==-1 && !d.unit("GHOST").arcticBody.empty(),"SEAL campaign rules or arctic art modified");
  require(d.unit("TANY").c4 && d.unit("SPY").spy && d.unit("CLEG").teleporter && !d.unit("CLEG").infantry->crushable,"Typed capabilities missing");
  require(d.weapon("NEUTRONRIFLE").temporal && d.weapon("MAKEUPKIT").disguise && d.weapon("MAKEUPKIT").supported,"Special weapons became bullets");
  require(d.weaponForTarget(d.unit("FV"),false,false,10)->temporal && d.weaponForTarget(d.unit("FV"),false,false,4)->id=="CRMP5","Native IFV modes missing");
  for(const auto id:{"WARPIN","WARPOUT","WARPAWAY"})require(d.effectAnimation(id).frameCount>0,"Missing native warp SHP");
}
void sabotage(const DefinitionRegistry& base) {
  for(const auto id:{"GHOST","TANY"}) {
    const auto d=fixture(base,"[GAPOWR]\nStrength=50000\n");World w(d);const auto actor=w.spawn(id,{0,0}),target=w.spawn("GAPOWR",{2,0},1);
    require(actor && target,"C4 setup failed");command(w,CommandKind::AttackEntity,actor,target);
    until(w,[&]{return find(w,target)->c4Detonates>0;},"C4 not planted");const auto due=find(w,target)->c4Detonates;
    require(due-w.tick()==27 && find(w,target)->hp==50000 && !find(w,target)->bomb && w.shots().empty(),"C4 used Ivan bomb or ordinary damage");
    command(w,CommandKind::Move,actor,0,{-5,-5});reach(w,due-1);require(find(w,target).has_value(),"C4 exploded early");w.step();require(!find(w,target) && find(w,actor),"C4 did not force destruction / consumed commando");
    World cancelled(d);const auto a=cancelled.spawn(id,{}),b=cancelled.spawn("GAPOWR",{8,0},1);command(cancelled,CommandKind::AttackEntity,a,b);run(cancelled,3);command(cancelled,CommandKind::Stop,a);run(cancelled,180);require(find(cancelled,b)->hp==50000 && !find(cancelled,b)->c4Detonates,"Cancelled approach planted C4");
  }
  const auto d=fixture(base,"[GAPOWR]\nCanC4=no\n");World w(d);const auto a=w.spawn("TANY",{}),b=w.spawn("GAPOWR",{1,0},1);command(w,CommandKind::AttackEntity,a,b);run(w,100);require(!find(w,b)->c4Detonates && find(w,b)->hp==750,"CanC4=no ignored");
}
void swim(const DefinitionRegistry& base) {
  const auto d=fixture(base);
  for(const auto id:{"GHOST","TANY"}) {
    World w(d);TerrainSurface water;water.land="Water";for(int x=1;x<6;++x)for(int y=-2;y<3;++y)w.setTerrain({x,y},water);
    require(!w.spawn("E1",{2,0}),"Ordinary infantry entered water");const auto a=w.spawn(id,{});command(w,CommandKind::Move,a,0,{3,0});bool wet=false;
    until(w,[&]{const auto e=find(w,a);wet|=e->infantry->sequence=="SWIM";return e->cell==TileCoord{3,0} && e->activity==Activity::Idle;},"Amphibious infantry failed to swim");
    w.step();require(wet && find(w,a)->infantry->sequence=="TREAD","Native swim/tread not used");command(w,CommandKind::Move,a,0,{7,0});until(w,[&]{return find(w,a)->cell==TileCoord{7,0};},"Swimmer failed to reach shore");
  }
}
void disguise(const DefinitionRegistry& base) {
  const auto d=fixture(base);World w(d);const auto spy=w.spawn("SPY",{}),copy=w.spawn("E2",{50,0},1);
  command(w,CommandKind::AttackEntity,spy,copy);w.step();require(find(w,spy)->spyDisguise=="E2" && find(w,copy)->hp==125 && w.shots().empty(),"MakeupKit range/type or harmlessness wrong");
  require(w.recognizes(spy,0) && !w.recognizes(spy,1),"Disguise did not conceal spy");const auto dog=w.spawn("DOG",{2,0},1);require(dog && w.recognizes(spy,1),"Dog did not recognize disguised spy");
  command(w,CommandKind::Move,dog,0,{20,0},1);run(w,240);require(!w.recognizes(spy,1),"Detection persisted beyond range");
  command(w,CommandKind::AttackGround,spy,0,{2,0});run(w,20);require(w.shots().empty(),"Spy fired ordinary weapon");
}
EntityId infiltrate(World& w,std::string_view building,TileCoord cell={2,0}) {
  const auto target=w.spawn(building,cell,1),spy=w.spawn("SPY",{cell.x-2,cell.y});require(target && spy,"Infiltration setup failed");command(w,CommandKind::EnterBuilding,spy,target);
  until(w,[&]{return !find(w,spy);},"Spy failed to enter/consume");require(find(w,target)->owner==1,"Spy captured the building");return target;
}
void espionage(const DefinitionRegistry& base) {
  const auto d=fixture(base);World money(d);infiltrate(money,"GAREFN");require(money.credits()==75000 && money.credits(1)==25000,"Refinery did not transfer half actual enemy credits");
  World power(d);const auto plant=infiltrate(power,"GAPOWR");(void)plant;const auto untilFrame=power.power(1).outageUntil;require(!power.power(1).online() && untilFrame-power.tick()==1000,"Native blackout duration wrong");reach(power,untilFrame-1);require(!power.power(1).online(),"Blackout ended early");power.step();require(power.power(1).online(),"Blackout did not expire");
  for(const auto factory:{"GAPILE","GAWEAP"}) {
    World w(d);infiltrate(w,factory);require(std::string_view(factory)=="GAPILE"?w.espionage().veteranInfantry:w.espionage().veteranVehicles,"Training bonus not granted");
    const auto f=w.spawn(factory,{10,0});Command c{w.tick()+1,0,w.tick()+1,CommandKind::Produce,f};c.type=std::string_view(factory)=="GAPILE"?"E1":"MTNK";require(w.enqueue(c),"Produce rejected");
    until(w,[&]{for(auto& e:w.snapshot())if(e.type==c.type && e.owner==0 && e.veteran)return true;return false;},"Infiltrated factory bonus did not train future units");
  }
  World tech(d);infiltrate(tech,"GATECH");require(tech.espionage().alliedTech,"Allied stolen technology flag missing");
  World friendly(d);const auto a=friendly.spawn("SPY",{}),b=friendly.spawn("GAPOWR",{2,0});command(friendly,CommandKind::EnterBuilding,a,b);run(friendly,100);require(find(friendly,a) && friendly.power().online(),"Friendly infiltration consumed spy or sabotaged own power");
}
void teleport(const DefinitionRegistry& base) {
  const auto d=fixture(base);World w(d);const auto a=w.spawn("CLEG",{});const auto start=find(w,a)->position;w.setBuildable({5,0},false);command(w,CommandKind::Move,a,0,{10,0});w.step();const auto e=*find(w,a);
  require(e.cell==TileCoord{10,0} && e.position.x-start.x==10000 && e.previous==e.position && e.chronoReady==54,"Chrono did not instantly jump with distance-derived delay");
  command(w,CommandKind::Move,a,0,{20,0});reach(w,e.chronoReady-1);require(find(w,a)->cell==TileCoord{10,0},"Moved during reforming");w.step();require(find(w,a)->cell==TileCoord{20,0} && find(w,a)->chronoReady>w.tick(),"Queued jump lost after recovery");
}
void temporal(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[MTNK]\nPrimary=TestHit\n[TestHit]\nDamage=100\nROF=1000\nRange=20\nProjectile=InvisibleLow\nSpeed=100\nWarhead=Super\n");World w(d);const auto a=w.spawn("CLEG",{}),victim=w.spawn("HTNK",{2,0},1);
  command(w,CommandKind::AttackEntity,a,victim);until(w,[&]{return find(w,victim)->frozen;},"Temporal lock not acquired");const auto frozen=*find(w,victim);require(frozen.hp==400 && frozen.warpRemaining==3992 && w.shots().empty(),"Temporal weapon dealt ordinary HP damage");
  const auto gun=w.spawn("MTNK",{-1,0});command(w,CommandKind::AttackEntity,gun,victim);command(w,CommandKind::Move,victim,0,{8,0},1);run(w,20);require(find(w,victim)->hp==400 && find(w,victim)->position==frozen.position,"Frozen victim moved or took ordinary damage");
  command(w,CommandKind::Stop,a);w.step();require(!find(w,victim)->frozen && find(w,victim)->hp==400,"Stop did not release target intact");
  const auto helper=w.spawn("CLEG",{0,1});command(w,CommandKind::AttackEntity,a,victim);run(w,10);command(w,CommandKind::AttackEntity,helper,victim);until(w,[&]{return find(w,helper)->activeWeapon=="NEUTRONRIFLE" && find(w,victim)->warpRemaining<3900;},"Helper failed to join");run(w,10);const auto before=find(w,victim)->warpRemaining;w.step();require(before-find(w,victim)->warpRemaining==16,"Helpers did not sum native WarpPerStep");
  until(w,[&]{return !find(w,victim);},"Target not erased");require(w.infantryCorpses().empty() && std::any_of(w.effects().begin(),w.effects().end(),[](auto& e){return e.animation=="WARPAWAY";}),"Erasure lacked native effect");
  const auto immune=fixture(base,"[HTNK]\nWarpable=no\n");World blocked(immune);const auto c=blocked.spawn("CLEG",{}),tank=blocked.spawn("HTNK",{2,0},1);command(blocked,CommandKind::AttackEntity,c,tank);run(blocked,100);require(!find(blocked,tank)->frozen && find(blocked,tank)->hp==400,"Warpable=no ignored");
}
void eraseCargoAndIfv(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[HTK]\nStrength=20\n[NeutronRifle]\nDamage=80\n");World w(d);const auto host=w.spawn("HTK",{2,0},1),passenger=w.spawn("IVAN",{2,1},1),a=w.spawn("CLEG",{});command(w,CommandKind::EnterTransport,passenger,host,{},1);until(w,[&]{return find(w,passenger)->containedBy==host;},"Cargo setup failed");command(w,CommandKind::AttackEntity,a,host);until(w,[&]{return !find(w,host);},"Transport not erased");require(!find(w,passenger) && std::none_of(w.effects().begin(),w.effects().end(),[](auto& e){return e.explosion;}),"Erased cargo exploded or escaped");
  World erasedTruck(d);const auto eraser=erasedTruck.spawn("CLEG",{}),truck=erasedTruck.spawn("DTRUCK",{3,0},1),witness=erasedTruck.spawn("HTNK",{3,1});
  command(erasedTruck,CommandKind::AttackEntity,eraser,truck);until(erasedTruck,[&]{return !find(erasedTruck,truck);},"Demolition truck not erased");
  require(find(erasedTruck,witness)->hp==400 && erasedTruck.radiationSites().empty() && std::none_of(erasedTruck.effects().begin(),erasedTruck.effects().end(),[](auto& e){return e.explosion;}),"Erasure triggered demolition death weapon / radiation");
  World parasite(d);const auto parasitized=parasite.spawn("HTNK",{2,0},1),drone=parasite.spawn("DRON",{0,1}),legion=parasite.spawn("CLEG",{});
  command(parasite,CommandKind::AttackEntity,drone,parasitized);until(parasite,[&]{return find(parasite,parasitized)->infestedBy==drone;},"Infestation setup failed");
  command(parasite,CommandKind::AttackEntity,legion,parasitized);until(parasite,[&]{return !find(parasite,parasitized);},"Infested host not erased");require(!find(parasite,drone),"Parasite escaped temporally erased host");
  World ifv(d);const auto vehicle=ifv.spawn("FV",{2,0}),crew=ifv.spawn("CLEG",{}),victim=ifv.spawn("HTNK",{4,0},1);command(ifv,CommandKind::EnterTransport,crew,vehicle);until(ifv,[&]{return find(ifv,crew)->containedBy==vehicle;},"Chrono infantry failed to board");require(find(ifv,vehicle)->cargo->mode==10,"Chrono IFV mode incorrect");command(ifv,CommandKind::AttackEntity,vehicle,victim);until(ifv,[&]{return find(ifv,victim)->frozen;},"Chrono IFV did not use temporal device");command(ifv,CommandKind::Unload,vehicle);until(ifv,[&]{return !find(ifv,crew)->containedBy;},"Chrono crew failed to unload");run(ifv,2);require(!find(ifv,victim)->frozen,"Unloaded IFV retained stale temporal device");
}
void interruptionsAndTraining(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[MTNK]\nPrimary=TestHit\n[TestHit]\nDamage=100000\nROF=1000\nRange=20\nProjectile=InvisibleLow\nSpeed=100\nWarhead=Super\n");
  World killed(d);const auto chrono=killed.spawn("CLEG",{}),victim=killed.spawn("HTNK",{3,0},1);
  command(killed,CommandKind::AttackEntity,chrono,victim);until(killed,[&]{return find(killed,victim)->frozen;},"Lock setup failed");
  const auto killer=killed.spawn("MTNK",{-2,0},1);command(killed,CommandKind::AttackEntity,killer,chrono,{},1);
  until(killed,[&]{return !find(killed,chrono);},"Temporal source not killed");require(!find(killed,victim)->frozen && find(killed,victim)->hp==400,"Dead temporal source retained lock or damaged victim");
  World construction(d);const auto freezer=construction.spawn("CLEG",{}),site=construction.spawn("GAPOWR",{3,0},1,true);
  command(construction,CommandKind::AttackEntity,freezer,site);until(construction,[&]{return find(construction,site)->frozen;},"Construction lock failed");run(construction,100);command(construction,CommandKind::Stop,freezer);construction.step();
  require(find(construction,site)->activity==Activity::Constructing,"Frozen construction finished while time was stopped");run(construction,54);require(find(construction,site)->activity==Activity::Idle,"Released construction did not resume");
  const auto trainedDefs=fixture(base,"[E2]\nStrength=10000\n[E1]\nFearless=yes\n");World training(trainedDefs);infiltrate(training,"GAPILE");const auto factory=training.spawn("GAPILE",{10,0});
  Command order{training.tick()+1,0,training.tick()+1,CommandKind::Produce,factory};order.type="E1";training.enqueue(order);EntityId veteran=0;
  until(training,[&]{for(auto& e:training.snapshot())if(e.type=="E1" && e.owner==0 && e.veteran){veteran=e.id;return true;}return false;},"Veteran not produced");
  run(training,150);const auto p=find(training,veteran)->cell;const auto target=training.spawn("E2",{p.x+2,p.y},1);command(training,CommandKind::Stop,veteran);training.step();command(training,CommandKind::AttackEntity,veteran,target);
  until(training,[&]{return find(training,target)->hp<10000;},"Veteran infantry did not fire");require(find(training,target)->hp==9987,"Spy training firepower multiplier not applied");
}
void replay(const DefinitionRegistry& base) {
 const auto d=fixture(base);World a(d),b(d);for(auto* w:{&a,&b}){const auto c=w->spawn("CLEG",{}),t=w->spawn("HTNK",{3,0},1);command(*w,CommandKind::AttackEntity,c,t);w->spawn("SPY",{5,5});}
 for(int i=0;i<600;++i){a.step();b.step();require(a.stateHash()==b.stateHash(),"Special infantry replay diverged");}
}
}
int main(int argc,char** argv){try{require(argc==2,"settings argument required");const auto d=DefinitionRegistry::load(argv[1]);definitions(d);sabotage(d);swim(d);disguise(d);espionage(d);teleport(d);temporal(d);eraseCargoAndIfv(d);interruptionsAndTraining(d);replay(d);std::cout<<"Commando regressions passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
