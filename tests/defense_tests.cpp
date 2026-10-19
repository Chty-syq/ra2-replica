#include "building_system.h"
#include "sidebar.h"
#include "presentation/world_view.h"
#include "simulation/world.h"
#include "test_support.h"
#include <iostream>
#include <set>
using namespace ra2;
namespace {
EntitySnapshot find(const World& w,EntityId id) {for(const auto& e:w.snapshot())if(e.id==id)return e;throw std::runtime_error("Missing defense test entity");}
void run(World& w,int frames) {while(frames-->0)w.step();}
DefinitionRegistry fixture(const DefinitionRegistry& base,std::string extra={}) {
  auto rules=base.rules();rules.overlay(IniDocument::parse("[HTNK]\nCanPassiveAquire=no\nStrength=100000\n[JUMPJET]\nCanPassiveAquire=no\nStrength=100000\n[E2]\nCanPassiveAquire=no\nStrength=100000\nFearless=yes\n"+extra));
  return DefinitionRegistry::compile(rules,base.art(),base.replica());
}
void catalogAndConstruction(const DefinitionRegistry& d) {
  const auto art=ArtIni::fromDocument(d.art(),d.replica(),d.rules());
  const std::set<std::string> expected{"GAWALL","GAPILL","NASAM","ATESLA","GAGAP","GTGCAN","GASPYSAT","GACSPH","GAWEAT","NAWALL","NALASR","NAFLAK","TESLA","NAIRON","NAMISL","NAPSIS"};
  SidebarAssets sidebar;sidebar.panelIcons[1]=makeSidebarPanel(SidebarTab::Defense);updateSidebarAvailability(sidebar,d);
  std::set<std::string> mapped;
  for(const auto& icon:sidebar.panelIcons[1]) {
    const auto id=buildingIdForIcon(icon.id,icon.faction);
    require(id && expected.contains(*id) && mapped.insert(*id).second,"Defense cameo maps to a power, duplicate or invalid type: "+icon.id);
    require(icon.available && icon.detail.find(*id)!=std::string::npos,"Buildable defense is disabled or mislabeled: "+*id);
    const auto& type=d.unit(*id);const auto visual=art.building(*id);
    require(type.unsupported.empty() && visual.id==type.id && visual.foundation && visual.foundation->width==type.foundationWidth && visual.foundation->height==type.foundationHeight,"Logical/art alias foundation mismatch: "+*id);
    World w(d);require(w.enqueue({1,0,1,CommandKind::PlaceBuilding,0,0,{4,6},*id}),"Defense placement command rejected");w.step();
    const auto entity=w.structureAt({4,6});require(entity,"Defense did not enter construction: "+*id);
    const auto view=makeWorldView(w,d,0);require(view.buildings.size()==1 && view.buildings[0].assetId==*id,"Presentation lost logical building id");
    require(find(w,entity).activity==(type.wall?Activity::Idle:Activity::Constructing),"Wall or buildup phase incorrect");
    for(int y=0;y<type.foundationHeight;++y)for(int x=0;x<type.foundationWidth;++x)
      require(w.structureAt({4+x,6+y})==entity,"Defense Foundation occupancy incomplete: "+*id);
    require(!w.spawn("HTNK",{4,6}) && !w.spawn(*id,{4,6}),"Defense placement overlaps a structure");
    run(w,d.profile().buildupFrames);require(find(w,entity).activity==Activity::Idle,"Defense construction never completes: "+*id);
  }
  require(mapped==expected,"Missing defense building");
  for(const auto id:{"GACSPH","GAWEAT","NAIRON","NAMISL","GASPYSAT","GAGAP","NAPSIS"})
    require(d.unit(id).support()==SupportStatus::Partial,"Unimplemented special mechanic incorrectly marked Ready");
  require(d.unit("TESLA").fireDelay==28 && d.unit("ATESLA").fireDelay==28 && d.weapon("COILBOLT").electricBolt,"Original charging/electric rules missing");
  require(d.unit("SHK").unsupported.empty() && d.chargeWeapon(d.unit("SHK"),d.unit("TESLA")),"Tesla infantry/coil charging definition missing");
}
void attacks(const DefinitionRegistry& d) {
  for(const auto type:{"GAPILL","NALASR","NASAM","NAFLAK","ATESLA","TESLA","GTGCAN"})for(const bool air:{false,true}) {
    World w(d);w.spawn("NANRCT",{-15,-15});const auto gun=w.spawn(type,{}),victim=w.spawn(air?"JUMPJET":"HTNK",{0,4},1);
    require(gun && victim,"Defense combat setup failed");run(w,350);
    const bool canAttack=d.weaponForTarget(d.unit(type),air)!=nullptr;
    require((find(w,victim).hp<find(w,victim).maxHp)==canAttack,std::string(type)+(air?" air":" ground")+" target filtering/damage failed");
    if(d.unit(type).turret && canAttack)require(find(w,gun).turretHeading!=0,"Defense turret did not aim");
  }
  World minRange(d);minRange.spawn("NANRCT",{-15,-15});minRange.spawn("GTGCAN",{});
  const auto tooClose=minRange.spawn("HTNK",{2,0},1);run(minRange,150);
  require(minRange.shots().empty() && find(minRange,tooClose).hp==100000 && d.weapon("GRANDCANNONWEAPON").minimumRangeMilliCells==3000,"Grand Cannon ignored MinimumRange=3");
  World constructing(d);constructing.spawn("NANRCT",{-15,-15});constructing.spawn("TESLA",{},0,true);constructing.spawn("HTNK",{4,0},1);
  run(constructing,d.profile().buildupFrames-1);require(constructing.beams().empty() && constructing.shots().empty(),"Defense fired during buildup");
}
void chargingAndPower(const DefinitionRegistry& d) {
  for(const auto type:{"GAPILL","NALASR"}) {
    World independent(d);independent.spawn("GAWEAT",{-10,5});const auto gun=independent.spawn(type,{}),target=independent.spawn("HTNK",{0,4},1);
    run(independent,100);require(!independent.power().online() && find(independent,gun).powered && find(independent,target).hp<100000,"Zero-drain machine gun stopped during a power outage");
  }
  for(const auto type:{"ATESLA","TESLA"}) {
    World w(d);w.spawn("NANRCT",{-15,-15});const auto gun=w.spawn(type,{}),victim=w.spawn("HTNK",{4,0},1);
    run(w,28);require(w.beams().empty() && find(w,gun).firingStarted==1,"Defense skipped delayed-fire charge");
    w.step();require(w.beams().size()==1 && w.beams()[0].started==29 && find(w,victim).hp==100000,"Beam must begin at charge frame 28 before damage");
    w.step();require(find(w,victim).hp<100000,"Charged defense never damaged target");
    std::set<Tick> fired{29};for(int i=0;i<300;++i){w.step();for(const auto& beam:w.beams())fired.insert(beam.started);}
    auto it=fired.begin();const auto first=*it++;require(it!=fired.end() && *it-first==static_cast<Tick>(d.weapon(d.unit(type).primary).rof),"Delayed-fire animation was added to every ROF");
  }
  World power(d);const auto coil=power.spawn("TESLA",{});power.spawn("HTNK",{4,0},1);run(power,50);
  require(!power.power().online() && !find(power,coil).powered && power.beams().empty(),"Unpowered Tesla continued firing");
  require(power.spawn("NAPOWR",{-5,0}),"Power plant setup failed");run(power,20);
  require(power.power().online() && find(power,coil).powered && power.beams().empty() && find(power,coil).firingStarted==51,"Power restoration skipped a new charge");
  run(power,9);require(!power.beams().empty(),"Restored power did not reactivate Tesla");
  World interrupted(d);interrupted.spawn("NAPOWR",{-5,0});const auto charging=interrupted.spawn("TESLA",{});interrupted.spawn("HTNK",{4,0},1);
  run(interrupted,15);interrupted.spawn("GAWEAT",{-10,5});run(interrupted,50);
  require(!interrupted.power().online() && interrupted.beams().empty() && !find(interrupted,charging).firingStarted,"Power loss did not cancel an in-progress charge");
  interrupted.spawn("NANRCT",{-15,-15});run(interrupted,28);require(interrupted.beams().empty(),"Power restoration resumed a stale charge");
  interrupted.step();require(!interrupted.beams().empty(),"Power restoration never restarted interrupted firing");
  const auto cancel=fixture(d,"[TESLA]\nCanPassiveAquire=no\n");World stopped(cancel);stopped.spawn("NAPOWR",{-5,0});
  const auto gun=stopped.spawn("TESLA",{}),target=stopped.spawn("HTNK",{4,0},1);
  stopped.enqueue({1,0,1,CommandKind::AttackEntity,gun,target});run(stopped,15);stopped.enqueue({16,0,2,CommandKind::Stop,gun});run(stopped,30);
  require(stopped.beams().empty() && find(stopped,gun).firingStarted==0,"Stopping a charge left firing visuals/damage active");
}
void walls(const DefinitionRegistry& d) {
  World connected(d);const auto center=connected.spawn("GAWALL",{});
  for(const auto cell:{TileCoord{0,-1},{1,0},{0,1},{-1,0}})require(connected.spawn("GAWALL",cell),"Wall connection setup failed");
  require(find(connected,center).wallConnections==15 && connected.map().cell({}).surface.wallHeightLeptons==128,"Wall adjacency or projectile obstruction missing");
  require(!connected.spawn("HTNK",{}),"Wall no longer blocks ground units");
  World separate(d);const auto a=separate.spawn("GAWALL",{});separate.spawn("NAWALL",{1,0});
  require(find(separate,a).wallConnections==0,"Different wall types incorrectly connect");
  World w(d);w.spawn("NANRCT",{-15,-15});w.spawn("NALASR",{});const auto target=w.spawn("HTNK",{4,0},1),wall=w.spawn("GAWALL",{2,0},1);
  run(w,150);require(find(w,target).hp==100000 && find(w,wall).hp==find(w,wall).maxHp,"Wall failed to stop machine-gun fire or took forbidden SA damage");
  const auto coil=w.spawn("TESLA",{-3,0});w.enqueue({w.tick()+1,0,1,CommandKind::AttackEntity,coil,wall});
  for(int i=0;i<900 && w.structureAt({2,0});++i)w.step();
  require(w.structureAt({2,0})==0 && w.map().cell({2,0}).surface.wallHeightLeptons==0 && !w.map().cell({2,0}).occupied,"Destroyed wall retained collision or occupancy");
  run(w,100);require(find(w,target).hp<100000,"Destroyed wall still blocks defense projectiles");
}
std::uint64_t replay(const DefinitionRegistry& d,int fps) {
  World w(d);w.spawn("NANRCT",{-15,-15});w.spawn("TESLA",{});w.spawn("ATESLA",{0,3});w.spawn("NASAM",{0,6});w.spawn("HTNK",{4,0},1);w.spawn("JUMPJET",{4,6},1);
  for(int frame=0;frame<fps*8;++frame)w.advance(std::uint64_t(frame+1)*1000000/fps-std::uint64_t(frame)*1000000/fps);
  return w.stateHash();
}
}
int main(int argc,char** argv) {
  try {require(argc==2,"Expected settings folder");const auto base=DefinitionRegistry::load(argv[1]);const auto d=fixture(base);
    catalogAndConstruction(d);attacks(d);chargingAndPower(d);walls(d);
    require(replay(d,30)==replay(d,60) && replay(d,60)==replay(d,144),"Defense simulation depends on rendering FPS");
    std::cout<<"Defense regressions passed: 16 buildings, mapping/buildup, ground/air combat, delayed fire, power, walls, deterministic replay\n";return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
