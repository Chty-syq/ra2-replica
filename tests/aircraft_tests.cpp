#include "config/definitions.h"
#include "simulation/world.h"
#include "simulation/aircraft.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <set>
using namespace ra2;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::optional<EntitySnapshot> get(const World& w,EntityId id){for(const auto& e:w.snapshot())if(e.id==id)return e;return {};}
void cmd(World& w,CommandKind k,EntityId id,EntityId target=0,TileCoord cell={},std::string type={}){static std::uint64_t seq=0;require(w.enqueue({w.tick()+1,0,++seq,k,id,target,cell,std::move(type)}),"command rejected");}
void frames(World& w,int n){while(n-->0)w.step();}
DefinitionRegistry fixture(const std::filesystem::path& path){auto d=DefinitionRegistry::load(path);auto replica=d.replica();replica.overlay(IniDocument::parse("[Simulation]\nProductionFramesOverride=1\n"));return DefinitionRegistry::compile(d.rules(),d.art(),replica);}
EntityId plane(const World& w,int index=0){for(const auto& e:w.snapshot())if(e.aircraft && e.type=="ORCA" && index--==0)return e.id;return 0;}
}
int main(int argc,char** argv)try {
 require(argc==2,"settings path required");auto d=fixture(argv[1]);
 for(const auto* id:{"ORCA","BEAG","SHAD","ZEP"})require(d.unit(id).support()!=SupportStatus::Unsupported,"aircraft unsupported");
 require(d.unit("ORCA").aircraft->ammo==1 && d.unit("ORCA").aircraft->reloadFrames==270,"native ammo/reload");
 require(d.unit("GAAIRC").airport->offsets.size()==4 && d.unit("AMRADR").airport->offsets[2][0]==256,"native four docking offsets");
 {
  // JumpjetSpeed is already leptons per game frame. Speed for a fixed-wing
  // aircraft goes through the original percent-to-255 import instead.
  for(const auto* id:{"SHAD","ZEP","ORCA"}) {
    const auto& config=*d.unit(id).aircraft;aircraft::State state;
    state.position.z=state.previous.z=config.movement.height*motion::lepton;
    state.speedMilli=100000;const auto initial=state.position;
    for(int i=0;i<20;++i)aircraft::advance(state,config,{100*256*motion::lepton,0,0},config.movement.height,16);
    const int nativeSpeed=id==std::string("SHAD")?30:id==std::string("ZEP")?5:35;
    require(state.position.x-initial.x==20*nativeSpeed*motion::lepton,"Jumpjet speed was imported as tracked Speed (aircraft flies too fast)");
  }
 }
 require(d.unit("SHAD").passengers==5 && d.unit("ZEP").aircraft->movement.height==750,"native jumpjet stats");
 require(d.factoryMatches(d.unit("GAAIRC"),d.unit("ORCA")) && d.factoryMatches(d.unit("GAWEAP"),d.unit("SHAD")) && d.factoryMatches(d.unit("NAWEAP"),d.unit("ZEP")),"air factory categories");
 require(!d.factoryMatches(d.unit("GAWEAP"),d.unit("ORCA")) && !d.factoryMatches(d.unit("GAAIRC"),d.unit("SHAD")),"wrong air factory accepted");
 require(!d.allowedHouse(d.unit("BEAG"),"British") && d.allowedHouse(d.unit("BEAG"),"Alliance") && !d.allowedHouse(d.unit("ORCA"),"Alliance"),"native Korean aircraft restriction");
 {
  World w(d);const auto airport=w.spawn("GAAIRC",{4,4});w.spawn("GAPOWR",{0,0});
  for(int i=0;i<5;++i)cmd(w,CommandKind::Produce,airport,0,{},"ORCA");frames(w,8);
  std::set<int> slots;std::set<Position> positions;int count=0;
  for(const auto& e:w.snapshot())if(e.type=="ORCA"){require(e.aircraft && e.aircraft->airport==airport && !e.airborne,"new fighter not docked");slots.insert(e.aircraft->slot);positions.insert(e.position);++count;}
  require(count==4 && slots.size()==4 && positions.size()==4 && !get(w,airport)->queueSize,"airport overbooked or stacked");
  const auto id=plane(w),target=w.spawn("NAIRON",{16,4},1);require(id && target,"attack scenario");
  cmd(w,CommandKind::AttackEntity,id,target);int missiles=0;std::set<std::uint64_t> seen;bool returned=false,empty=false,refilled=false;
  for(int i=0;i<1800;++i){w.step();for(const auto& shot:w.shots())if(shot.sourceType=="ORCA" && seen.insert(shot.id).second)++missiles;
   const auto p=get(w,id);require(bool(p),"fighter lost unexpectedly");empty|=p->aircraft->ammo==0;
   if(empty && p->aircraft->mission==AircraftMission::Landed){returned=true;refilled|=p->aircraft->ammo==1;}
   if(refilled)break;
  }
  require(missiles==2,"one ammo must launch one native two-missile salvo");require(empty && returned && refilled,"fighter attack-return-land-reload failed");
  require(get(w,target)->hp<get(w,target)->maxHp,"fighter missiles failed to damage ground target");
  cmd(w,CommandKind::Move,id,0,{12,9});frames(w,150);require(get(w,id)->airborne,"fighter failed to take off");
  cmd(w,CommandKind::Stop,id);frames(w,1600);require(get(w,id)->aircraft->mission==AircraftMission::Landed,"stop must return to airport");
 }
 {
  World w(d);w.spawn("GAAIRC",{4,4});w.spawn("GAPOWR",{});const auto id=w.spawn("ORCA",{4,4}),target=w.spawn("NAIRON",{15,4},1);
  cmd(w,CommandKind::AttackEntity,id,target);bool fired=false;
  for(int i=0;i<700 && !fired;++i){w.step();const auto shots=w.shots();fired=std::any_of(shots.begin(),shots.end(),[](const auto& shot){return shot.sourceType=="ORCA";});}
  require(fired && get(w,id)->aircraft->ammo==0,"first missile did not consume native ammo");
  cmd(w,CommandKind::Stop,id);w.step();require(get(w,id)->aircraft->ammo==0,"cancelled burst refunded fired ammo");
 }
 {
  World w(d);const auto heli=w.spawn("SHAD",{4,4}),gi=w.spawn("E1",{5,4});require(heli && gi,"helicopter spawn");
  cmd(w,CommandKind::EnterTransport,gi,heli);frames(w,180);require(get(w,gi)->containedBy==heli,"night hawk boarding");
  for(int x=8;x<=13;++x)for(int y=0;y<15;++y){TerrainSurface t;t.land="Water";w.setTerrain({x,y},t);}
  cmd(w,CommandKind::Move,heli,0,{11,4});frames(w,200);require(get(w,heli)->airborne,"helicopter crossed water incorrectly");
  cmd(w,CommandKind::Unload,heli);frames(w,300);require(get(w,gi) && !get(w,gi)->containedBy && !get(w,heli)->airborne,"helicopter did not find nearby dry landing/unload");
  require(w.map().cell(get(w,gi)->cell).surface.land!="Water","passenger unloaded in sea");
  const auto target=w.spawn("SENGINEER",{16,7},1);cmd(w,CommandKind::AttackEntity,heli,target);frames(w,300);require(!get(w,target) || get(w,target)->hp<get(w,target)->maxHp,"helicopter gun failed");
 }
 {
  World w(d);const auto zep=w.spawn("ZEP",{4,4}),target=w.spawn("NAIRON",{12,4},1);require(zep && target,"kirov scenario");
  cmd(w,CommandKind::AttackEntity,zep,target);bool bomb=false;frames(w,20);
  for(int i=0;i<650;++i){w.step();for(const auto& shot:w.shots())if(shot.sourceType=="ZEP"){bomb=true;require(shot.position.x==shot.start.x && shot.position.y==shot.start.y,"Kirov bomb steered horizontally");}}
  require(bomb && get(w,zep)->airborne && get(w,zep)->heightLeptons>=750,"Kirov flight/bomb missing");
  require(!get(w,target) || get(w,target)->hp<get(w,target)->maxHp,"Kirov bomb splash missed target");
 }
 {
  World w(d);const auto airport=w.spawn("GAAIRC",{4,4}),other=w.spawn("AMRADR",{15,4});w.spawn("GAPOWR",{0,0});
  cmd(w,CommandKind::Produce,airport,0,{},"BEAG");frames(w,3);EntityId eagle=0;for(const auto& e:w.snapshot())if(e.type=="BEAG")eagle=e.id;
  require(eagle,"black eagle production");cmd(w,CommandKind::EnterBuilding,eagle,other);frames(w,1300);require(get(w,eagle)->aircraft->airport==other && !get(w,eagle)->airborne,"manual airport transfer failed");
  cmd(w,CommandKind::SellBuilding,airport);frames(w,80);require(get(w,eagle).has_value(),"transferred aircraft died with previous airport");
  cmd(w,CommandKind::SellBuilding,other);frames(w,80);require(!get(w,eagle),"docked plane survived sold airport");
 }
 {
  for(const auto* id:{"SHAD","ZEP"}) {
    World w(d);const auto factory=w.spawn(id==std::string("SHAD")?"GAWEAP":"NAWEAP",{4,4});cmd(w,CommandKind::Produce,factory,0,{},id);frames(w,2);
    EntityId unit=0;for(const auto& e:w.snapshot())if(e.type==id)unit=e.id;
    require(unit && get(w,unit)->exitFactory==factory && get(w,factory)->factoryPhase==FactoryExitPhase::Preparing,"aircraft bypassed war factory opening");
    cmd(w,CommandKind::Move,unit,0,{14,8});
    for(int i=0;i<1200 && (get(w,unit)->exitFactory || get(w,unit)->cell!=TileCoord{14,8});++i)w.step();
    require(get(w,unit) && !get(w,unit)->exitFactory && get(w,unit)->cell==TileCoord{14,8} && get(w,factory)->factoryPhase==FactoryExitPhase::Idle,"aircraft roof exit/deferred rally failed");
  }
 }
 {
  // A grounded helicopter uses AG targeting; after takeoff only AA can hit it.
  World w(d);const auto heli=w.spawn("SHAD",{5,4}),gi=w.spawn("E1",{3,4},1),flak=w.spawn("FLAKT",{5,6},1);
  require(heli && gi && flak,"air defense scenario");
  w.enqueue({1,1,1,CommandKind::AttackEntity,gi,heli});frames(w,25);const auto damaged=get(w,heli);require(damaged && damaged->hp<damaged->maxHp,"landed helicopter rejected AG damage");
  w.enqueue({w.tick()+1,1,2,CommandKind::Stop,gi});cmd(w,CommandKind::Move,heli,0,{8,4});frames(w,55);
  const auto flying=get(w,heli);require(flying && flying->airborne,"AA scenario takeoff");
  cmd(w,CommandKind::AttackGround,heli,0,{8,4});
  w.enqueue({w.tick()+1,1,3,CommandKind::AttackEntity,flak,heli});bool falling=false;
  for(int i=0;i<900 && (get(w,heli) || !falling);++i){w.step();falling|=!w.aircraftWrecks().empty();}
  require(!get(w,heli) && falling,"AA did not kill aircraft and create falling voxel wreck");
 }
 {
  World w(d);const auto airport=w.spawn("GAAIRC",{4,4}),second=w.spawn("GAAIRC",{20,4});w.spawn("GAPOWR",{});
  const auto id=w.spawn("ORCA",{4,4});require(id,"orphan-airport scenario");cmd(w,CommandKind::Move,id,0,{12,10});frames(w,150);
  cmd(w,CommandKind::SellBuilding,airport);frames(w,70);require(get(w,id) && get(w,id)->aircraft->airport==second,"airborne plane failed to reserve another airport");
  cmd(w,CommandKind::Stop,id);frames(w,1400);require(get(w,id) && get(w,id)->aircraft->mission==AircraftMission::Landed,"reassigned plane failed to land");
 }
 {
  World a(d),b(d);for(auto* w:{&a,&b}){const auto z=w->spawn("ZEP",{4,4});w->enqueue({1,0,1,CommandKind::Move,z,0,{14,7}});}
  frames(a,500);frames(b,500);require(a.stateHash()==b.stateHash(),"aircraft simulation not deterministic");
 }
 std::cout<<"Aircraft regressions passed: native definitions, four pads, salvo/ammo/RTB/reload, air transport, water landing search, vertical bombs, airport transfer/loss, deterministic flight\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
