#include "simulation/world.h"
#include "shp_ts.h"
#include "test_support.h"
#include <algorithm>
#include <iostream>
using namespace ra2;
namespace {
std::optional<EntitySnapshot> find(const World& w,EntityId id){for(const auto& e:w.snapshot())if(e.id==id)return e;return {};}
void run(World& w,int n){while(n-->0)w.step();}
template<class P> void until(World& w,P p,const char* message,int limit=1200){for(int i=0;i<limit && !p();++i)w.step();require(p(),message);}
DefinitionRegistry fixture(const DefinitionRegistry& base,std::string extra={},std::string profile={}) {
  auto rules=base.rules(),replica=base.replica(),art=base.art();
  rules.overlay(IniDocument::parse("[SHK]\nCanPassiveAquire=no\n[TTNK]\nCanPassiveAquire=no\n[TESLA]\nCanPassiveAquire=no\n[HTNK]\nCanPassiveAquire=no\n[FV]\nCanPassiveAquire=no\n[E2]\nCanPassiveAquire=no\nFearless=yes\n[JUMPJET]\nCanPassiveAquire=no\n"+extra));
  replica.overlay(IniDocument::parse("[Simulation]\nProductionFramesOverride=4\n"+profile));
  art.overlay(IniDocument::parse("[ELECTRO]\nEnd="+std::to_string(base.effectAnimation("ELECTRO").frameCount-1)));
  return DefinitionRegistry::compile(rules,art,replica);
}
void definitions(const DefinitionRegistry& d) {
  const auto& foot=d.unit("SHK");const auto& tank=d.unit("TTNK");
  require(foot.unsupported.empty() && tank.unsupported.empty(),"Tesla units still disabled");
  require(foot.infantry && foot.infantry->bodyFrames>290 && !foot.infantry->crushable && foot.infantry->fireUp==2 && foot.ifvMode==6,"Native armored infantry properties lost");
  require(tank.cost==1200 && tank.requiredHouses==std::vector<std::string>{"RUSSIANS"},"Tesla tank country/cost changed");
  require(d.sidebarUnit("shkicon","Soviet")->id=="SHK" && d.sidebarUnit("ttnkicon","Soviet")->id=="TTNK","Tesla cameos missing");
  require(d.weaponForTarget(foot,false)->id=="ELECTRICBOLT" && !d.weaponForTarget(foot,true) && !d.weaponForTarget(tank,true),"Tesla ground/air filtering failed");
  require(d.chargeWeapon(foot,d.unit("TESLA"))->id=="ASSAULTBOLT" && !d.chargeWeapon(foot,d.unit("ATESLA")) && !d.chargeWeapon(tank,d.unit("TESLA")),"Charging target/source capability lost");
  require(d.weaponForTarget(d.unit("FV"),false,false,6)->id=="CRELECTRICBOLT" && !d.weaponForTarget(d.unit("FV"),true,false,6) && d.unit("FV").gunnerTurrets.at(6)==3,"IFV shock mode mismatch");
  const auto& death=d.effectAnimation("ELECTRO");
  require(death.frameCount==15 && death.loopStart==0 && death.loopEnd==3 && death.loopCount==5 && death.extraFrames()==16,"Native electrocution loop missing");
  for(int i=0;i<20;++i)require(death.frame(i)==i%4,"Electrocution loop frame mismatch");
  require(death.frame(20)==4,"Electrocution never leaves loop");
  for(const auto field:{"LoopCount=0","LoopCount=1001","LoopEnd=65536","LoopStart=4\nLoopEnd=3"}) {
    auto art=d.art();art.overlay(IniDocument::parse(std::string("[ELECTRO]\n")+field));bool rejected=false;
    try{(void)DefinitionRegistry::compile(d.rules(),art,d.replica());}catch(const std::runtime_error&){rejected=true;}
    require(rejected,"Malformed electrocution loop accepted");
  }
}
void production(const DefinitionRegistry& base) {
  for(const bool tank:{false,true}) {
    const auto d=fixture(base,{},"[Simulation]\nSandbox=no\nPlayerHouse=Russians\nStartingCredits=2000\n");World w(d);
    const auto factory=w.spawn(tank?"NAWEAP":"NAHAND",{});const auto type=tank?"TTNK":"SHK";
    if(tank){w.enqueue({1,0,1,CommandKind::Produce,factory,0,{},type});run(w,10);require(w.credits()==2000,"Tesla tank bypassed radar prerequisite");w.spawn("NARADR",{8,0});}
    w.enqueue({w.tick()+1,0,2,CommandKind::Produce,factory,0,{},type});run(w,180);
    int count=0;for(const auto& e:w.snapshot())count+=e.type==type;
    require(count==1 && w.credits()==(tank?800:1500),"Tesla production/exit/cost failed");
  }
  const auto wrong=fixture(base,{},"[Simulation]\nSandbox=no\nPlayerHouse=Arabs\nStartingCredits=2000\n");World w(wrong);
  const auto factory=w.spawn("NAWEAP",{});w.spawn("NARADR",{8,0});w.enqueue({1,0,1,CommandKind::Produce,factory,0,{},"TTNK"});run(w,50);
  require(w.credits()==2000 && w.snapshot().size()==2,"Tesla tank country restriction bypassed");
}
void combat(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[HTNK]\nStrength=100000\n[JUMPJET]\nStrength=100000\n");
  for(const auto type:{"SHK","TTNK"}) {
    World w(d);const auto gun=w.spawn(type,{}),target=w.spawn("HTNK",{2,0},1);w.enqueue({1,0,1,CommandKind::AttackEntity,gun,target});
    std::vector<Tick> times;
    for(int i=0;i<280;++i){w.step();for(const auto& b:w.beams())if(b.started==w.tick()) {
      const bool foot=std::string_view(type)=="SHK";require(b.weapon==(foot?"ELECTRICBOLT":"TANKBOLT") && b.duration==17,"Wrong Tesla weapon/bolt duration");
      if(foot)require(b.started==find(w,gun)->infantry->animationStarted+2,"Tesla infantry FireUp ignored");
      require(b.startHeight>=100,"Tesla muzzle height lost");times.push_back(b.started);
    }}
    require(times.size()>=3 && times[1]-times[0]==(std::string_view(type)=="SHK"?60:75),"Tesla ROF changed");
    require(find(w,target)->hp==100000-int(times.size())*(std::string_view(type)=="SHK"?50:135),"Tesla bolt damage/armor mismatch");
    World air(d);const auto a=air.spawn(type,{}),b=air.spawn("JUMPJET",{2,0},1);air.enqueue({1,0,1,CommandKind::AttackEntity,a,b});run(air,160);
    require(air.beams().empty() && find(air,b)->hp==100000,"Ground Tesla fired at air target");
  }
  World death(d);const auto tank=death.spawn("TTNK",{}),victim=death.spawn("E2",{2,0},1);death.enqueue({1,0,1,CommandKind::AttackEntity,tank,victim});
  until(death,[&]{return !find(death,victim);},"Tesla never killed infantry");
  require(death.infantryCorpses().empty() && std::any_of(death.effects().begin(),death.effects().end(),[](const auto& e){return e.animation=="ELECTRO";}),"Tesla death used ordinary infantry corpse");
  for(const auto& e:death.effects())if(e.animation=="ELECTRO")require(e.duration==31,"Native electrocution lifetime ignored looping");
  require(death.occupants({2,0}).empty(),"Electrocution retained infantry occupancy");run(death,170);require(death.effects().empty(),"Electrocution effect never expires");
}
void charging(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[HTNK]\nStrength=100000\n");World w(d);const auto coil=w.spawn("TESLA",{3,0});
  const auto a=w.spawn("SHK",{2,0}),b=w.spawn("SHK",{3,-1}),c=w.spawn("SHK",{4,0});
  const auto attack=[&](EntityId actor,std::uint64_t sequence){w.enqueue({w.tick()+1,0,sequence,CommandKind::AttackEntity,actor,coil});run(w,45);};
  attack(a,1);require(find(w,coil)->coilChargers==1 && !find(w,coil)->powered && !find(w,coil)->overpowered,"One trooper powered an offline coil");
  attack(b,2);require(find(w,coil)->coilChargers==2 && find(w,coil)->powered && !find(w,coil)->overpowered,"Two troopers failed to power an offline coil");
  const auto target=w.spawn("HTNK",{8,0},1);w.enqueue({w.tick()+1,0,3,CommandKind::AttackEntity,coil,target});run(w,45);
  require(find(w,coil)->activeWeapon=="COILBOLT" && find(w,target)->hp<100000,"Offline supported coil did not fire normal weapon");
  attack(c,4);require(find(w,coil)->coilChargers==3 && find(w,coil)->overpowered && find(w,coil)->activeWeapon=="OPCOILBOLT","Three troopers failed to overcharge offline coil");
  bool enhanced=false;for(int i=0;i<160;++i){w.step();for(const auto& beam:w.beams())enhanced|=beam.weapon=="OPCOILBOLT";}
  require(enhanced && find(w,coil)->hp==600,"Charging damaged coil or never fired overcharge");
  w.enqueue({w.tick()+1,0,5,CommandKind::Move,c,0,{12,-4}});run(w,1);require(find(w,coil)->coilChargers==2 && !find(w,coil)->overpowered,"Moving trooper retained overcharge");
  w.enqueue({w.tick()+1,0,6,CommandKind::Stop,b});run(w,1);require(find(w,coil)->coilChargers==1 && !find(w,coil)->powered,"Stopped trooper retained power");
  require(w.spawn("NAPOWR",{-10,0}),"Power plant setup failed");run(w,1);require(find(w,coil)->powered && find(w,coil)->overpowered,"One trooper failed to boost powered coil");
  w.enqueue({w.tick()+1,0,7,CommandKind::SellBuilding,coil});run(w,1);require(!find(w,coil)->coilChargers && !find(w,coil)->overpowered,"Selling coil retained support");
  run(w,100);require(!find(w,coil) && find(w,a) && !find(w,a)->containedBy,"Charging consumed infantry or retained sold target");
  // Automatic support stays local and never overrides an enemy already in range.
  const auto automatic=fixture(base,"[SHK]\nCanPassiveAquire=yes\n");World idle(automatic);const auto tower=idle.spawn("TESLA",{3,0}),idleFoot=idle.spawn("SHK",{2,0});run(idle,50);
  require(find(idle,tower)->coilChargers==1,"Idle nearby trooper did not support coil");
  idle.enqueue({idle.tick()+1,0,1,CommandKind::Stop,idleFoot});run(idle,50);require(!find(idle,tower)->coilChargers,"Stop immediately reacquired automatic coil support");
  idle.enqueue({idle.tick()+1,0,2,CommandKind::AttackEntity,idleFoot,tower});run(idle,50);require(find(idle,tower)->coilChargers==1,"Stopped trooper could not resume charging");
  idle.enqueue({idle.tick()+1,0,3,CommandKind::Stop,idleFoot});run(idle,1);
  idle.enqueue({idle.tick()+1,0,4,CommandKind::Move,idleFoot,0,find(idle,idleFoot)->cell});run(idle,50);require(find(idle,tower)->coilChargers==1,"New move order failed to restore automatic coil support");
  World enemy(automatic);enemy.spawn("TESLA",{3,0});const auto trooper=enemy.spawn("SHK",{2,0});enemy.spawn("E2",{1,0},1);run(enemy,20);
  require(find(enemy,trooper)->activeWeapon=="ELECTRICBOLT","Auto support took priority over enemy");
}
void transportAndCrush(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[HTNK]\nStrength=100000\n");World w(d);const auto host=w.spawn("FV",{2,0}),foot=w.spawn("SHK",{}),target=w.spawn("HTNK",{4,0},1);
  w.enqueue({1,0,1,CommandKind::EnterTransport,foot,host});run(w,170);
  require(find(w,foot)->containedBy==host && find(w,host)->cargo->mode==6 && find(w,host)->cargo->turret==3 && find(w,host)->activeWeapon=="CRELECTRICBOLT","Tesla IFV failed to change gunner/turret");
  w.enqueue({w.tick()+1,0,2,CommandKind::AttackEntity,host,target});run(w,160);require(find(w,target)->hp<100000,"Tesla IFV never fired");
  w.enqueue({w.tick()+1,0,3,CommandKind::Unload,host});run(w,50);require(!find(w,foot)->containedBy && find(w,host)->cargo->mode==0,"Tesla IFV unload retained passenger weapon");
  World crush(d);const auto armored=crush.spawn("SHK",{2,0},1),tank=crush.spawn("HTNK",{});crush.enqueue({1,0,1,CommandKind::Move,tank,0,{4,0}});run(crush,180);
  require(find(crush,armored) && find(crush,armored)->hp==130,"Uncrushable Tesla infantry was crushed");
}
void interruptedSupport(const DefinitionRegistry& base) {
  const auto d=fixture(base);World dead(d);const auto coil=dead.spawn("TESLA",{3,0}),foot=dead.spawn("SHK",{2,0});dead.spawn("NAPOWR",{-10,0});
  dead.enqueue({1,0,1,CommandKind::AttackEntity,foot,coil});run(dead,50);require(find(dead,coil)->overpowered,"Support casualty setup failed");
  const auto killer=dead.spawn("TTNK",{0,2},1);dead.enqueue({dead.tick()+1,1,1,CommandKind::AttackEntity,killer,foot});
  until(dead,[&]{return !find(dead,foot);},"Support trooper never died");require(find(dead,coil)->coilChargers==0 && !find(dead,coil)->overpowered,"Dead trooper retained coil support");
  World board(d);const auto tower=board.spawn("TESLA",{3,0}),passenger=board.spawn("SHK",{2,0}),carrier=board.spawn("HTK",{1,0});
  board.enqueue({1,0,1,CommandKind::AttackEntity,passenger,tower});run(board,50);
  board.enqueue({board.tick()+1,0,2,CommandKind::EnterTransport,passenger,carrier});run(board,1);require(find(board,tower)->coilChargers==0,"Boarding order retained coil support");
  run(board,200);require(find(board,passenger)->containedBy==carrier && !find(board,tower)->coilChargers,"Contained trooper continued charging");
  World invalid(d);const auto source=invalid.spawn("SHK",{}),power=invalid.spawn("NAPOWR",{2,0});invalid.enqueue({1,0,1,CommandKind::AttackEntity,source,power});run(invalid,100);
  require(find(invalid,power)->hp==find(invalid,power)->maxHp && invalid.beams().empty(),"Tesla charged or damaged an ordinary friendly building");
}
void replay(const DefinitionRegistry& base) {
  const auto d=fixture(base);std::uint64_t expected=0;
  for(const int hz:{30,60,144}){World w(d);w.setGameSpeed({GameSpeedMode::Multiplayer,6});const auto tower=w.spawn("TESLA",{3,0}),foot=w.spawn("SHK",{2,0});w.spawn("NAPOWR",{-10,0});
    w.enqueue({1,0,1,CommandKind::AttackEntity,foot,tower});w.enqueue({160,0,2,CommandKind::Move,foot,0,{8,0}});
    for(int f=1;f<=hz*4;++f)w.advance(std::uint64_t(f)*1000000/hz-std::uint64_t(f-1)*1000000/hz);
    require(w.tick()==240,"Tesla replay frame count mismatch");if(expected)require(w.stateHash()==expected,"Tesla state depends on presentation rate");else expected=w.stateHash();}
}
}
int main(int argc,char** argv){try{require(argc==2,"Expected settings path");const auto d=DefinitionRegistry::load(argv[1]);definitions(d);production(d);combat(d);charging(d);transportAndCrush(d);interruptedSupport(d);replay(d);std::cout<<"Tesla units passed: native definitions, production, bolts, electrocution, coil support, IFV, crushing and replay\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
