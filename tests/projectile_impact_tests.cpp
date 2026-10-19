#include "simulation/world.h"
#include "test_support.h"
#include <algorithm>
#include <array>
#include <iostream>
using namespace ra2;
namespace {
EntitySnapshot get(const World& w,EntityId id){for(const auto& e:w.snapshot())if(e.id==id)return e;throw std::runtime_error("Impact fixture lost entity");}
void run(World& w,int frames){while(frames-->0)w.step();}
DefinitionRegistry fixture(const DefinitionRegistry& base,std::string extra={},std::string profile={}) {
  auto r=base.rules(),a=base.art(),p=base.replica();
  for(const auto id:{"HTNK","MTNK","TNKD","E1","E2","JUMPJET"})r.overlay(IniDocument::parse(std::string("[")+id+"]\nCanPassiveAquire=no\nROT=0\n"));
  r.overlay(IniDocument::parse("[E2]\nStrength=10000\nFearless=yes\n[E1]\nStrength=10000\n[120mm]\nROF=10000\n[105mm]\nROF=10000\n[SABOT]\nROF=10000\n[M60]\nROF=10000\n"+extra));
  p.overlay(IniDocument::parse(profile));
  for(const auto& [id,anim]:base.effectAnimations())if(anim.frameCount>0)a.overlay(IniDocument::parse("["+id+"]\nEnd="+std::to_string(anim.frameCount-1)));
  return DefinitionRegistry::compile(r,a,p);
}
struct Impact {ShotSnapshot launch;Position point,targetAtLaunch;};
Impact fire(World& w,EntityId gun,TileCoord cell,EntityId target=0) {
  w.enqueue({w.tick()+1,0,1,target?CommandKind::AttackEntity:CommandKind::AttackGround,gun,target,cell});
  Impact result;bool fired=false;
  for(int i=0;i<150;++i) {
    w.step();if(!fired && !w.shots().empty()) {
      result.launch=w.shots().front();fired=true;
      if(target)result.targetAtLaunch=get(w,target).position;
    }
    if(fired && w.shots().empty()) {
      for(const auto& e:w.effects())if(e.explosion && e.started==w.tick()){result.point=e.position;return result;}
      result.point=result.launch.target;return result;
    }
  }
  throw std::runtime_error("Fixture cannon did not detonate");
}
void targetedSubcells(const DefinitionRegistry& base) {
  const auto d=fixture(base);
  for(const auto gunType:{"HTNK","MTNK","TNKD"})for(const auto cell:{TileCoord{3,0},TileCoord{3,3},TileCoord{0,3},TileCoord{-3,3},TileCoord{-3,0},TileCoord{-3,-3},TileCoord{0,-3},TileCoord{3,-3}})
    for(int selected=0;selected<3;++selected) {
      World w(d);const auto gun=w.spawn(gunType,{});std::array<EntityId,3> foot{};for(auto& id:foot)id=w.spawn("E2",cell,1);
      const auto coordinate=get(w,foot[selected]).position;const auto impact=fire(w,gun,cell,foot[selected]);
      require(impact.launch.target==coordinate,"Actor attack rounded the infantry subcell to a large cell");
      require(std::abs(impact.point.x-coordinate.x)<=32 && std::abs(impact.point.y-coordinate.y)<=32,"Stationary shell landed away from its fixed target");
      require(get(w,foot[selected]).hp<10000,"Cannon failed to damage selected subcell");
      // Native AP spread is narrower than the separation of the other bodies.
      for(int i=0;i<3;++i)if(i!=selected)require(get(w,foot[i]).hp==10000,"Precise shell hit every infantry in a cell");
    }
}
void groundSplash(const DefinitionRegistry& base) {
  const auto d=fixture(base);
  for(const auto type:{"HTNK","MTNK"})for(const auto cell:{TileCoord{3,0},TileCoord{-3,2},TileCoord{2,-3}}) {
    World w(d);const auto gun=w.spawn(type,{});std::array<EntityId,3> foot{};for(auto& id:foot)id=w.spawn("E2",cell,1);
    const auto neighbour=w.spawn("E2",{cell.x+1,cell.y},1);const auto impact=fire(w,gun,cell);
    require(impact.launch.target==Position{cell.x*1000LL,cell.y*1000LL} && impact.point==impact.launch.target,"Ground order was retargeted onto an infantry");
    for(const auto id:foot)require(get(w,id).hp<10000 && get(w,id).hp>9900,"Native .3-cell AP splash missed a same-cell body or ignored verses");
    require(get(w,neighbour).hp==10000,"Hit shape turned AP splash into whole-cell neighbour damage");
    if(std::string_view(type)=="HTNK")for(const auto id:foot)require(get(w,id).hp==9987,"AP body-edge falloff or native 25% infantry armor multiplier changed");
  }
  // Disabling the compatibility body reproduces the original point-only hole.
  const auto point=fixture(base,{},"[Simulation]\nInfantryHitRadiusLeptons=0\n");World legacy(point);const auto gun=legacy.spawn("HTNK",{}),victim=legacy.spawn("E2",{3,0},1);fire(legacy,gun,{3,0});require(get(legacy,victim).hp==10000,"Body radius calibration ignored");
  for(const auto spread:{"0",".1"}) {
    const auto narrow=fixture(base,std::string("[AP]\nCellSpread=")+spread+"\n");World w(narrow);const auto g=w.spawn("HTNK",{}),f=w.spawn("E2",{3,0},1);fire(w,g,{3,0});require(get(w,f).hp==10000,"Zero/narrow warhead gained artificial whole-cell splash");
  }
  World direct(d);const auto gi=direct.spawn("E1",{}),a=direct.spawn("E2",{2,0},1),b=direct.spawn("E2",{2,0},1);fire(direct,gi,{2,0},a);
  require(get(direct,a).hp<10000 && get(direct,b).hp==10000,"Zero-spread rifle damaged neighbouring subcell");
}
void falloffAndDomains(const DefinitionRegistry& base) {
  for(const auto spread:{".227",".228"}) {
    const auto d=fixture(base,std::string("[AP]\nCellSpread=")+spread+"\nPercentAtMax=1\n");World w(d);const auto gun=w.spawn("HTNK",{}),victim=w.spawn("E2",{3,0},1);fire(w,gun,{3,0});
    require((get(w,victim).hp<10000)==(std::string_view(spread)==".228"),"Infantry body-edge boundary is not respected");
  }
  const auto d=fixture(base,"[Cannon]\nAA=yes\n[JUMPJET]\nStrength=10000\n");World w(d);const auto gun=w.spawn("HTNK",{}),ground=w.spawn("E2",{3,0},1),air=w.spawn("JUMPJET",{3,0},1);
  w.enqueue({1,1,1,CommandKind::Move,air,0,{3,0}});run(w,35);require(get(w,air).heightLeptons==500,"Air fixture did not take off");fire(w,gun,{3,0});
  require(get(w,ground).hp<10000 && get(w,air).hp==10000,"Foot body radius flattened 3D splash distance");
  const auto friendlyDefs=fixture(base);World friendly(friendlyDefs);const auto g=friendly.spawn("HTNK",{}),ally=friendly.spawn("E2",{3,0});fire(friendly,g,{3,0});require(get(friendly,ally).hp<10000,"Ground splash lost friendly damage");
  const auto proneDefs=fixture(base,"[E2]\nFearless=no\n[120mm]\nROF=60\n");World prone(proneDefs);const auto p=prone.spawn("HTNK",{}),target=prone.spawn("E2",{3,0},1);
  prone.enqueue({1,0,1,CommandKind::AttackGround,p,0,{3,0}});run(prone,100);require(get(prone,target).hp==9981,"Native prone damage multiplier was lost after body-edge hit");
}
void dodgeAndObstacles(const DefinitionRegistry& base) {
  const auto d=fixture(base,"[AP]\nCellSpread=0\nPercentAtMax=1\n");World w(d);const auto gun=w.spawn("HTNK",{}),foot=w.spawn("E2",{4,0},1);
  w.enqueue({1,1,1,CommandKind::Move,foot,0,{10,0}});run(w,6);const auto launch=fire(w,gun,{4,0},foot);
  require(launch.launch.target.x>launch.targetAtLaunch.x && get(w,foot).hp<10000,"Cannon failed to lead and hit constant-speed infantry");
  // A post-launch direction change must evade the shell: no target-ID based hit.
  World evasion(d);const auto g=evasion.spawn("HTNK",{}),victim=evasion.spawn("E2",{4,0},1);evasion.enqueue({1,0,1,CommandKind::AttackEntity,g,victim});
  for(int i=0;i<10 && evasion.shots().empty();++i)evasion.step();require(!evasion.shots().empty(),"Dodge fixture did not fire");const auto aim=evasion.shots().front().target;
  evasion.enqueue({evasion.tick()+1,1,1,CommandKind::Move,victim,0,{4,5}});run(evasion,35);
  require(get(evasion,victim).hp==10000 && get(evasion,victim).position!=aim,"Non-homing cannon followed target after launch");
  World wall(d);TerrainSurface barrier;barrier.wallHeightLeptons=256;wall.setTerrain({2,0},barrier);const auto shooter=wall.spawn("HTNK",{}),behind=wall.spawn("E2",{4,0},1);const auto blocked=fire(wall,shooter,{4,0},behind);
  require(blocked.point.x<3000 && get(wall,behind).hp==10000,"Subcell correction snapped a blocked shell through a wall");
}
void validationAndReplay(const DefinitionRegistry& base) {
  for(const auto bad:{"-1","129"}) {bool rejected=false;try{(void)fixture(base,{},std::string("[Simulation]\nInfantryHitRadiusLeptons=")+bad);}catch(const std::runtime_error&){rejected=true;}require(rejected,"Invalid infantry hit radius accepted");}
  const auto d=fixture(base);World a(d),b(d);for(auto* w:{&a,&b}){const auto gun=w->spawn("HTNK",{});for(int i=0;i<3;++i)w->spawn("E2",{3,0},1);w->enqueue({1,0,1,CommandKind::AttackGround,gun,0,{3,0}});}
  for(int i=0;i<100;++i){a.step();b.step();require(a.stateHash()==b.stateHash(),"Body-edge splash is nondeterministic");}
}
}
int main(int argc,char** argv)try{const auto base=DefinitionRegistry::load(argc>1?argv[1]:"assets/settings");targetedSubcells(base);groundSplash(base);falloffAndDomains(base);dodgeAndObstacles(base);validationAndReplay(base);std::cout<<"Projectile impact regressions passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
