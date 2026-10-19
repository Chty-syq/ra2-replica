#include "simulation/world.h"
#include "test_support.h"
#include <algorithm>
#include <cmath>
#include <iostream>
using namespace ra2;
namespace {
std::optional<EntitySnapshot> find(const World& w,EntityId id){for(const auto& e:w.snapshot())if(e.id==id)return e;return {};}
void run(World& w,int frames){while(frames-->0)w.step();}
template<class P>void until(World& w,P p,const char* message,int limit=1200){for(int i=0;i<limit && !p();++i)w.step();require(p(),message);}
DefinitionRegistry fixture(const DefinitionRegistry& base,std::string extra={},std::string profile={}) {
  auto r=base.rules(),a=base.art(),p=base.replica();
  for(const auto id:{"TERROR","DTRUCK","FV","HTK","HTNK","MTNK","E1","E2","JUMPJET","SNIPE"})r.overlay(IniDocument::parse(std::string("[")+id+"]\nCanPassiveAquire=no\n"));
  r.overlay(IniDocument::parse("[HTNK]\nStrength=100000\n[E2]\nStrength=100000\nFearless=yes\n[MTNK]\nPrimary=TestKill\n[TestKill]\nDamage=100000\nROF=100\nRange=30\nProjectile=InvisibleLow\nWarhead=TestKillWH\n[TestKillWH]\nVerses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%\n"+extra));
  p.overlay(IniDocument::parse("[Simulation]\nProductionFramesOverride=4\n"+profile));
  for(const auto& [id,anim]:base.effectAnimations())if(anim.frameCount>0)a.overlay(IniDocument::parse("["+id+"]\nEnd="+std::to_string(anim.frameCount-1)));
  return DefinitionRegistry::compile(r,a,p);
}
int countEffect(const World& w,std::string_view animation){return std::count_if(w.effects().begin(),w.effects().end(),[&](const auto& e){return e.animation==animation;});}
int damageAt(const DefinitionRegistry& d,std::string_view weapon,const EntitySnapshot& target,Position epicenter) {
  const auto& w=d.weapon(weapon);const auto& wh=d.warhead(w.warhead);const auto& type=d.unit(target.type);
  const auto dx=target.position.x-epicenter.x,dy=target.position.y-epicenter.y;
  const auto distance=std::max<std::int64_t>(0,std::sqrt(dx*dx+dy*dy)-(type.infantry?d.profile().infantryHitRadiusLeptons*1000/256:0));
  if(distance>wh.spreadMilliCells)return 0;
  const auto attenuation=100-distance*(100-wh.percentAtMax)/wh.spreadMilliCells;
  return w.damage*wh.versesPercent[type.armor]*attenuation/10000;
}
void definitions(const DefinitionRegistry& d) {
  const auto& foot=d.unit("TERROR");const auto& truck=d.unit("DTRUCK");
  require(foot.support()==SupportStatus::Ready && truck.support()==SupportStatus::Ready,"Demolition types not ready");
  require(foot.image=="TRST" && foot.infantry->bodyFrames==188 && !foot.infantry->crawls && foot.infantry->fireUp==1,"Original terrorist art layout lost");
  require(foot.strength==75 && foot.cost==200 && foot.ifvMode==11 && foot.explodes && foot.deathWeapon=="TERRORBOMB","Native terrorist rules lost");
  require(truck.image=="TRUCKA" && truck.strength==150 && truck.cost==1500 && !truck.turret && truck.explodes && truck.deathWeapon=="DEMOBOMB","Native truck rules lost");
  require(d.weapon("TERRORBOMB").suicide && d.weapon("TERRORBOMB").damage==225 && d.warhead("TERRORBOMBWH").spreadMilliCells==2000,"Native terror weapon lost");
  require(d.weapon("DEMOBOMB").suicide && d.weapon("DEMOBOMB").damage==300 && d.weapon("DEMOBOMB").radiationLevel==100 && d.warhead("DEMOBOMBWH").spreadMilliCells==8000,"Native demo weapon lost");
  require(d.weaponForTarget(d.unit("FV"),false,false,11)->id=="CRTERRORBOMB" && d.unit("FV").gunnerTurrets.at(11)==3 && d.unit("FV").deathWeapon=="CRNUKE","Suicide IFV mode lost");
  require(d.warhead("CRNUKEWH").animations==std::vector<std::string>{"MININUKE"} && d.effectAnimation("MININUKE").frameCount==39 && d.effectAnimation("DEMTEXP").frameCount==25,"Native explosion references/frames lost");
  require(d.sidebarUnit("trsticon","Soviet")->id=="TERROR" && d.sidebarUnit("trkaicon","Soviet")->id=="DTRUCK","New cameos map to wrong types");
  require(d.unit("IVAN").ivan && d.weapon("IVANBOMBER").plantsBomb && d.unit("TANY").c4,"Typed Ivan/C4 bomb capabilities lost");
  auto r=d.rules();r.overlay(IniDocument::parse("[TerrorBomb]\nProjectile=Cannon\n"));
  const auto bad=DefinitionRegistry::compile(r,d.art(),d.replica());require(!bad.weapon("TERRORBOMB").supported,"Unsupported suicide projectile accepted");
}
void activeExplosion(const DefinitionRegistry& base) {
  const auto d=fixture(base);
  for(const auto type:{"TERROR","DTRUCK"}) {
    World w(d);const auto actor=w.spawn(type,{});const auto start=*find(w,actor);
    std::vector<EntityId> targets;for(int i=0;i<3;++i)targets.push_back(w.spawn("E2",{1,0},1));
    targets.push_back(w.spawn("HTNK",{0,1}));targets.push_back(w.spawn("HTNK",{2,0},1));targets.push_back(w.spawn("HTNK",{8,0},1));targets.push_back(w.spawn("HTNK",{9,0},1));
    for(const auto id:targets)require(id,"Splash target setup failed");
    const auto before=w.snapshot();run(w,80);require(find(w,actor)->hp==start.hp && w.effects().empty(),"Suicide unit auto-acquired a target");
    w.enqueue({w.tick()+1,0,1,CommandKind::AttackGround,actor,0,{}});
    until(w,[&]{return !find(w,actor);},"Manual suicide did not detonate");
    const auto weapon=d.unit(type).primary;const auto animation=std::string(type)=="TERROR"?"MININUKE":"DEMTEXP";
    require(w.shots().empty() && countEffect(w,animation)==1 && w.infantryCorpses().empty(),"Suicide emitted a projectile, duplicate blast or ordinary corpse");
    require(w.effects().front().position==start.position && w.effects().front().sourceType==type,"Blast moved to target cell or lost source identity");
    for(const auto id:targets) {
      const auto previous=*std::find_if(before.begin(),before.end(),[&](const auto& e){return e.id==id;});
      require(find(w,id)->hp==previous.hp-damageAt(d,weapon,previous,start.position),"Splash falloff, armor, friendly fire or subcell damage wrong");
    }
    if(std::string(type)=="DTRUCK")require(w.radiationSites().size()==1 && w.radiationSites().front().spread==8000 && w.radiationLevel({})==100,"Native truck radiation missing");
    else require(w.radiationSites().empty(),"Terrorist invented a radiation field");
    run(w,650);require(w.radiationSites().empty() && w.effects().empty(),"Suicide effect/radiation never expires");
  }
}
void pursuitAndDeath(const DefinitionRegistry& base) {
  const auto d=fixture(base);
  for(const auto type:{"TERROR","DTRUCK"}) {
    World w(d);const auto actor=w.spawn(type,{}),target=w.spawn("HTNK",{7,0},1);
    w.enqueue({1,0,1,CommandKind::AttackEntity,actor,target});run(w,8);
    w.enqueue({w.tick()+1,0,2,CommandKind::Stop,actor});run(w,50);require(find(w,actor) && w.effects().empty(),"Stop failed to cancel suicide approach");
    w.enqueue({w.tick()+1,0,3,CommandKind::AttackEntity,actor,target});Position last{};
    until(w,[&]{if(const auto e=find(w,actor)){last=e->position;return false;}return true;},"Suicide attacker failed to approach ground target");
    const auto epicenter=w.effects().front().position;
    require(std::abs(epicenter.x-last.x)<250 && std::abs(epicenter.y-last.y)<250 && epicenter!=find(w,target)->position,"Pursuit explosion snapped away from moving attacker");
    World killed(d);const auto victim=killed.spawn(type,{}),killer=killed.spawn("MTNK",{12,0},1);const auto location=find(killed,victim)->position;
    killed.enqueue({1,1,1,CommandKind::AttackEntity,killer,victim});until(killed,[&]{return !find(killed,victim);},"Could not kill demolition unit");
    require(killed.effects().size()==1 && killed.effects().front().position==location && killed.shots().empty(),"Destroyed demolition unit did not detonate once at actual position");
    World air(d);const auto grounded=air.spawn(type,{}),flying=air.spawn("JUMPJET",{1,0},1);air.enqueue({1,0,1,CommandKind::AttackEntity,grounded,flying});run(air,130);
    require(find(air,grounded) && find(air,flying)->hp==find(air,flying)->maxHp && air.effects().empty(),"Ground suicide weapon attacked aircraft");
  }
  World crushed(d);const auto driver=crushed.spawn("HTNK",{}),victim=crushed.spawn("TERROR",{3,0},1);
  crushed.enqueue({1,0,1,CommandKind::Move,driver,0,{5,0}});until(crushed,[&]{return !find(crushed,victim);},"Tank did not crush terrorist");
  require(countEffect(crushed,"MININUKE")==1 && find(crushed,driver)->hp<100000,"Crushed terrorist did not detonate against crusher");
}
void chains(const DefinitionRegistry& base) {
  const auto d=fixture(base);World a(d),b(d);
  for(auto* w:{&a,&b}) {
    // Deliberately spawn the trigger LAST, so the first blast kills lower IDs.
    for(int x:{12,8,4,0})require(w->spawn("DTRUCK",{x,0}),"Chain setup failed");
    w->enqueue({1,0,1,CommandKind::AttackGround,4,0,{}});
  }
  a.step();b.step();require(a.snapshot().empty() && countEffect(a,"DEMTEXP")==4 && a.radiationSites().size()==4,"Chain skipped an earlier ID or detonated twice");
  for(int i=0;i<700;++i){require(a.stateHash()==b.stateHash(),"Explosion replay diverged");a.step();b.step();}
  World c(d),e(d);
  for(auto* w:{&c,&e}) {
    const auto host=w->spawn("FV",{4,0}),passenger=w->spawn("TERROR",{});
    w->spawn("HTNK",{8,2},1);
    w->enqueue({1,0,1,CommandKind::EnterTransport,passenger,host});
    w->enqueue({150,0,2,CommandKind::Move,host,0,{7,0}});
    w->enqueue({350,0,3,CommandKind::AttackGround,host,0,{7,0}});
  }
  for(int i=0;i<900;++i){c.step();e.step();require(c.stateHash()==e.stateHash(),"Passenger/movement/suicide replay diverged");}
}
void transport(const DefinitionRegistry& base) {
  const auto d=fixture(base);
  for(const auto carrier:{"FV","HTK"}) {
    World w(d);const auto host=w.spawn(carrier,{4,0}),foot=w.spawn("TERROR",{});
    w.enqueue({1,0,1,CommandKind::EnterTransport,foot,host});until(w,[&]{return find(w,foot)->containedBy==host;},"Terrorist failed to board");
    w.enqueue({w.tick()+1,0,2,CommandKind::AttackGround,foot,0,{4,0}});run(w,20);require(find(w,host) && find(w,foot) && w.effects().empty(),"Hidden passenger accepted suicide command");
    if(std::string(carrier)=="FV") {
      require(find(w,host)->cargo->mode==11 && find(w,host)->cargo->turret==3 && find(w,host)->activeWeapon=="CRTERRORBOMB","Terrorist IFV turret/weapon wrong");
      w.enqueue({w.tick()+1,0,3,CommandKind::Unload,host});until(w,[&]{return !find(w,foot)->containedBy;},"Terrorist IFV failed to unload");
      require(find(w,host)->activeWeapon=="HOVERMISSILE" && find(w,host)->cargo->mode==0,"Unload retained suicide weapon");
      w.enqueue({w.tick()+1,0,4,CommandKind::EnterTransport,foot,host});until(w,[&]{return find(w,foot)->containedBy==host;},"Terrorist failed to reboard");
      const auto witness=w.spawn("HTNK",{7,0},1);const auto previous=*find(w,witness);
      w.enqueue({w.tick()+1,0,5,CommandKind::AttackGround,host,0,{4,0}});until(w,[&]{return !find(w,host);},"Suicide IFV did not detonate");
      require(!find(w,foot) && countEffect(w,"MININUKE")==1 && w.radiationLevel({4,0})==500 && find(w,witness)->hp==previous.hp-damageAt(d,"CRNUKE",previous,{4000,0}),"IFV did not use its explicit native death weapon exactly once");
    } else {
      const auto killer=w.spawn("MTNK",{15,0},1);w.enqueue({w.tick()+1,1,1,CommandKind::AttackEntity,killer,host});until(w,[&]{return !find(w,host);},"Could not destroy transport");
      require(!find(w,foot) && w.effects().empty() && w.radiationSites().empty(),"Limbo passenger detonated outside destroyed transport");
    }
  }
  for(const auto passenger:{"","E1","ENGINEER","DESO","TERROR"}) {
    World w(d);const auto host=w.spawn("FV",{4,0});EntityId foot=0;
    if(*passenger){foot=w.spawn(passenger,{});w.enqueue({1,0,1,CommandKind::EnterTransport,foot,host});until(w,[&]{return find(w,foot)->containedBy==host;},"IFV mode setup failed");}
    const auto killer=w.spawn("MTNK",{15,0},1);w.enqueue({w.tick()+1,1,1,CommandKind::AttackEntity,killer,host});until(w,[&]{return !find(w,host);},"Could not kill IFV");
    require(!foot || !find(w,foot),"Dead IFV orphaned passenger");
    require(countEffect(w,"MININUKE")==(*passenger && std::string(passenger)=="TERROR"?1:0),"Ordinary IFV nuked or suicide IFV failed to explode on death");
  }
}
void production(const DefinitionRegistry& base) {
  for(const bool vehicle:{false,true})for(const bool validHouse:{false,true}) {
    const auto type=vehicle?"DTRUCK":"TERROR";const auto d=fixture(base,{},std::string("[Simulation]\nSandbox=no\nStartingCredits=2000\nPlayerHouse=")+(validHouse?(vehicle?"Africans":"Confederation"):"Russians"));
    World w(d);const auto factory=w.spawn(vehicle?"NAWEAP":"NAHAND",{});
    w.enqueue({1,0,1,CommandKind::Produce,factory,0,{},type});run(w,12);require(w.credits()==2000 && w.snapshot().size()==1,"Radar prerequisite bypassed");
    w.spawn("NARADR",{8,0});w.enqueue({w.tick()+1,0,2,CommandKind::Produce,factory,0,{},type});run(w,240);
    const auto units=w.snapshot();const auto unit=std::find_if(units.begin(),units.end(),[&](const auto& e){return e.type==type;});
    require((unit!=units.end())==validHouse,"Original national restriction lost");
    require(w.credits(0)==2000-(validHouse?d.unit(type).cost:0),"Production cost wrong");
    if(validHouse)require(unit->activity!=Activity::LeavingFactory && unit->hp==d.unit(type).strength,"Produced unit stuck in factory exit");
  }
  const auto d=fixture(base);World sandbox(d);const auto barracks=sandbox.spawn("NAHAND",{}),factory=sandbox.spawn("NAWEAP",{8,0});
  sandbox.enqueue({1,0,1,CommandKind::Produce,barracks,0,{},"TERROR"});sandbox.enqueue({1,0,2,CommandKind::Produce,factory,0,{},"DTRUCK"});run(sandbox,240);
  const auto units=sandbox.snapshot();
  require(std::count_if(units.begin(),units.end(),[](const auto& e){return e.type=="TERROR" || e.type=="DTRUCK";})==2,"Mixed-faction sandbox could not produce both new units");
}
}
int main(int argc,char** argv)try{const auto d=DefinitionRegistry::load(argc>1?argv[1]:"assets/settings");definitions(d);activeExplosion(d);pursuitAndDeath(d);chains(d);transport(d);production(d);std::cout<<"Demolition regressions passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
