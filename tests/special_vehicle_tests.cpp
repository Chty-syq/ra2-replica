#include "simulation/world.h"
#include "test_support.h"
#include <algorithm>
#include <iostream>
#include <set>
using namespace ra2;
namespace {
EntitySnapshot find(const World& w,EntityId id){for(const auto& e:w.snapshot())if(e.id==id)return e;throw std::runtime_error("Test entity disappeared");}
void run(World& w,int n){while(n-->0)w.step();}
DefinitionRegistry fixture(const DefinitionRegistry& base,std::string extra={},std::string settings={}) {
  auto r=base.rules(),p=base.replica();
  r.overlay(IniDocument::parse("[SREF]\nCanPassiveAquire=no\n[MGTK]\nCanPassiveAquire=no\nStrength=10000\n[HTNK]\nCanPassiveAquire=no\nStrength=10000\n[MTNK]\nCanPassiveAquire=no\nStrength=10000\n[E1]\nCanPassiveAquire=no\n[E2]\nCanPassiveAquire=no\nStrength=10000\n[TNKD]\nCanPassiveAquire=no\n"+extra));
  p.overlay(IniDocument::parse("[Simulation]\nProductionFramesOverride=4\n"+settings));
  return DefinitionRegistry::compile(r,base.art(),p);
}
void definitions(const DefinitionRegistry& d) {
  require(d.unit("SREF").unsupported.empty() && d.unit("MGTK").unsupported.empty(),"Special vehicles remain disabled");
  require(d.unit("SREF").primary=="COMET" && d.unit("SREF").chargeTurrets==4 && d.weapon("COMET").laser && d.weapon("COMET").laserDuration==15,"Prism Weapon1/charge/laser data lost");
  require(d.projectile("LARGECOMETP").shrapnelWeapon=="COMETFRAGMENT" && d.projectile("LARGECOMETP").shrapnelCount==5 && d.weapon("COMETFRAGMENT").supported,"Prism fragments not loaded");
  require(d.unit("MGTK").disguiseWhenStill && d.unit("MGTK").disguises.size()==4 && d.weapon("MIRAGEGUN").disguiseBlinkFrames==15,"Original mirage disguise settings lost");
  require(!d.unit("MGTK").turret && d.unit("MGTK").visual.body=="rtnk" && d.unit("MGTK").visual.turret.empty() && d.unit("MGTK").visual.barrel.empty(),
          "Mirage must render the complete RTNK body without the unused turret/barrel");
  auto stale=d.replica();stale.overlay(IniDocument::parse("[Visual.MGTK]\nTurret=rtnktur\nBarrel=rtnkbarl\n"));
  const auto guarded=DefinitionRegistry::compile(d.rules(),d.art(),stale);
  require(guarded.unit("MGTK").visual.turret.empty() && guarded.unit("MGTK").visual.barrel.empty() && !guarded.unit("SREF").visual.turret.empty(),
          "Stale visual settings overrode Turret=no or removed a real turret");
  require(d.warhead("ULTRAAP").animations==std::vector<std::string>{"S_CLSN30"} && d.effectAnimation("S_CLSN30").frameCount==18,"Tank destroyer impact asset is missing");
  require(!d.weaponForTarget(d.unit("SREF"),true) && !d.weaponForTarget(d.unit("MGTK"),true),"Ground-only special tank can attack air");
}
void sabot(const DefinitionRegistry& base) {
  const auto d=fixture(base);World w(d);const auto gun=w.spawn("TNKD",{}),target=w.spawn("HTNK",{3,0},1);
  w.enqueue({1,0,1,CommandKind::AttackEntity,gun,target});bool explosion=false;
  for(int i=0;i<90;++i){w.step();for(const auto& e:w.effects())if(e.animation=="S_CLSN30") {
    explosion=true;require(e.explosion && std::abs(e.position.x-3000)<100 && std::abs(e.position.y)<100,"Sabot explosion is not at shell impact");
  }}
  require(explosion && find(w,target).hp<10000,"Sabot impact/damage never reached presentation");
}
void prism(const DefinitionRegistry& base) {
  const auto d=fixture(base);World w(d);const auto prism=w.spawn("SREF",{}),main=w.spawn("HTNK",{4,0},1);
  const auto near=w.spawn("HTNK",{5,0},1),friendId=w.spawn("HTNK",{4,1}),far=w.spawn("HTNK",{8,0},1),air=w.spawn("JUMPJET",{4,0},1);
  w.enqueue({1,0,1,CommandKind::AttackEntity,prism,main});run(w,1);
  require(find(w,prism).chargeTurret==3,"Charge turret did not start in discharged stage");
  require(w.beams().size()==1 && w.beams().front().started==w.tick() && w.beams().front().weapon=="COMET" && w.beams().front().startHeight==184,
    "Prism beam must appear on its firing frame at Weapon1FLH");
  require(find(w,main).hp==10000,"Firing visual advanced the damage projectile");
  w.step();require(w.beams().size()==6,"Fragments must appear on their creation frame");
  require(find(w,main).hp==9950 && find(w,near).hp==10000,"Prism main damage or fragment timing is wrong");
  std::size_t fragments=0;for(const auto& s:w.shots())fragments+=s.weaponId=="COMETFRAGMENT";
  require(fragments==5,"Prism did not emit original five fragments");w.step();
  require(w.beams().size()==6 && find(w,near).hp==9985 && find(w,friendId).hp==10000 && find(w,far).hp==10000 && find(w,air).hp==125,"Fragment range/armor/domain/owner filtering is wrong");
  w.enqueue({w.tick()+1,0,2,CommandKind::Stop,prism});std::set<int> stages{3};
  for(int i=0;i<105;++i){w.step();stages.insert(find(w,prism).chargeTurret);}
  require(stages==std::set<int>({0,1,2,3}) && w.beams().empty(),"Charge turret skipped a stage or beam never expired");
  World building(d);const auto a=building.spawn("SREF",{}),b=building.spawn("GAPOWR",{4,0},1);building.spawn("HTNK",{6,0},1);
  building.enqueue({1,0,1,CommandKind::AttackEntity,a,b});run(building,4);
  require(building.beams().size()==1,"Building hit incorrectly triggered vanilla shrapnel");
  World blocked(d);TerrainSurface cliff;cliff.cliff=true;cliff.heightLeptons=256;blocked.setTerrain({2,0},cliff);
  const auto gun=blocked.spawn("SREF",{}),victim=blocked.spawn("HTNK",{4,0},1);blocked.enqueue({1,0,1,CommandKind::AttackEntity,gun,victim});run(blocked,4);
  require(find(blocked,victim).hp==10000 && blocked.beams().size()==1 && blocked.beams().front().end.x<3000,"Laser ignored cliff collision");
  World cadence(d);const auto c=cadence.spawn("SREF",{}),t=cadence.spawn("HTNK",{4,0},1);cadence.enqueue({1,0,1,CommandKind::AttackEntity,c,t});
  std::vector<Tick> times;for(int i=0;i<230;++i){cadence.step();for(const auto& s:cadence.shots())if(s.weaponId=="COMET" && s.started==cadence.tick())times.push_back(s.started);}
  require(times.size()==3 && times[1]-times[0]==100 && times[2]-times[1]==100,"Prism ROF changed");
}
void mirage(const DefinitionRegistry& base) {
  const auto d=fixture(base);World w(d);const auto mirage=w.spawn("MGTK",{});
  require(find(w,mirage).disguised && !w.recognizes(mirage,1) && w.recognizes(mirage,0) && w.occupant({})==mirage,"Stationary disguise changed occupancy/recognition");
  const auto tree=find(w,mirage).disguise;w.enqueue({1,0,1,CommandKind::Move,mirage,0,{3,0}});w.step();require(!find(w,mirage).disguised,"Moving mirage stayed disguised");run(w,200);
  require(find(w,mirage).cell==TileCoord{3,0} && find(w,mirage).disguised && find(w,mirage).disguise==tree,"Arrival lost or changed mirage disguise");
  const auto target=w.spawn("HTNK",{6,0},1);w.enqueue({w.tick()+1,0,2,CommandKind::AttackEntity,mirage,target});
  bool fired=false;Tick firedAt=0;
  for(int i=0;i<80 && !fired;++i){w.step();for(const auto& s:w.shots())if(s.sourceType=="MGTK"){fired=true;firedAt=s.started;}}
  require(fired && find(w,mirage).disguised && find(w,mirage).disguiseFireUntil==firedAt+15 &&
          find(w,mirage).disguiseExposedUntil==0 && !w.recognizes(mirage,1),"Mirage fire visual blink became a logical decloak");
  w.enqueue({w.tick()+1,0,3,CommandKind::Stop,mirage});run(w,15);
  require(w.tick()>=find(w,mirage).disguiseFireUntil && find(w,mirage).disguised && find(w,target).hp==9900,"Mirage heat gun/blink expiry failed");
  const auto hidden=fixture(base,"[HTNK]\nCanPassiveAquire=yes\n");World acquisition(hidden);
  const auto m=acquisition.spawn("MGTK",{3,0}),enemy=acquisition.spawn("HTNK",{},1);run(acquisition,100);
  require(find(acquisition,m).hp==10000,"Enemy automatically acquired a hidden mirage");
  acquisition.enqueue({acquisition.tick()+1,0,1,CommandKind::Move,m,0,{3,2}});run(acquisition,150);
  require(find(acquisition,m).hp<10000,"Enemy ignored a moving/revealed mirage");
  const auto hp=find(acquisition,m).hp;run(acquisition,150);
  require(find(acquisition,m).disguised && find(acquisition,m).hp<hp && find(acquisition,enemy).hp==10000,"Redisguise dropped an already acquired enemy target");
  const auto detector=fixture(base,"[E1]\nDetectDisguise=yes\nDetectDisguiseRange=6\n");World detection(detector);
  const auto disguised=detection.spawn("MGTK",{3,0});detection.spawn("E1",{8,0},1);run(detection,2);
  require(detection.recognizes(disguised,1),"Disguise detector did not reveal mirage");
  World infantry(d);const auto tank=infantry.spawn("MGTK",{3,0}),soldier=infantry.spawn("E2",{3,1},1);run(infantry,2);
  require(infantry.recognizes(tank,1) && find(infantry,tank).disguiseFireUntil==0 &&
          find(infantry,tank).disguiseExposedUntil==infantry.tick()+20,"Nearby enemy infantry did not reveal mirage independently of firing");
  infantry.enqueue({infantry.tick()+1,1,1,CommandKind::Move,soldier,0,{9,1}});run(infantry,350);
  require(!infantry.recognizes(tank,1),"Infantry detection never expired");
}
void mirageTimers(const DefinitionRegistry& base) {
  const auto rapid=fixture(base,"[MirageGun]\nROF=4\n");World fire(rapid);
  const auto gun=fire.spawn("MGTK",{}),target=fire.spawn("HTNK",{4,0},1);
  fire.enqueue({1,0,1,CommandKind::AttackEntity,gun,target});
  Tick latest=0;int shots=0;
  for(int i=0;i<25;++i) {
    fire.step();for(const auto& shot:fire.shots())if(shot.sourceType=="MGTK" && shot.started==fire.tick()){latest=shot.started;++shots;}
    if(latest)require(find(fire,gun).disguiseFireUntil==latest+15 && find(fire,gun).disguiseExposedUntil==0 && !fire.recognizes(gun,1),
      "Repeated shots lost the visual deadline or changed logical recognition");
  }
  require(shots>=5,"Rapid-fire disguise test never exercised overlapping reveals");
  fire.enqueue({fire.tick()+1,0,2,CommandKind::Stop,gun});
  while(fire.tick()<latest+14)fire.step();
  require(fire.tick()<find(fire,gun).disguiseFireUntil,"Last firing frame expired early");
  fire.step();require(fire.tick()==find(fire,gun).disguiseFireUntil && find(fire,gun).disguised && !fire.recognizes(gun,1),"Visual fire reveal expiry changed disguise logic");
  const auto zero=fixture(base,"[MirageGun]\nDisguiseFakeBlinkTime=0\n");World silent(zero);
  const auto m=silent.spawn("MGTK",{}),t=silent.spawn("HTNK",{4,0},1);silent.enqueue({1,0,1,CommandKind::AttackEntity,m,t});
  bool shot=false;for(int i=0;i<30;++i) {
    silent.step();shot|=!silent.shots().empty();
    require(find(silent,m).disguiseFireUntil==0 && !silent.recognizes(m,1),"Disabled fake blink created a reveal timer");
  }
  require(shot,"Zero-blink weapon did not fire");
}
void productionAndReplay(const DefinitionRegistry& base) {
  const auto d=fixture(base,{},"[Simulation]\nSandbox=no\nStartingCredits=4000\nPlayerHouse=British\n");World w(d);const auto factory=w.spawn("GAWEAP",{});
  w.enqueue({1,0,1,CommandKind::Produce,factory,0,{},"SREF"});run(w,8);require(w.credits()==4000,"Prism skipped tech lab prerequisite");
  w.spawn("GATECH",{10,0});w.enqueue({w.tick()+1,0,2,CommandKind::Produce,factory,0,{},"SREF"});w.enqueue({w.tick()+1,0,3,CommandKind::Produce,factory,0,{},"MGTK"});
  bool cleared=false;for(int i=0;i<500;++i){w.step();if(!cleared)for(const auto& e:w.snapshot())if(e.type=="SREF" && !e.exitFactory) {
    w.enqueue({w.tick()+1,0,4,CommandKind::Move,e.id,0,{7,4}});cleared=true;
  }}
  int count=0;for(const auto& e:w.snapshot())count+=e.type=="SREF" || e.type=="MGTK";
  require(count==2 && w.credits()==1800,"Special vehicle production/egress/cost failed");
  const auto replay=fixture(base);std::uint64_t expected=0;
  for(int hz:{30,60,144}) {
    World world(replay);world.setGameSpeed({GameSpeedMode::Multiplayer,6});const auto a=world.spawn("SREF",{}),b=world.spawn("HTNK",{4,0},1),m=world.spawn("MGTK",{0,2});world.spawn("HTNK",{5,1},1);
    world.enqueue({1,0,1,CommandKind::AttackEntity,a,b});world.enqueue({1,0,2,CommandKind::Move,m,0,{3,2}});world.enqueue({150,0,3,CommandKind::AttackEntity,m,b});
    for(int f=1;f<=hz*6;++f)world.advance(std::uint64_t(f)*1000000/hz-std::uint64_t(f-1)*1000000/hz);
    require(world.tick()==360,"Special tank replay frame count changed");if(expected)require(world.stateHash()==expected,"Render rate changed beams/fragments/disguise outcome");else expected=world.stateHash();
  }
}
}
int main(int argc,char** argv) {
  try{require(argc==2,"Expected settings path");const auto d=DefinitionRegistry::load(argv[1]);definitions(d);sabot(d);prism(d);mirage(d);mirageTimers(d);productionAndReplay(d);std::cout<<"Special vehicle regressions passed\n";}
  catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
