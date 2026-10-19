#include "simulation/world.h"
#include "test_support.h"
#include <algorithm>
#include <iostream>
#include <set>
using namespace ra2;
namespace {
std::optional<EntitySnapshot> find(const World& w,EntityId id){for(const auto& e:w.snapshot())if(e.id==id)return e;return {};}
void run(World& w,int n){while(n-->0)w.step();}
template<class P>void until(World& w,P p,const char* message,int limit=1800){for(int i=0;i<limit && !p();++i)w.step();require(p(),message);}
void reach(World& w,Tick frame){while(w.tick()<frame)w.step();}
DefinitionRegistry fixture(const DefinitionRegistry& base,std::string extra={},std::string profile={}) {
  auto r=base.rules(),a=base.art(),p=base.replica();
  for(const auto id:{"IVAN","ENGINEER","SENGINEER","APOC","HTNK","MTNK","FV","HTK","E2","JUMPJET"})r.overlay(IniDocument::parse(std::string("[")+id+"]\nCanPassiveAquire=no\n"));
  r.overlay(IniDocument::parse("[HTNK]\nStrength=10000\n[E2]\nStrength=10000\nFearless=yes\n[MTNK]\nPrimary=TestHit\n[TestHit]\nDamage=100000\nRange=30\nROF=1000\nProjectile=InvisibleLow\nWarhead=TestHitWH\n[TestHitWH]\nVerses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%\n"+extra));
  p.overlay(IniDocument::parse("[Simulation]\nProductionFramesOverride=4\n"+profile));
  for(const auto& [id,anim]:base.effectAnimations())if(anim.frameCount>0)a.overlay(IniDocument::parse("["+id+"]\nEnd="+std::to_string(anim.frameCount-1)));
  return DefinitionRegistry::compile(r,a,p);
}
BombSnapshot plant(World& w,EntityId source,EntityId target,std::uint32_t owner=0) {
  require(source && target,"Bomb setup failed");w.enqueue({w.tick()+1,owner,w.tick()+1,CommandKind::AttackEntity,source,target});
  until(w,[&]{return find(w,target) && find(w,target)->bomb.has_value();},"Ivan failed to attach bomb");return *find(w,target)->bomb;
}
int effects(const World& w,std::string_view name){return std::count_if(w.effects().begin(),w.effects().end(),[&](const auto& e){return e.animation==name;});}
void definitions(const DefinitionRegistry& d) {
  const auto& ivan=d.unit("IVAN");const auto& apoc=d.unit("APOC");
  require(ivan.support()==SupportStatus::Ready && ivan.ivan && ivan.infantry->bodyFrames==267 && ivan.infantry->fireUp==6 && ivan.cost==600 && ivan.ifvMode==7,"Original Ivan definition/layout lost");
  require(d.weapon("IVANBOMBER").supported && d.weapon("IVANBOMBER").plantsBomb && d.weapon("IVANBOMBER").cellRangefinding && !d.weapon("IVANBOMBER").suicide,"Ivan became an ordinary gun or suicide attacker");
  require(d.ivanBomb().delay==450 && d.ivanBomb().damage==400 && d.ivanBomb().flickerFrames==8 && d.warhead(d.ivanBomb().warhead).spreadMilliCells==1500,"Native bomb globals lost");
  require(d.weapon("DEFUSEKIT").supported && d.weapon("DEFUSEKIT").disarmsBomb && d.unit("ENGINEER").support()==SupportStatus::Ready && d.unit("SENGINEER").bombSight==4000,"Engineer disarm/detection not enabled");
  require(d.weaponForTarget(d.unit("FV"),false,false,7)->id=="CRNUKE" && d.unit("FV").gunnerTurrets.at(7)==3,"Ivan IFV mapping lost");
  require(apoc.selfHealing && apoc.strength==800 && d.weapon(apoc.primary).burst==2 && d.weapon(apoc.secondary).burst==2 && d.weapon(apoc.collateralWeapon).damage==40,"Apocalypse capabilities lost");
  require(d.effectAnimation("CRIVEXP").frameCount==23 && d.effectAnimation("APMUZZLE").frameCount==13 && d.effectAnimation("APOCEXP").frameCount==16,"Missing native explosion/muzzle assets");
  require(d.sidebarUnit("ivanicon","Soviet")->id=="IVAN" && d.sidebarUnit("mtnkicon","Soviet")->id=="APOC","Cameo/image aliases lost");
  for(const auto field:{"IvanTimedDelay=0","IvanDamage=-1","IvanIconFlickerRate=0","ConditionYellow=150%"}) {
    auto r=d.rules();r.overlay(IniDocument::parse(std::string(std::string_view(field).starts_with("ConditionYellow")?"[AudioVisual]\n":"[CombatDamage]\n")+field));bool rejected=false;try{(void)DefinitionRegistry::compile(r,d.art(),d.replica());}catch(const std::runtime_error&){rejected=true;}require(rejected,"Invalid bomb/healing configuration accepted");
  }
}
void timedAndFriendly(const DefinitionRegistry& base) {
  for(const bool friendly:{false,true}) {
    const auto d=fixture(base);World w(d);const auto actor=w.spawn("IVAN",{}),target=w.spawn("HTNK",{1,0},friendly?0:1);
    const auto bomb=plant(w,actor,target);require(find(w,target)->hp==10000 && w.shots().empty() && w.effects().empty(),"Planting dealt instant damage or fired an ordinary projectile");
    require(bomb.source==actor && bomb.owner==0 && bomb.detonates-bomb.planted==450,"Bomb lost owner or native delay");
    w.enqueue({w.tick()+1,0,2,CommandKind::AttackEntity,actor,target});run(w,20);require(find(w,target)->bomb->planted==bomb.planted,"Second plant reset or stacked a bomb");
    w.enqueue({w.tick()+1,friendly?0u:1u,3,CommandKind::Move,target,0,{5,0}});reach(w,bomb.detonates-1);
    require(find(w,target)->position==Position{5000,0} && find(w,target)->hp==10000 && find(w,target)->bomb,"Bomb did not follow moving target or exploded early");
    w.step();require(!find(w,target)->bomb && find(w,target)->hp==9600 && effects(w,"CRIVEXP")==1 && w.effects().front().position==Position{5000,0},"Bomb failed to detonate at current position exactly once");
  }
  const auto d=fixture(base,"[CombatDamage]\nIvanDamage=123\nIvanTimedDelay=30\n[IvanBomber]\nDamage=777\n");World custom(d);
  const auto actor=custom.spawn("IVAN",{}),target=custom.spawn("HTNK",{1,0},1);const auto bomb=plant(custom,actor,target);reach(custom,bomb.detonates);
  require(find(custom,target)->hp==9877,"Planted bomb used the placement weapon's death-only Damage");
  const auto explosive=fixture(base,"[IVAN]\nStrength=10000\n[E2]\nStrength=125\n");World infantry(explosive);
  const auto owner=infantry.spawn("IVAN",{}),victim=infantry.spawn("E2",{1,0},1);const auto lethal=plant(infantry,owner,victim);reach(infantry,lethal.detonates);
  require(!find(infantry,victim) && effects(infantry,"S_BANG34")==1 && infantry.infantryCorpses().empty(),"Native InfDeath=6 became an ordinary infantry corpse");
}
void disarmAndCancellation(const DefinitionRegistry& base) {
  const auto d=fixture(base);
  for(const auto engineer:{"ENGINEER","SENGINEER"})for(const bool building:{false,true}) {
    World w(d);const auto actor=w.spawn("IVAN",{},1),target=w.spawn(building?"GAPOWR":"HTNK",{1,0});const auto bomb=plant(w,actor,target,1);
    const auto crew=w.spawn(engineer,{0,-1});const auto hp=find(w,target)->hp;
    w.enqueue({w.tick()+1,0,1,CommandKind::AttackEntity,crew,target});until(w,[&]{return !find(w,target)->bomb;},"Engineer failed to disarm attached bomb");
    require(find(w,crew) && find(w,crew)->hp==75 && !find(w,crew)->buildingInteraction && find(w,target)->hp==hp && find(w,target)->owner==0,"Disarm damaged/repaired/captured target or consumed engineer");
    reach(w,bomb.detonates+5);require(find(w,target)->hp==hp && effects(w,"CRIVEXP")==0,"Disarmed bomb later detonated");
  }
  World cancelled(d);const auto actor=cancelled.spawn("IVAN",{}),target=cancelled.spawn("HTNK",{7,0},1);
  cancelled.enqueue({1,0,1,CommandKind::AttackEntity,actor,target});cancelled.enqueue({4,0,2,CommandKind::Stop,actor});run(cancelled,500);
  require(!find(cancelled,target)->bomb && find(cancelled,target)->hp==10000,"Stopped plant approach still attached a bomb");
  const auto immune=fixture(base,"[HTNK]\nBombable=no\n");World blocked(immune);const auto foot=blocked.spawn("IVAN",{}),tank=blocked.spawn("HTNK",{1,0},1);
  blocked.enqueue({1,0,1,CommandKind::AttackEntity,foot,tank});blocked.enqueue({30,0,2,CommandKind::AttackGround,foot,0,{1,0}});run(blocked,100);
  require(!find(blocked,tank)->bomb && blocked.shots().empty(),"Bombable=no or ground-attack restriction ignored");
  World stopped(d);const auto planter=stopped.spawn("IVAN",{}),host=stopped.spawn("HTNK",{1,0}),crew=stopped.spawn("ENGINEER",{7,0});const auto timer=plant(stopped,planter,host);
  stopped.enqueue({stopped.tick()+1,0,2,CommandKind::AttackEntity,crew,host});stopped.enqueue({stopped.tick()+4,0,3,CommandKind::Stop,crew});run(stopped,120);
  require(find(stopped,host)->bomb && find(stopped,host)->bomb->detonates==timer.detonates,"Cancelled disarm still removed/reset bomb");
}
void deathAndSale(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[IVAN]\nStrength=10000\n");World w(d);const auto actor=w.spawn("IVAN",{}),target=w.spawn("HTNK",{1,0},1),witness=w.spawn("HTNK",{1,1});
  const auto bomb=plant(w,actor,target);const auto killer=w.spawn("MTNK",{10,0});w.enqueue({w.tick()+1,0,2,CommandKind::AttackEntity,killer,target});until(w,[&]{return !find(w,target);},"Bombed target did not die");
  require(effects(w,"CRIVEXP")==1 && find(w,witness)->hp<10000,"Target death failed to detonate attached bomb");reach(w,bomb.detonates+5);require(effects(w,"CRIVEXP")==0,"Dead target retained an orphan bomb");
  World sourceDead(d);const auto source=sourceDead.spawn("IVAN",{}),victim=sourceDead.spawn("HTNK",{1,0},1);const auto timer=plant(sourceDead,source,victim);
  sourceDead.enqueue({sourceDead.tick()+1,0,2,CommandKind::Move,source,0,{-5,0}});run(sourceDead,220);
  const auto gun=sourceDead.spawn("MTNK",{10,-6},1);sourceDead.enqueue({sourceDead.tick()+1,1,1,CommandKind::AttackEntity,gun,source});until(sourceDead,[&]{return !find(sourceDead,source);},"Could not kill bomb owner");
  const auto hp=find(sourceDead,victim)->hp;reach(sourceDead,timer.detonates);require(find(sourceDead,victim)->hp==hp-400 && effects(sourceDead,"CRIVEXP")==1,"Owner death disarmed bomb or lost delayed damage");
  World sale(d);const auto ivan=sale.spawn("IVAN",{}),house=sale.spawn("GAPOWR",{1,0});plant(sale,ivan,house);
  sale.enqueue({sale.tick()+1,0,2,CommandKind::SellBuilding,house});until(sale,[&]{return !find(sale,house);},"Bombed building failed to sell");require(effects(sale,"CRIVEXP")==1,"Selling silently discarded attached bomb");
}
void transportAndIfv(const DefinitionRegistry& base) {
  const auto d=fixture(base);
  for(const bool killTransport:{false,true}) {
    World w(d);const auto actor=w.spawn("IVAN",{}),target=w.spawn("E2",{1,0}),carrier=w.spawn("HTK",{3,0});const auto bomb=plant(w,actor,target);
    w.enqueue({w.tick()+1,0,2,CommandKind::EnterTransport,target,carrier});until(w,[&]{return find(w,target)->containedBy==carrier;},"Bombed passenger failed to board");
    if(killTransport) {
      const auto killer=w.spawn("MTNK",{12,0},1);w.enqueue({w.tick()+1,1,1,CommandKind::AttackEntity,killer,carrier});until(w,[&]{return !find(w,carrier);},"Could not kill bombed transport");
      require(!find(w,target) && effects(w,"CRIVEXP")==0,"Hidden destroyed passenger detonated in world");reach(w,bomb.detonates+5);require(effects(w,"CRIVEXP")==0,"Destroyed cargo retained orphan bomb");
    } else {
      reach(w,bomb.detonates+20);require(find(w,target)->bomb && find(w,target)->hp==10000,"Hidden bomb exploded instead of waiting for unlimbo");
      w.enqueue({w.tick()+1,0,3,CommandKind::Unload,carrier});until(w,[&]{return !find(w,target)->containedBy;},"Bombed passenger failed to unload");w.step();
      require(!find(w,target)->bomb && find(w,target)->hp==9600 && effects(w,"CRIVEXP")==1,"Expired hidden bomb failed to detonate on unload");
    }
  }
  World ifv(d);const auto host=ifv.spawn("FV",{2,0}),passenger=ifv.spawn("IVAN",{});
  ifv.enqueue({1,0,1,CommandKind::EnterTransport,passenger,host});until(ifv,[&]{return find(ifv,passenger)->containedBy==host;},"Ivan failed to board IFV");
  require(find(ifv,host)->cargo->mode==7 && find(ifv,host)->cargo->turret==3 && find(ifv,host)->activeWeapon=="CRNUKE","Ivan IFV mode/turret wrong");
  ifv.enqueue({ifv.tick()+1,0,2,CommandKind::Unload,host});until(ifv,[&]{return !find(ifv,passenger)->containedBy;},"Ivan failed to unload");require(find(ifv,host)->cargo->mode==0,"IFV retained suicide after unload");
  ifv.enqueue({ifv.tick()+1,0,3,CommandKind::EnterTransport,passenger,host});until(ifv,[&]{return find(ifv,passenger)->containedBy==host;},"Ivan failed to reboard");
  ifv.enqueue({ifv.tick()+1,0,4,CommandKind::AttackGround,host,0,{2,0}});until(ifv,[&]{return !find(ifv,host);},"Ivan IFV did not self-destruct");
  require(!find(ifv,passenger) && effects(ifv,"MININUKE")==1 && effects(ifv,"CRIVEXP")==0 && ifv.radiationLevel({2,0})==500,"Ivan IFV detonated wrong weapon or duplicated passenger death explosion");
  World deploy(d);const auto planter=deploy.spawn("IVAN",{3,0}),mcv=deploy.spawn("AMCV",{4,0},1);const auto timer=plant(deploy,planter,mcv);
  deploy.enqueue({deploy.tick()+1,0,2,CommandKind::Move,planter,0,{0,0}});run(deploy,120);deploy.enqueue({deploy.tick()+1,1,1,CommandKind::Deploy,mcv});
  until(deploy,[&]{return find(deploy,mcv) && find(deploy,mcv)->type=="GACNST";},"Bombed MCV failed to deploy");
  require(find(deploy,mcv)->bomb && find(deploy,mcv)->bomb->planted==timer.planted && find(deploy,mcv)->bomb->detonates==timer.detonates,"Deployment lost attached bomb identity/timer");
}
void apocWeaponsAndHealing(const DefinitionRegistry& base) {
  const auto d=fixture(base);
  for(const bool air:{false,true}) {
    World w(d);const auto actor=w.spawn("APOC",{}),target=w.spawn(air?"JUMPJET":"HTNK",{4,0},1);w.enqueue({1,0,1,CommandKind::AttackEntity,actor,target});
    std::set<std::uint64_t> shots;std::vector<ShotSnapshot> fired;
    until(w,[&]{for(const auto& s:w.shots())if(shots.insert(s.id).second)fired.push_back(s);return fired.size()==2;},"Apocalypse did not fire two-shot burst");
    w.enqueue({w.tick()+1,0,2,CommandKind::Stop,actor});run(w,80);
    require(fired[0].weaponId==(air?"MAMMOTHTUSK":"120MMX") && fired[1].weaponId==fired[0].weaponId && fired[1].started-fired[0].started==5,"Apocalypse burst domain/timing wrong");
    if(!air)require(find(w,target)->hp==9800 && fired[0].start!=fired[1].start,"Ground burst lost damage or alternating barrel FLH");
    else require(!find(w,target) || find(w,target)->hp<find(w,target)->maxHp,"Apocalypse AA rockets failed to hit");
  }
  for(const int damage:{100,401,600,800}) {
    const auto heal=fixture(base,"[TestHit]\nDamage="+std::to_string(damage)+"\n");World w(heal);const auto actor=w.spawn("APOC",{}),killer=w.spawn("MTNK",{10,0},1);
    w.enqueue({1,1,1,CommandKind::AttackEntity,killer,actor});until(w,[&]{return !find(w,actor) || find(w,actor)->hp<800;},"Healing fixture never hit");
    w.enqueue({w.tick()+1,1,2,CommandKind::Stop,killer});const auto money=w.credits();
    if(damage==800) {require(!find(w,actor) && effects(w,"TWLT070")==1,"Dead Apocalypse revived or lost native death effect");run(w,100);require(!find(w,actor),"Self healing resurrected dead tank");continue;}
    const auto start=*find(w,actor);const auto tick=w.tick();run(w,56);const int expected=start.hp>=400?start.hp:std::min(400,start.hp+int((w.tick()/14)-(tick/14)));
    require(find(w,actor)->hp==expected && w.credits()==money,"Self healing used RepairStep, wrong phase/cap, or charged credits");run(w,3000);
    require(find(w,actor)->hp==(start.hp>=400?start.hp:400),"Self healing exceeded native yellow-health cap");
  }
}
void productionAndReplay(const DefinitionRegistry& base) {
  for(const bool vehicle:{false,true}) {
    const auto d=fixture(base,{},"[Simulation]\nSandbox=no\nPlayerHouse=Russians\nStartingCredits=2000\n");World w(d);const auto factory=w.spawn(vehicle?"NAWEAP":"NAHAND",{});const auto type=vehicle?"APOC":"IVAN";
    w.enqueue({1,0,1,CommandKind::Produce,factory,0,{},type});run(w,12);require(w.credits()==2000,"New unit prerequisite bypassed");
    w.spawn(vehicle?"NATECH":"NARADR",{8,0});w.enqueue({w.tick()+1,0,2,CommandKind::Produce,factory,0,{},type});run(w,450);
    const auto entities=w.snapshot();const auto made=std::find_if(entities.begin(),entities.end(),[&](const auto& e){return e.type==type;});
    require(made!=entities.end() && made->activity!=Activity::LeavingFactory && w.credits()==2000-d.unit(type).cost,"New unit production/cost/factory exit failed");
  }
  const auto d=fixture(base);World a(d),b(d);
  for(auto* w:{&a,&b}) {const auto actor=w->spawn("IVAN",{}),target=w->spawn("HTNK",{1,0},1);w->enqueue({1,0,1,CommandKind::AttackEntity,actor,target});w->enqueue({90,1,1,CommandKind::Move,target,0,{5,0}});}
  bool attached=false;for(int i=0;i<700;++i){a.step();b.step();require(a.stateHash()==b.stateHash(),"Bomb/recovery replay diverged");for(const auto& e:a.snapshot())attached|=e.bomb.has_value();}require(attached,"Replay never tested attached bomb state");
}
}
int main(int argc,char** argv)try{const auto d=DefinitionRegistry::load(argc>1?argv[1]:"assets/settings");definitions(d);timedAndFriendly(d);disarmAndCancellation(d);deathAndSale(d);transportAndIfv(d);apocWeaponsAndHealing(d);productionAndReplay(d);std::cout<<"Ivan/Apocalypse regressions passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
