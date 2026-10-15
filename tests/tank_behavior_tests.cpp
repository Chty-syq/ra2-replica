#include "simulation/world.h"
#include "simulation/kinematics.h"
#include "presentation/world_view.h"
#include "rhino_render_state.h"
#include "test_support.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>

using namespace ra2;
namespace {
EntitySnapshot unit(const World& w,EntityId id) {
  for (const auto& v:w.snapshot()) if (v.id==id) return v;
  throw std::runtime_error("Test unit disappeared");
}
void step(World& w,int count) { while (count-->0) w.step(); }
DefinitionRegistry changed(const DefinitionRegistry& defs,const std::string& rules) {
  auto r=defs.rules(); r.overlay(IniDocument::parse(rules));
  return DefinitionRegistry::compile(r,defs.art(),defs.replica());
}
void uniformMovement(const DefinitionRegistry& defs) {
  for (const auto* type:{"HTNK","MTNK"}) {
    World w(defs); const auto id=w.spawn(type,{});
    w.enqueue({1,0,1,CommandKind::Move,id,0,{10,0}});
    std::int64_t lo=100000,hi=0;
    for (int i=0;i<200;++i) {
      const auto before=unit(w,id).position; w.step(); const auto after=unit(w,id);
      if (after.cell==TileCoord{10,0}) break;
      const auto dx=after.position.x-before.x; lo=std::min(lo,dx); hi=std::max(hi,dx);
    }
    require(hi-lo<=1,"Straight travel pulses at cell boundaries");
    require(w.tick()==(std::string_view(type)=="HTNK"?171:151),"Base Speed calibration changed");
  }
  World w(defs); const auto id=w.spawn("HTNK",{});
  w.enqueue({1,0,1,CommandKind::Move,id,0,{10,5}});
  int turningInMotion=0,stalledAfterStarting=0; bool started=false;
  for (int i=0;i<300 && unit(w,id).cell!=TileCoord{10,5};++i) {
    const auto before=unit(w,id); w.step(); const auto after=unit(w,id);
    if (before.position!=after.position) started=true;
    if (before.heading!=after.heading) {
      if (before.position!=after.position) ++turningInMotion;
      else if (started) ++stalledAfterStarting;
    }
  }
  require(turningInMotion>4 && stalledAfterStarting==0,"Cornering still stops at a waypoint");
  require(unit(w,id).cell==TileCoord{10,5},"Curved path failed to arrive");
  w.enqueue({w.tick(),0,2,CommandKind::Move,id,0,{-3,-3}}); step(w,700);
  require(unit(w,id).cell==TileCoord{-3,-3},"Reversal/negative-coordinate movement failed");
}
void stableCornering(const DefinitionRegistry& defs) {
  const std::array<motion::Vector,8> compass{{{1024000,0,0},{1024000,1024000,0},{0,1024000,0},
    {-1024000,1024000,0},{-1024000,0,0},{-1024000,-1024000,0},{0,-1024000,0},{1024000,-1024000,0}}};
  for(int facing=0;facing<8;++facing) for(const int turn:{-2,-1,1,2}) {
    motion::DriveSegment track;
    const auto incoming=compass[facing],outgoing=compass[(facing+turn+8)%8];
    track.buildCorner({-incoming.x,-incoming.y,0},{},outgoing);
    const int start=facing*8192,sign=turn>0?1:-1,total=std::abs(turn)*8192;
    int previous=0;
    for(int sample=0;sample<=256;++sample) {
      const int angle=sign*motion::angleDelta(start,track.heading(track.length()*sample/256));
      require(angle>=0 && angle<=total,"Corner heading counter-steers or overshoots its outgoing direction");
      require(angle+4>=previous,"Single corner contains a reverse steering correction"); previous=angle;
    }
  }
  for(const auto* type:{"HTNK","MTNK"}) {
    World w(defs); const auto id=w.spawn(type,{});
    w.enqueue({1,0,1,CommandKind::Move,id,0,{10,1}});
    int previous=0;
    for(int i=0;i<240 && unit(w,id).cell!=TileCoord{10,1};++i) {
      const auto before=unit(w,id); w.step(); const auto after=unit(w,id);
      const int angle=motion::angleDelta(0,after.heading);
      require(angle>=0 && angle<=8192 && angle+4>=previous,"Open-ground 45-degree turn wiggles");
      require(angle-previous<=1280,"Corner exceeds chassis turn rate"); previous=angle;
      const auto dx=after.position.x-before.position.x,dy=after.position.y-before.position.y;
      require(dx*dx+dy*dy<=70*70,"Corner completion snapped the tank to a cell centre");
    }
    require(unit(w,id).position==Position{10000,1000},"Rounded path did not finish at the requested centre");
  }
  World redirected(defs); const auto id=redirected.spawn("HTNK",{});
  redirected.enqueue({1,0,1,CommandKind::Move,id,0,{10,1}});
  for(int i=0;i<180;++i) { redirected.step(); const auto s=unit(redirected,id); if(s.heading>1500 && s.heading<7000) break; }
  redirected.enqueue({redirected.tick()+1,0,2,CommandKind::Stop,id}); step(redirected,30);
  redirected.enqueue({redirected.tick()+1,0,3,CommandKind::Move,id,0,{-2,-2}});
  for(int i=0;i<400;++i) {
    const auto before=unit(redirected,id); redirected.step(); const auto after=unit(redirected,id);
    const auto dx=after.position.x-before.position.x,dy=after.position.y-before.position.y;
    require(dx*dx+dy*dy<=62*62,"Reissuing a move after a rounded corner teleported the tank");
  }
  require(unit(redirected,id).position==Position{-2000,-2000},"Interrupted corner failed to finish redirected path");
  World tight(defs);
  for(const auto blocked:{TileCoord{-1,0},TileCoord{0,-1},TileCoord{0,1},TileCoord{2,0},TileCoord{2,1}})
    tight.setBuildable(blocked,false);
  const auto cornerTank=tight.spawn("HTNK",{});
  tight.enqueue({1,0,1,CommandKind::Move,cornerTank,0,{1,3}});
  for(int i=0;i<140 && unit(tight,cornerTank).position!=Position{1000,3000};++i) {
    const auto before=unit(tight,cornerTank); tight.step(); const auto after=unit(tight,cornerTank);
    require(after.heading>=before.heading && after.heading<=16384,"Right-angle corner reverses steering");
    if(before.position!=after.position)
      require(after.heading-before.heading<=1280,"Tight moving corner exceeds ROT");
  }
  require(unit(tight,cornerTank).position==Position{1000,3000},"Right-angle obstacle route failed to arrive");
}
void aiming(const DefinitionRegistry& defs) {
  const double exact=std::atan2(1.0,3.0)*65536/(2*std::acos(-1.0));
  require(std::abs(motion::direction(3,1)-exact)<5,"Logical aiming is still quantized to 32 directions");
  World w(defs); const auto id=w.spawn("HTNK",{});
  w.enqueue({1,0,1,CommandKind::AttackGround,id,0,{0,3}}); step(w,13);
  require(!w.shots().empty() && unit(w,id).turretHeading==16384,"ROT=5 baseline changed");
  w.enqueue({w.tick(),0,2,CommandKind::Move,id,0,{3,0}}); w.step();
  require(unit(w,id).turretHeading==16384,"Move snapped turret back onto chassis");
  int previous=unit(w,id).turretHeading;
  for (int i=0;i<15;++i) { w.step(); const int current=unit(w,id).turretHeading;
    require(std::abs(motion::angleDelta(previous,current))<1600,"Turret realignment exceeds ROT allowance"); previous=current; }
  require(previous==0,"Turret did not finish realigning");
  const auto harmless=changed(defs,"[MTNK]\nPrimary=none");
  World tracking(harmless); const auto tracker=tracking.spawn("HTNK",{}),target=tracking.spawn("MTNK",{0,3},1);
  tracking.enqueue({1,1,1,CommandKind::Move,target,0,{3,3}});
  tracking.enqueue({1,0,1,CommandKind::AttackEntity,tracker,target,{0,3}});
  step(tracking,8);
  require(unit(tracking,tracker).turretHeading>6000,"Continuously changing target bearing froze turret rotation");
}
void opportunityFire(const DefinitionRegistry& defs) {
  World w(defs); const auto shooter=w.spawn("HTNK",{}); const auto target=w.spawn("AMCV",{3,0},1);
  w.enqueue({1,0,1,CommandKind::Move,shooter,0,{0,4}});
  std::set<std::uint64_t> fired;
  for (int i=0;i<90;++i) { w.step(); for (const auto& s:w.shots()) fired.insert(s.id); }
  require(!fired.empty() && unit(w,target).hp<unit(w,target).maxHp,"Moving tank never attacked nearby enemy");
  require(unit(w,shooter).cell==TileCoord{0,4},"Opportunity fire cancelled movement order");
  const auto noOpportunity=changed(defs,"[HTNK]\nOpportunityFire=no");
  World stopped(noOpportunity); const auto tank=stopped.spawn("HTNK",{}); stopped.spawn("AMCV",{3,0},1);
  stopped.enqueue({1,0,1,CommandKind::Move,tank,0,{0,4}});
  for (int i=0;i<50;++i) { stopped.step(); require(stopped.shots().empty(),"OpportunityFire=no was ignored"); }
}
void ballistics(const DefinitionRegistry& defs) {
  require(!motion::ballisticLaunch({100000000000,0,0},1280,6),"Out-of-domain ballistic distance accepted");
  const auto harmless=changed(defs,"[MTNK]\nPrimary=none\nStrength=5000");
  for (const int range:{1,3,5}) {
    World w(harmless); const auto shooter=w.spawn("HTNK",{}); const auto target=w.spawn("MTNK",{range,0},1);
    w.enqueue({1,0,1,CommandKind::AttackEntity,shooter,target,{range,0}}); w.step();
    require(w.shots().size()==1,"Cannon failed to find low arc");
    const auto shot=w.shots()[0];
    require(shot.start.x==585 && shot.heightLeptons==100,"Authoritative shot did not start at original FLH");
    require(shot.impact-shot.started<static_cast<Tick>(range==1?8:range==3?20:32),"Arcing projectile still uses raw INI Speed=40");
    step(w,40); require(unit(w,target).hp==4910,"Stationary cannon hit/armor damage drifted");
  }
  World w(harmless); const auto shooter=w.spawn("HTNK",{}); const auto target=w.spawn("MTNK",{4,0},1);
  w.enqueue({1,1,1,CommandKind::Move,target,0,{10,0}});
  w.enqueue({1,0,1,CommandKind::AttackEntity,shooter,target,{4,0}}); w.step();
  require(w.shots().size()==1 && w.shots()[0].target.x>unit(w,target).position.x,"Moving target has no launch-time lead");
  const auto aim=w.shots()[0].target;
  step(w,4); require(w.shots()[0].target==aim,"Ordinary cannon incorrectly homes after launch");
  step(w,25); require(unit(w,target).hp<5000,"Constant-speed target was missed despite unobstructed lead");
  const auto slowGravity=changed(harmless,"[General]\nGravity=1");
  World low(slowGravity); const auto tank=low.spawn("HTNK",{});
  low.enqueue({1,0,1,CommandKind::AttackGround,tank,0,{5,0}}); low.step();
  require(low.shots()[0].impact-low.shots()[0].started>20,"Configured gravity is ignored");
}
void terrain(const DefinitionRegistry& defs) {
  for (const bool wall:{false,true}) {
    World w(defs); TerrainSurface surface;
    if (wall) surface.wallHeightLeptons=256; else { surface.heightLeptons=384; surface.cliff=true; }
    w.setTerrain({2,0},surface);
    const auto id=w.spawn("HTNK",{}); w.enqueue({1,0,1,CommandKind::AttackGround,id,0,{5,0}});
    bool blocked=false;
    for(int i=0;i<35;++i) { w.step(); for (const auto& e:w.effects()) if (e.explosion && e.position.x<3000) blocked=true; }
    require(blocked,wall?"Cannon tunneled through wall":"Cannon ignored raised cliff");
    w.enqueue({w.tick(),0,2,CommandKind::Move,id,0,{4,0}});
    for(int i=0;i<180;++i) { w.step(); require(unit(w,id).cell!=TileCoord{2,0},"Vehicle entered blocked surface"); }
    require(unit(w,id).cell==TileCoord{4,0},"Vehicle could not route around obstacle");
  }
  World ice(defs),clear(defs);
  for(int x=0;x<=10;++x) for(int y=-2;y<=2;++y) { TerrainSurface t; t.land="ICE"; ice.setTerrain({x,y},t); }
  const auto icy=ice.spawn("HTNK",{}),dry=clear.spawn("HTNK",{});
  ice.enqueue({1,0,1,CommandKind::Move,icy,0,{10,0}}); clear.enqueue({1,0,1,CommandKind::Move,dry,0,{10,0}});
  step(ice,100); step(clear,100);
  require(std::abs(unit(ice,icy).position.x*100-unit(clear,dry).position.x*80)<=100,"Track terrain speed multiplier ignored");
  World ramp(defs); TerrainSurface t; t.heightLeptons=128; t.slopeX=128;
  ramp.setTerrain({0,0},t); const auto tank=ramp.spawn("HTNK",{});
  const auto view=makeWorldView(ramp,defs,tank).vehicles.at(0);
  require(view.heightLeptons==128 && view.pitchDegrees>20,"Terrain height/IsTilter did not reach VXL presentation");
  ramp.enqueue({1,0,1,CommandKind::AttackGround,tank,0,{3,0}}); ramp.step();
  require(!ramp.shots().empty() && ramp.shots()[0].heightLeptons>270 && ramp.shots()[0].start.x<500,
          "Ramp muzzle failed to follow the tilted chassis");
  // VXL uses a flipped Y axis. Compare its complete FLH transform against the
  // authoritative integer transform, across chassis and turret orientations.
  for(const int heading:{0,8192,16384,32768}) for(const int facing:{0,8192,16384}) {
    const auto basis=motion::unitVector(heading);
    const double pitch=(128*basis.x+64*basis.y)/65536.0,roll=(64*basis.x-128*basis.y)/65536.0;
    ImGuiDebugPanelState debug{}; debug.rhinoTransform.worldRotateXDeg=0;
    debug.rhinoTransform.worldRotateYDeg=0; debug.rhinoTransform.worldRotateZDeg=0;
    auto state=buildRhinoTankRenderState(HouseColorSet{},debug,static_cast<float>(heading*2*std::acos(-1.0)/65536));
    applyRhinoSurfaceTilt(state,static_cast<float>(std::atan(pitch/256)*180/std::acos(-1.0)),
                         static_cast<float>(std::atan(roll/256)*180/std::acos(-1.0)));
    const auto local=motion::rotateFlh(facing-heading,{150,0,100});
    const double model[]{double(local.x)/motion::lepton,-double(local.y)/motion::lepton,double(local.z)/motion::lepton};
    double drawn[3]{};
    for(int col=0;col<3;++col) for(int row=0;row<3;++row) drawn[col]+=model[row]*state.worldTransform[row*4+col];
    const auto actual=motion::surfaceFlh(heading,facing,{150,0,100},128,64);
    require(std::abs(drawn[0]-double(actual.x)/motion::lepton)<1 &&
            std::abs(-drawn[1]-double(actual.y)/motion::lepton)<1 &&
            std::abs(drawn[2]-double(actual.z)/motion::lepton)<1,"Rendered and simulated tilted FLH disagree");
  }
}
void presentation(const DefinitionRegistry& defs) {
  World w(defs); w.setGameSpeed({GameSpeedMode::Singleplayer,6});
  const auto id=w.spawn("HTNK",{}); w.enqueue({1,0,1,CommandKind::AttackGround,id,0,{3,0}}); w.step();
  auto view=makeWorldView(w,defs,0);
  require(view.projectiles[0].simulated && std::abs(view.projectiles[0].logicalPosition.x-0.585f)<0.001f,"Renderer recomputed a different muzzle");
  require(view.effects.size()==1 && view.effects[0].animationFrame==0,"Muzzle did not start at original frame zero");
  step(w,3); view=makeWorldView(w,defs,0);
  require(view.effects[0].animationFrame==3,"Four-frame GUNFIRE was truncated or accelerated");
  w.step(); view=makeWorldView(w,defs,0);
  for(const auto& e:view.effects) require(e.animation!="GUNFIRE","GUNFIRE retained an expired frame");
  bool large=false;
  for(int i=0;i<30 && !large;++i) { w.step(); for(const auto& e:w.effects()) if(e.animation=="S_CLSN22") {
    large=true; require(e.duration==26,"Normalized explosion timing ignored original SHP length");
  } }
  require(large,"AP damage-based AnimList selection never reached S_CLSN22");
}
}
int main(int argc,char** argv) {
  try {
    require(argc==2,"Expected settings directory"); const auto defs=DefinitionRegistry::load(argv[1]);
    uniformMovement(defs); stableCornering(defs); aiming(defs); opportunityFire(defs); ballistics(defs); terrain(defs); presentation(defs);
    std::cout<<"Tank behavior regressions passed\n"; return 0;
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
