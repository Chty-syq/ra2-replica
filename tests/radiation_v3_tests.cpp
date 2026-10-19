#include "simulation/world.h"
#include "simulation/missile.h"
#include "test_support.h"
#include <algorithm>
#include <iostream>
using namespace ra2;
namespace {
std::optional<EntitySnapshot> find(const World& w,EntityId id){for(const auto& e:w.snapshot())if(e.id==id)return e;return {};}
void run(World& w,int n){while(n-->0)w.step();}
template<class P> void until(World& w,P p,const char* message,int limit=1600){for(int i=0;i<limit && !p();++i)w.step();require(p(),message);}
std::optional<EntitySnapshot> rocket(const World& w){for(const auto& e:w.snapshot())if(e.missile)return e;return {};}
DefinitionRegistry fixture(const DefinitionRegistry& base,std::string extra={},std::string profile={}) {
  auto rules=base.rules(),replica=base.replica(),art=base.art();
  rules.overlay(IniDocument::parse("[DESO]\nCanPassiveAquire=no\n[V3]\nCanPassiveAquire=no\n[HTNK]\nCanPassiveAquire=no\nStrength=100000\n[FV]\nCanPassiveAquire=no\n[E2]\nCanPassiveAquire=no\nFearless=yes\n[JUMPJET]\nCanPassiveAquire=no\n[SNIPE]\nCanPassiveAquire=no\n[FLAKT]\nCanPassiveAquire=no\n[CombatDamage]\nBallisticScatter=0\n"+extra));
  replica.overlay(IniDocument::parse("[Simulation]\nProductionFramesOverride=4\n"+profile));
  for(const auto id:{"ELECTRO","NUKEDIE","V3TAKOFF","V3TRAIL","TWLT070"})art.overlay(IniDocument::parse(std::string("[")+id+"]\nEnd="+std::to_string(base.effectAnimation(id).frameCount-1)));
  return DefinitionRegistry::compile(rules,art,replica);
}
void definitions(const DefinitionRegistry& d) {
  const auto& deso=d.unit("DESO");const auto& v3=d.unit("V3");
  require(deso.unsupported.empty() && v3.unsupported.empty() && d.unit("V3ROCKET").missileSpawn,"New types still disabled");
  require(deso.infantry->bodyFrames==322 && deso.infantry->deployer && !deso.infantry->crushable && deso.deployFire && deso.immuneToRadiation,"Native desolator capability lost");
  require(d.weapon("RADBEAMWEAPON").radiationBeam && d.weapon("RADERUPTIONWEAPON").areaFire && d.weapon("RADERUPTIONWEAPON").radiationLevel==500,"Radiation weapons not compiled");
  require(d.weaponForTarget(deso,false)->id=="RADBEAMWEAPON" && !d.weaponForTarget(deso,true),"Radiation target domain mismatch");
  require(v3.missileLauncher && v3.missileLauncher->regenerateFrames==400 && d.weapon("V3LAUNCHER").spawner && d.weapon("V3LAUNCHER").minimumRangeMilliCells==5000 && d.weapon("V3LAUNCHER").rangeMilliCells==18000,"V3 launcher timing/range lost");
  require(d.weapon(d.missile().impactWeapon).damage==200 && d.warhead("V3WH").spreadMilliCells==1000,"V3 impact does not use native warhead/damage");
  require(d.unit("FV").gunnerTurrets.at(9)==3 && d.weaponForTarget(d.unit("FV"),false,false,9)->id=="CRRADBEAMWEAPON","Radiation IFV mapping lost");
  require(d.sidebarUnit("desoicon","Soviet")->id=="DESO" && d.sidebarUnit("v3icon","Soviet")->id=="V3","New cameos resolve wrong type");
  for(const auto field:{"[Radiation]\nRadApplicationDelay=0","[Radiation]\nRadDurationMultiple=0","[Radiation]\nRadColor=0,256,0","[General]\nV3RocketTiltFrames=0","[General]\nV3RocketAcceleration=0"}) {
    auto rules=d.rules();rules.overlay(IniDocument::parse(field));bool rejected=false;try{(void)DefinitionRegistry::compile(rules,d.art(),d.replica());}catch(const std::runtime_error&){rejected=true;}require(rejected,"Invalid radiation/missile profile accepted");
  }
}
void fieldRules(const DefinitionRegistry& d) {
  radiation::Field field;const auto& r=d.radiation();field.refresh({},10000,500,0,r);
  require(field.level({},0,r)==500 && field.level({5,0},0,r)==250 && field.level({10,0},0,r)==0,"Radiation distance falloff wrong");
  require(field.level({},89,r)==500 && field.level({},90,r)==400 && field.level({},180,r)==300 && field.level({},450,r)==0,"Radiation level stepping wrong");
  field.refresh({},10000,500,200,r);require(field.sites().size()==1 && field.level({},200,r)==500,"Deployed refresh stacked identical site");
  field.refresh({1,0},10000,500,200,r);require(field.sites().size()==2 && field.level({},200,r)==r.levelMax,"Overlapping radiation exceeds damage cap");
  field.expire(699,r);require(field.sites().size()==2,"Radiation expired early");field.expire(700,r);require(field.sites().empty(),"Radiation never expired");
}
void production(const DefinitionRegistry& base) {
  for(const bool vehicle:{false,true}) {
    const auto d=fixture(base,{},std::string("[Simulation]\nSandbox=no\nStartingCredits=2000\nPlayerHouse=")+(vehicle?"Russians":"Arabs"));World w(d);
    const auto factory=w.spawn(vehicle?"NAWEAP":"NAHAND",{});const auto type=vehicle?"V3":"DESO";
    w.enqueue({1,0,1,CommandKind::Produce,factory,0,{},type});run(w,10);require(w.credits()==2000,"Radar prerequisite bypassed");
    require(w.spawn("NARADR",{8,0}),"Radar setup failed");w.enqueue({w.tick()+1,0,2,CommandKind::Produce,factory,0,{},type});run(w,200);
    int count=0;for(const auto& e:w.snapshot())count+=e.type==type;require(count==1 && w.credits()==(vehicle?1200:1400),"Production, cost or factory exit failed");
  }
  const auto d=fixture(base,{},"[Simulation]\nSandbox=no\nPlayerHouse=Russians\nStartingCredits=2000\n");World w(d);const auto factory=w.spawn("NAHAND",{});w.spawn("NARADR",{8,0});w.enqueue({1,0,1,CommandKind::Produce,factory,0,{},"DESO"});run(w,50);
  require(w.credits()==2000 && w.snapshot().size()==2,"Iraq restriction bypassed");require(!w.spawn("V3ROCKET",{20,0}),"Spawned missile created as a regular map vehicle");
}
void radiationCombat(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[E2]\nStrength=100000\n");World w(d);const auto gun=w.spawn("DESO",{}),target=w.spawn("E2",{3,0},1);
  w.enqueue({1,0,1,CommandKind::AttackEntity,gun,target});std::vector<Tick> times;
  for(int i=0;i<190;++i){w.step();for(const auto& b:w.beams())if(b.started==w.tick()) {
    require(b.weapon=="RADBEAMWEAPON" && b.started==find(w,gun)->infantry->animationStarted+2,"Radiation shot event/beam wrong");times.push_back(b.started);
  }}
  require(times.size()>=3 && times[1]-times[0]==50 && find(w,target)->hp==100000-int(times.size())*125,"Radiation beam ROF/damage wrong");
  for(const auto type:{"DESO","JUMPJET","GAPOWR"}) {
    World invalid(d);const auto shooter=invalid.spawn("DESO",{}),victim=invalid.spawn(type,{3,0},1);require(victim,"Radiation immunity fixture failed");
    invalid.enqueue({1,0,1,CommandKind::AttackEntity,shooter,victim});run(invalid,150);require(find(invalid,victim)->hp==find(invalid,victim)->maxHp && invalid.beams().empty(),"Radiation hit immune/air/building target");
  }
  const auto lethal=fixture(base);World death(lethal);const auto deso=death.spawn("DESO",{}),victim=death.spawn("E2",{3,0},1);death.enqueue({1,0,1,CommandKind::AttackEntity,deso,victim});
  until(death,[&]{return !find(death,victim);},"Radiation failed to kill infantry");require(death.infantryCorpses().empty() && std::any_of(death.effects().begin(),death.effects().end(),[](const auto& e){return e.animation=="NUKEDIE";}),"Radiation did not use native melting death");
}
void deployment(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[E2]\nStrength=100000\n[JUMPJET]\nStrength=100000\n");World w(d);const auto deso=w.spawn("DESO",{}),enemy=w.spawn("E2",{1,0},1),ally=w.spawn("E2",{0,1}),immune=w.spawn("DESO",{-1,0}),air=w.spawn("JUMPJET",{1,0},1),building=w.spawn("GAPOWR",{3,0});
  w.enqueue({1,0,1,CommandKind::Deploy,deso});until(w,[&]{return !w.radiationSites().empty();},"Deploy did not create radiation");
  require(find(w,deso)->infantry->posture==InfantryPosture::Deployed && w.radiationLevel({})==500,"Radiation fired before deployment/with wrong level");
  const auto firstPulse=w.radiationSites().front().started;
  run(w,140);require(w.radiationSites().size()==1 && find(w,deso)->hp==150 && find(w,immune)->hp==150,"Radiation refresh stacked or hurt immune trooper");
  require(w.radiationSites().front().started==firstPulse,"Deployed desolator renewed radiation above one-third threshold");
  until(w,[&]{return w.radiationSites().front().started>firstPulse;},"Deployed desolator never renewed weakened radiation");
  require(find(w,enemy)->hp<100000 && find(w,ally)->hp<100000 && find(w,air)->hp==100000 && find(w,building)->hp==find(w,building)->maxHp,"Radiation friendly damage/immunity/layer filtering wrong");
  const auto last=w.radiationSites().front().started;w.enqueue({w.tick()+1,0,2,CommandKind::Move,deso,0,{12,0}});
  until(w,[&]{return find(w,deso)->cell==TileCoord{12,0};},"Deployed move failed to undeploy and walk");
  require(find(w,deso)->infantry->posture==InfantryPosture::Standing && (w.radiationSites().empty() || w.radiationSites().front().started==last),"Walking continued renewing radiation");
  run(w,600);require(w.radiationSites().empty() && !w.radiationLevel({}),"Radiation survived its decay lifetime");
  World queued(d);const auto actor=queued.spawn("DESO",{});queued.enqueue({1,0,1,CommandKind::Deploy,actor});queued.enqueue({4,0,2,CommandKind::Move,actor,0,{2,0}});run(queued,180);
  require(find(queued,actor)->cell==TileCoord{2,0} && find(queued,actor)->infantry->posture==InfantryPosture::Standing && queued.radiationSites().empty(),"Move during deploy got stuck or irradiated destination");
  World dead(d);const auto victim=dead.spawn("DESO",{});dead.enqueue({1,0,1,CommandKind::Deploy,victim});until(dead,[&]{return !dead.radiationSites().empty();},"Dead site fixture failed");
  const auto killer=dead.spawn("SNIPE",{12,0},1);dead.enqueue({dead.tick()+1,1,1,CommandKind::AttackEntity,killer,victim});until(dead,[&]{return !find(dead,victim);},"Could not kill deployed desolator");
  require(!dead.radiationSites().empty(),"Dead desolator instantly removed residual radiation");run(dead,600);require(dead.radiationSites().empty(),"Dead desolator radiation never decayed");
}
void ifv(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[E2]\nStrength=100000\n");World w(d);const auto host=w.spawn("FV",{2,0}),foot=w.spawn("DESO",{}),target=w.spawn("E2",{5,0},1);
  w.enqueue({1,0,1,CommandKind::EnterTransport,foot,host});run(w,180);require(find(w,foot)->containedBy==host && find(w,host)->cargo->mode==9 && find(w,host)->cargo->turret==3,"Radiation IFV passenger/turret wrong");
  w.enqueue({w.tick()+1,0,2,CommandKind::AttackEntity,host,target});bool beam=false;for(int i=0;i<130;++i){w.step();for(const auto& b:w.beams())beam|=b.weapon=="CRRADBEAMWEAPON";}
  require(beam && find(w,target)->hp<100000 && w.radiationSites().empty(),"Radiation IFV failed to fire or deployed radiation incorrectly");
  w.enqueue({w.tick()+1,0,3,CommandKind::Unload,host});run(w,50);require(!find(w,foot)->containedBy && find(w,host)->cargo->mode==0,"Radiation IFV unload did not restore empty weapon");
}
void trajectories(const DefinitionRegistry& d) {
  for(const int cells:{5,10,18}) {
    missile::State s;s.position=s.previous={0,0,75*motion::lepton};s.target={std::int64_t(cells)*1000*motion::scale,0,0};s.pitch=d.missile().initialPitch;
    bool impact=false,climb=false,cruise=false,dive=false;for(Tick frame=0;frame<800 && !impact;++frame) {
      const auto step=missile::advance(s,d.missile(),15,frame);if(frame<60)require(!s.launched && s.position.x==0,"V3 moved before tilt completed");
      if(step.takeoff)require(frame==60,"Native V3 tilt duration changed");
      if(s.launched){require(motion::length(s.position-s.previous)<=std::int64_t(39)*motion::lepton,"Rocket teleported during curve/descent");climb|=s.phase==missile::Phase::Climbing;cruise|=s.phase==missile::Phase::Cruising;dive|=s.phase==missile::Phase::Diving;}
      impact=step.impact;
    }
    require(impact && climb && cruise && dive && s.position==s.target,"V3 trajectory never reached fixed target");
  }
}
void launcher(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[GAPOWR]\nStrength=10000\n");World w(d);const auto gun=w.spawn("V3",{}),target=w.spawn("GAPOWR",{10,0},1);w.enqueue({1,0,1,CommandKind::AttackEntity,gun,target});run(w,1);
  require(rocket(w) && rocket(w)->takingOff && find(w,gun)->launcherEmpty && find(w,target)->hp==10000,"V3 did not create separate preparing missile");const auto missileId=rocket(w)->id;
  require(w.occupant({})==gun && w.occupant(rocket(w)->cell)!=missileId && w.airOccupants(rocket(w)->cell).empty(),"Spawned rocket occupied ground or infantry air slot");
  run(w,59);require(find(w,missileId)->takingOff,"V3 launched before 60 frame tilt");run(w,1);require(!find(w,missileId)->takingOff && find(w,gun)->launcherReady==461,"V3 launch/reload timer wrong");
  w.enqueue({w.tick()+1,0,2,CommandKind::Stop,gun});until(w,[&]{return !find(w,missileId);},"V3 missile never impacted");
  require(find(w,target)->hp==9800,"V3 used launcher Damage=1 instead of actual rocket damage");
  while(w.tick()<460)w.step();require(find(w,gun)->launcherEmpty,"V3 ammo regenerated early");w.step();require(!find(w,gun)->launcherEmpty,"V3 never regenerated loaded body");
  World cancel(d);const auto launcher=cancel.spawn("V3",{}),victim=cancel.spawn("GAPOWR",{10,0},1);cancel.enqueue({1,0,1,CommandKind::AttackEntity,launcher,victim});cancel.enqueue({20,0,2,CommandKind::Move,launcher,0,{0,2}});run(cancel,220);
  require(!rocket(cancel) && !find(cancel,launcher)->launcherEmpty && find(cancel,victim)->hp==10000,"Cancelled V3 launch retained missile/ammo/damage");
  World retry(d);const auto actor=retry.spawn("V3",{}),destination=retry.spawn("GAPOWR",{10,0},1);retry.enqueue({1,0,1,CommandKind::AttackEntity,actor,destination});run(retry,10);const auto cancelledId=rocket(retry)->id;
  retry.enqueue({11,0,2,CommandKind::Stop,actor});run(retry,1);retry.enqueue({12,0,3,CommandKind::AttackEntity,actor,destination});run(retry,1);
  require(rocket(retry) && rocket(retry)->id!=cancelledId && rocket(retry)->takingOff,"Cancelled V3 could not immediately prepare another launch");
  World minimum(d);const auto close=minimum.spawn("V3",{}),near=minimum.spawn("GAPOWR",{4,0},1);minimum.enqueue({1,0,1,CommandKind::AttackEntity,close,near});run(minimum,150);require(!rocket(minimum) && find(minimum,near)->hp==10000,"V3 ignored minimum range");
  const auto fast=fixture(base,"[HTNK]\nSpeed=100\n");World moving(fast);const auto source=moving.spawn("V3",{}),runner=moving.spawn("HTNK",{18,0},1);
  moving.enqueue({1,0,1,CommandKind::AttackEntity,source,runner});moving.enqueue({20,1,1,CommandKind::Move,runner,0,{24,0}});run(moving,60);
  require(rocket(moving) && rocket(moving)->takingOff && find(moving,source)->cell==TileCoord{},"Preparing V3 chased moving target and detached its missile");
  run(moving,1);const auto projectile=rocket(moving)->id;moving.enqueue({62,0,2,CommandKind::Stop,source});until(moving,[&]{return !find(moving,projectile);},"Moving-target missile got stuck");
  require(find(moving,runner)->hp==100000,"V3 impact followed target instead of fixed launch coordinates");
}
void interception(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[FLAKT]\nCanPassiveAquire=yes\n[FlakGuyGun]\nRange=.01\n[FlakGuyAAGun]\nDamage=100000\n[GAPOWR]\nStrength=10000\n[120mm]\nDamage=100000\n");
  World w(d);const auto gun=w.spawn("V3",{}),target=w.spawn("GAPOWR",{10,0},1);w.spawn("FLAKT",{5,0},1);w.enqueue({1,0,1,CommandKind::AttackEntity,gun,target});run(w,1);const auto id=rocket(w)->id;bool aa=false;
  for(int i=0;i<350 && find(w,id);++i){w.step();for(const auto& shot:w.shots())aa|=shot.weaponId=="FLAKGUYAAGUN";}
  require(aa && !find(w,id) && find(w,target)->hp==10000 && find(w,gun)->launcherEmpty,"AA did not intercept V3 or intercepted rocket damaged ground target");
  World orphan(d);const auto parent=orphan.spawn("V3",{}),victim=orphan.spawn("GAPOWR",{10,0},1);orphan.enqueue({1,0,1,CommandKind::AttackEntity,parent,victim});run(orphan,65);const auto child=rocket(orphan)->id;
  const auto killer=orphan.spawn("HTNK",{1,0},1);orphan.enqueue({orphan.tick()+1,1,1,CommandKind::AttackEntity,killer,parent});until(orphan,[&]{return !find(orphan,parent);},"V3 parent destruction fixture failed");
  require(find(orphan,child).has_value(),"Flying rocket was deleted with its parent");until(orphan,[&]{return !find(orphan,child);},"Orphan rocket got stuck");require(find(orphan,victim)->hp==9800,"Orphan rocket lost its impact");
}
void replay(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[E2]\nStrength=100000\n");std::uint64_t expected=0;
  for(const int hz:{30,60,144}){World w(d);w.setGameSpeed({GameSpeedMode::Multiplayer,6});const auto deso=w.spawn("DESO",{}),v3=w.spawn("V3",{0,12}),target=w.spawn("HTNK",{10,12},1);w.spawn("E2",{2,0},1);
    w.enqueue({1,0,1,CommandKind::Deploy,deso});w.enqueue({1,0,2,CommandKind::AttackEntity,v3,target});w.enqueue({200,0,3,CommandKind::Move,deso,0,{10,0}});
    for(int f=1;f<=hz*8;++f)w.advance(std::uint64_t(f)*1000000/hz-std::uint64_t(f-1)*1000000/hz);
    require(w.tick()==480,"Radiation/V3 replay frame count wrong");if(expected)require(w.stateHash()==expected,"Radiation/V3 state depends on display rate");else expected=w.stateHash();}
}
}
int main(int argc,char** argv){try{require(argc==2,"Expected settings path");const auto d=DefinitionRegistry::load(argv[1]);definitions(d);fieldRules(d);production(d);radiationCombat(d);deployment(d);ifv(d);trajectories(d);launcher(d);interception(d);replay(d);std::cout<<"Radiation/V3 passed: production, beams, deployment, persistent sites, immunity, IFV, missile tilt/curve/reload/interception/cancellation and replay\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
