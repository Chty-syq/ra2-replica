#include "simulation/world.h"
#include "test_support.h"
#include <algorithm>
#include <iostream>
#include <set>
using namespace ra2;
namespace {
std::optional<EntitySnapshot> find(const World& w,EntityId id) {for(const auto& e:w.snapshot())if(e.id==id)return e;return {};}
void run(World& w,int n){while(n-->0)w.step();}
template<class Predicate> void until(World& w,Predicate predicate,const char* message,int limit=1200) {
  for(int i=0;i<limit && !predicate();++i)w.step();require(predicate(),message);
}
DefinitionRegistry fixture(const DefinitionRegistry& base,std::string extra={},std::string profile={}) {
  auto rules=base.rules(),replica=base.replica();
  rules.overlay(IniDocument::parse("[E1]\nCanPassiveAquire=no\n[E2]\nCanPassiveAquire=no\n[FV]\nCanPassiveAquire=no\n[HTK]\nCanPassiveAquire=no\n[HTNK]\nCanPassiveAquire=no\n[SNIPE]\nCanPassiveAquire=no\n[FLAKT]\nCanPassiveAquire=no\n[JUMPJET]\nCanPassiveAquire=no\n"+extra));
  replica.overlay(IniDocument::parse("[Simulation]\nProductionFramesOverride=4\n"+profile));
  return DefinitionRegistry::compile(rules,base.art(),replica);
}
void definitions(const DefinitionRegistry& d) {
  const auto& flak=d.unit("FLAKT");const auto& rock=d.unit("JUMPJET");
  require(flak.unsupported.empty() && rock.unsupported.empty(),"AA infantry definitions rejected");
  require(flak.infantry->bodyFrames==301 && rock.infantry->bodyFrames==427,"Original infantry frame counts lost");
  require(!flak.flight && rock.flight && !rock.groundMobile,"Jumpjet still uses ground locomotion");
  require(rock.flight->height==500 && rock.flight->speed==30 && rock.flight->climb==20 && rock.flight->crash==25,"Original flight overrides lost");
  require(d.weaponForTarget(flak,false)->id=="FLAKGUYGUN" && d.weaponForTarget(flak,true)->id=="FLAKGUYAAGUN","Flak ground/air selection failed");
  require(!d.weaponForTarget(d.unit("E1"),true) && !d.weaponForTarget(d.unit("SNIPE"),true),"Ground-only guns can target air");
  require(d.weaponForTarget(d.unit("HTK"),true)->id=="FLAKTRACKAAGUN" && d.weaponForTarget(d.unit("FV"),true)->id=="HOVERMISSILE","Existing carrier AA did not select correct weapon");
  require(!d.weaponForTarget(d.unit("FV"),true,false,2) && !d.weaponForTarget(d.unit("FV"),true,false,3),"IFV gunner target domains ignored");
  require(d.sidebarUnit("flkticon","Soviet")->id=="FLAKT" && d.sidebarUnit("jjeticon","Allied")->id=="JUMPJET","Original cameos not connected");
  require(canonicalId(flak.visual.faction)=="SOVIET" && canonicalId(rock.visual.faction)=="ALLIED","Infantry faction rules lost");
}
void production(const DefinitionRegistry& base) {
  for(const bool soviet:{false,true}) {
    const auto d=fixture(base,{},std::string("[Simulation]\nSandbox=no\nStartingCredits=2000\nPlayerHouse=")+(soviet?"Russians":"British"));
    World w(d);const auto factory=w.spawn(soviet?"NAHAND":"GAPILE",{});
    const auto type=soviet?"FLAKT":"JUMPJET";
    w.enqueue({1,0,1,CommandKind::Produce,factory,0,{},type});run(w,10);
    require(w.credits()==2000,"Radar prerequisite was bypassed");
    require(w.spawn(soviet?"NARADR":"GAAIRC",{8,0}),"Radar setup failed");
    w.enqueue({w.tick()+1,0,2,CommandKind::Produce,factory,0,{},type});run(w,200);
    int count=0;for(const auto& e:w.snapshot())if(e.type==type){++count;require(!e.takingOff && (!e.airborne || e.heightLeptons==500),"Produced jumpjet did not take off");}
    require(count==1 && w.credits()==(soviet?1700:1400),"AA infantry production/cost failed");
  }
}
void layersAndOrders(const DefinitionRegistry& base) {
  const auto d=fixture(base);World w(d);const auto air=w.spawn("JUMPJET",{}),tank=w.spawn("HTNK",{});
  require(air && tank && w.occupants({})==std::vector<EntityId>{tank} && w.airOccupants({})==std::vector<EntityId>{air},"Air and ground occupancy overlap");
  const auto building=w.spawn("GAPOWR",{3,0});require(building,"Building setup failed");
  for(const int x:{1,2,5,6}){TerrainSurface water;water.land="Water";w.setTerrain({x,0},water);w.setBuildable({x,0},false);}
  w.enqueue({1,0,1,CommandKind::Move,air,0,{6,0}});run(w,24);
  require(find(w,air)->position==find(w,air)->previous && find(w,air)->heightLeptons==480,"Takeoff moved before reaching cruise height");
  bool aboveRoof=false,fly=false;
  for(int i=0;i<450;++i){w.step();const auto e=find(w,air);fly|=e->infantry->sequence=="FLY";if(e->cell==TileCoord{3,0})aboveRoof|=e->heightLeptons>=d.unit("GAPOWR").buildingHeight+500;}
  require(aboveRoof && fly && find(w,air)->cell==TileCoord{6,0} && find(w,air)->activity==Activity::Idle && find(w,air)->heightLeptons==500,"Flight did not cross water/building or settle to hover");
  w.enqueue({w.tick()+1,0,2,CommandKind::Move,air,0,{14,0}});run(w,30);
  w.enqueue({w.tick()+1,0,3,CommandKind::Stop,air});run(w,1);const auto stopped=find(w,air)->position;run(w,30);
  require(find(w,air)->position==stopped && find(w,air)->infantry->sequence=="HOVER","Stop did not cancel flight");
  w.enqueue({w.tick()+1,0,4,CommandKind::Move,air,0,{0,4}});run(w,350);
  require(find(w,air)->cell==TileCoord{0,4} && w.airOccupants({6,0}).empty(),"New flight order retained air reservation");
  require(w.spawn("JUMPJET",{6,0}) && w.spawn("JUMPJET",{6,0}) && w.spawn("JUMPJET",{6,0}) && !w.spawn("JUMPJET",{6,0}),"Air subcell capacity/release is broken");
  const auto host=w.spawn("HTK",{0,5});
  w.enqueue({w.tick()+1,0,5,CommandKind::EnterTransport,air,host});run(w,100);
  require(!find(w,air)->containedBy && !find(w,air)->buildingInteraction,"BalloonHover jumpjet boarded a ground transport");
  World group(d);std::vector<EntityId> ids;
  for(int i=0;i<6;++i)ids.push_back(group.spawn("JUMPJET",{i,0}));
  group.enqueue({1,0,1,CommandKind::GroupMove,0,0,{6,5},{},ids});run(group,650);
  std::set<Position> positions;for(auto id:ids){const auto e=find(group,id);require(e->activity==Activity::Idle && std::abs(e->cell.x-6)<=1 && std::abs(e->cell.y-5)<=1,"Air group failed to arrive");positions.insert(e->position);}
  require(positions.size()==ids.size(),"Air group overlapped final subcells");
}
void targetDomainsAndTiming(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[JUMPJET]\nStrength=100000\n[E2]\nStrength=100000\nFearless=yes\n");
  for(const bool airborne:{false,true}) {
    World w(d);const auto shooter=w.spawn("FLAKT",{}),target=w.spawn(airborne?"JUMPJET":"E2",{3,0},1);
    run(w,30);w.enqueue({w.tick()+1,0,1,CommandKind::AttackEntity,shooter,target});
    std::vector<Tick> times;std::set<Position> endpoints;
    for(int i=0;i<180;++i){w.step();for(const auto& shot:w.shots())if(shot.sourceType=="FLAKT" && shot.started==w.tick()) {
      require(shot.weaponId==(airborne?"FLAKGUYAAGUN":"FLAKGUYGUN"),"Flak selected the wrong weapon");
      require(shot.started==find(w,shooter)->infantry->animationStarted+3,"Flak FireUp=3 ignored");
      require(shot.targetHeightLeptons==(airborne?500:0),"Flak target height was projected onto ground");
      const auto p=find(w,target)->position;const auto dx=shot.target.x-p.x,dy=shot.target.y-p.y;
      require(dx*dx+dy*dy<=std::int64_t(airborne?2000:1000)*(airborne?2000:1000),"Flak scatter exceeds rule radius");
      times.push_back(shot.started);endpoints.insert(shot.target);
    }}
    require(times.size()>=5 && times[1]-times[0]==(airborne?25:20) && endpoints.size()>1,"Flak ROF/scatter failed");
    require(find(w,target)->hp<find(w,target)->maxHp,"Flak never damaged intended target domain");
  }
  for(const auto gun:{"E1","SNIPE","HTNK"}) {
    World w(d);const auto shooter=w.spawn(gun,{}),target=w.spawn("JUMPJET",{3,0},1);run(w,30);
    w.enqueue({w.tick()+1,0,1,CommandKind::AttackEntity,shooter,target});run(w,180);
    require(find(w,target)->hp==find(w,target)->maxHp && w.shots().empty(),"Ground-only manual attack hit airborne infantry");
  }
  World rock(d);const auto a=rock.spawn("JUMPJET",{}),b=rock.spawn("E2",{3,0},1);
  rock.enqueue({1,0,1,CommandKind::AttackEntity,a,b});std::vector<Tick> times;
  for(int i=0;i<180;++i){rock.step();for(const auto& shot:rock.shots())if(shot.started==rock.tick()) {
    require(shot.weaponId=="20MM" && shot.started==find(rock,a)->infantry->animationStarted+2 && shot.heightLeptons>500,"Air firing origin/FireUp=2 failed");
    require(!find(rock,a)->takingOff && find(rock,a)->infantry->sequence=="FIREFLY","Jumpjet fired during takeoff or wrong animation");times.push_back(shot.started);
  }}
  require(times.size()>=4 && times[1]-times[0]==30 && find(rock,b)->hp<find(rock,b)->maxHp,"Jumpjet gun/ROF failed");
  World splash(d);const auto ground=splash.spawn("E2",{3,0},1),air=splash.spawn("JUMPJET",{3,0},1),flak=splash.spawn("FLAKT",{});
  run(splash,30);splash.enqueue({splash.tick()+1,0,1,CommandKind::AttackEntity,flak,air});run(splash,200);
  require(find(splash,ground)->hp==find(splash,ground)->maxHp,"AA splash damaged ground unit below target");
  splash.enqueue({splash.tick()+1,0,2,CommandKind::AttackEntity,flak,ground});const auto hp=find(splash,air)->hp;run(splash,200);
  require(find(splash,air)->hp==hp && find(splash,ground)->hp<find(splash,ground)->maxHp,"Ground splash crossed air layer");
}
void carriersAndDeath(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[JUMPJET]\nStrength=100000\n");
  for(const auto gun:{"FV","HTK"}) {
    World w(d);const auto shooter=w.spawn(gun,{}),target=w.spawn("JUMPJET",{7,0},1);run(w,30);
    w.enqueue({w.tick()+1,0,1,CommandKind::AttackEntity,shooter,target});bool shot=false,homing=false;
    for(int i=0;i<240;++i){w.step();for(const auto& s:w.shots())if(s.sourceType==gun){shot=true;require(s.targetHeightLeptons==500,"Carrier AA aims at ground");homing|=s.weaponId=="HOVERMISSILE" && s.heightLeptons>200;}}
    require(shot && find(w,target)->hp<find(w,target)->maxHp && (std::string_view(gun)!="FV" || homing),"Carrier failed to hit air target");
    if(std::string_view(gun)=="FV")require(find(w,shooter)->cell==TileCoord{},"IFV ignored AirRangeBonus=4");
  }
  World ifv(d);const auto host=ifv.spawn("FV",{2,0}),passenger=ifv.spawn("FLAKT",{}),target=ifv.spawn("HTNK",{4,0},1);
  ifv.enqueue({1,0,1,CommandKind::EnterTransport,passenger,host});run(ifv,180);
  require(find(ifv,host)->cargo->mode==3 && find(ifv,host)->cargo->turret==1 && find(ifv,passenger)->containedBy==host,"Flak IFV passenger mode failed");
  ifv.enqueue({ifv.tick()+1,0,2,CommandKind::AttackEntity,host,target});run(ifv,180);require(find(ifv,target)->hp<find(ifv,target)->maxHp,"Flak IFV could not fire at ground");
  const auto lethal=fixture(base,"[FlakGuyAAGun]\nDamage=100000\n[CombatDamage]\nBallisticScatter=0\n");World dead(lethal);
  const auto victim=dead.spawn("JUMPJET",{3,0},1),gun=dead.spawn("FLAKT",{});run(dead,30);
  dead.enqueue({dead.tick()+1,0,1,CommandKind::AttackEntity,gun,victim});
  until(dead,[&]{return !find(dead,victim);},"Lethal AA did not kill jumpjet");
  require(dead.airOccupants({3,0}).empty() && dead.infantryCorpses().size()==1 && dead.infantryCorpses().front().sequence=="AIRDEATHSTART","Air death retained occupancy or skipped first animation");
  bool falling=false,finish=false;int last=10000;
  for(int i=0;i<45;++i){for(const auto& c:dead.infantryCorpses()){require(c.heightLeptons<=last,"Corpse rose during falling");last=c.heightLeptons;falling|=c.sequence=="AIRDEATHFALLING";finish|=c.sequence=="AIRDEATHFINISH";}dead.step();}
  require(falling && finish && dead.infantryCorpses().empty() && dead.spawn("HTNK",{3,0}),"Air death failed to complete or blocked ground");
}
void acquisitionAndChasing(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[FLAKT]\nCanPassiveAquire=yes\n[JUMPJET]\nStrength=100000\n[E1]\nCanPassiveAquire=yes\n[CombatDamage]\nBallisticScatter=0\n");
  World automatic(d);const auto target=automatic.spawn("JUMPJET",{7,0},1),flak=automatic.spawn("FLAKT",{}),gi=automatic.spawn("E1",{6,0});
  bool flakShot=false,giShot=false;
  for(int i=0;i<150;++i){automatic.step();for(const auto& s:automatic.shots()){flakShot|=s.sourceType=="FLAKT";giShot|=s.sourceType=="E1";}}
  require(flakShot && !giShot && find(automatic,target)->hp<find(automatic,target)->maxHp && find(automatic,flak) && find(automatic,gi),"Automatic AA acquired wrong domain/range");
  const auto manual=fixture(base,"[JUMPJET]\nStrength=100000\n[CombatDamage]\nBallisticScatter=0\n");World chase(manual);
  const auto runner=chase.spawn("JUMPJET",{10,0},1),pursuer=chase.spawn("FLAKT",{});
  chase.enqueue({1,0,1,CommandKind::AttackEntity,pursuer,runner});run(chase,40);
  chase.enqueue({chase.tick()+1,1,1,CommandKind::Move,runner,0,{20,0}});run(chase,650);
  require(find(chase,runner)->cell==TileCoord{20,0} && find(chase,pursuer)->cell.x>=12 && find(chase,runner)->hp<find(chase,runner)->maxHp,"Ground AA failed to follow moving airborne target");
  World air(manual);const auto a=air.spawn("JUMPJET",{}),b=air.spawn("JUMPJET",{3,0},1);
  air.enqueue({1,0,1,CommandKind::AttackEntity,a,b});run(air,150);
  require(find(air,b)->hp<find(air,b)->maxHp && find(air,a)->heightLeptons==500,"Jumpjet could not attack airborne infantry");
}
void replay(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[JUMPJET]\nStrength=100000\n");std::uint64_t expected=0;
  for(const int hz:{30,60,144}) {
    World w(d);w.setGameSpeed({GameSpeedMode::Multiplayer,6});
    const auto air=w.spawn("JUMPJET",{}),target=w.spawn("JUMPJET",{6,0},1),flak=w.spawn("FLAKT",{2,0});
    w.enqueue({1,0,1,CommandKind::Move,air,0,{8,4}});w.enqueue({1,0,2,CommandKind::AttackEntity,flak,target});
    w.enqueue({160,0,3,CommandKind::AttackEntity,air,target});
    for(int f=1;f<=hz*6;++f)w.advance(std::uint64_t(f)*1000000/hz-std::uint64_t(f-1)*1000000/hz);
    require(w.tick()==360,"Air replay frame count changed");
    if(expected)require(w.stateHash()==expected,"Render rate changed air/scatter simulation");else expected=w.stateHash();
    w.setGameSpeed({GameSpeedMode::Multiplayer,3});for(const auto& e:w.snapshot())require(e.previousHeightLeptons==e.heightLeptons,"Speed change interpolates old altitude");
  }
}
}
int main(int argc,char** argv) {
  try {
    require(argc==2,"Expected settings path");const auto d=DefinitionRegistry::load(argv[1]);
    definitions(d);production(d);layersAndOrders(d);targetDomainsAndTiming(d);carriersAndDeath(d);acquisitionAndChasing(d);replay(d);
    std::cout<<"Air defense regressions passed\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
