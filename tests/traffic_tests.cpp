#include "simulation/world.h"
#include "simulation/traffic.h"
#include "simulation/kinematics.h"
#include "test_support.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>
#include <set>
#include <tuple>

using namespace ra2;
namespace {
EntitySnapshot view(const World& w,EntityId id) {
  for(const auto& e:w.snapshot()) if(e.id==id) return e;
  throw std::runtime_error("Traffic participant disappeared");
}
void group(World& w,std::vector<EntityId> ids,TileCoord goal,std::uint64_t sequence) {
  Command command{w.tick()+1,0,sequence,CommandKind::GroupMove,0,0,goal}; command.members=std::move(ids);
  require(w.enqueue(std::move(command)),"Group command rejected");
}
struct Measurements {
  std::size_t waits=0,yields=0,maxSearch=0,maxAssignment=0;
  std::size_t flowBuilds=0,flowSteps=0,flowExpanded=0,forecastHolds=0;
  std::uint64_t longestStall=0; double maxMilliseconds=0;
  std::int64_t travel=0,direct=0;
  std::map<EntityId,std::uint64_t> stalled;
  std::map<EntityId,TileCoord> goals;
  bool step(World& w) {
    const auto before=w.snapshot(); const auto began=std::chrono::steady_clock::now(); w.step();
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
    maxMilliseconds=std::max(maxMilliseconds,elapsed);
    const auto stats=w.movementDiagnostics(); waits+=stats.waitingUnits; yields+=stats.yields;
    flowBuilds+=stats.flowBuilds; flowSteps+=stats.flowSteps; flowExpanded+=stats.flowExpanded; forecastHolds+=stats.forecastHolds;
    maxSearch=std::max(maxSearch,stats.pathExpanded); maxAssignment=std::max(maxAssignment,stats.assignmentExpanded);
    require(stats.pathExpanded<=8192 && stats.assignmentExpanded<=8192,"Traffic exceeded its per-frame search budget");
    const auto after=w.snapshot(); std::set<std::pair<int,int>> occupied;
    bool arrived=true;
    for(const auto& e:after) {
      if(!goals.contains(e.id) && !e.awaitingFormation) {
        goals[e.id]=e.destination;
        direct+=motion::length({std::int64_t(e.destination.x)*1000-e.previous.x,std::int64_t(e.destination.y)*1000-e.previous.y,0});
      }
      if(goals.contains(e.id)) require(e.destination==goals.at(e.id),"Traffic silently replaced a unit's assigned destination");
      require(occupied.emplace(e.cell.x,e.cell.y).second,"Two tanks own the same cell");
      const auto old=std::find_if(before.begin(),before.end(),[&](const auto& p) { return p.id==e.id; });
      if(old!=before.end()) {
        const auto dx=e.position.x-old->position.x,dy=e.position.y-old->position.y;
        require(dx*dx+dy*dy<=70*70,"Traffic recovery teleported a tank or exceeded its movement speed");
        travel+=motion::length({dx,dy,0});
      }
      if(e.cell!=e.destination && old!=before.end() && old->position==e.position) ++stalled[e.id]; else stalled[e.id]=0;
      longestStall=std::max(longestStall,stalled[e.id]);
      arrived&=!e.awaitingFormation && e.cell==e.destination && e.activity==Activity::Idle;
      for(const auto& peer:after) if(peer.id>e.id) {
        const auto dx=peer.position.x-e.position.x,dy=peer.position.y-e.position.y;
        require(dx*dx+dy*dy>=499*499,"Swept paths intersected while endpoints remained distinct");
      }
    }
    return arrived;
  }
  void finish(World& w,int budget,const char* name) {
    bool arrived=false;
    while(budget-->0) if(step(w)) { arrived=true; break; }
    std::cout<<name<<": frames="<<w.tick()<<" waits="<<waits<<" yields="<<yields
      <<" longest_stall="<<longestStall<<" search="<<maxSearch<<" assignment="<<maxAssignment
      <<" travel_ratio="<<(direct?double(travel)/direct:1)<<" max_ms="<<maxMilliseconds<<'\n';
    std::cout<<"  flows="<<flowBuilds<<" flow_steps="<<flowSteps<<" flow_expanded="<<flowExpanded<<" forecast_holds="<<forecastHolds<<'\n';
    if(!arrived) for(const auto& e:w.snapshot()) std::cerr<<e.id<<" at "<<e.cell.x<<','<<e.cell.y<<" -> "<<e.destination.x<<','<<e.destination.y<<'\n';
    require(arrived,std::string(name)+" did not finish within its frame budget");
  }
};
void algorithms() {
  // Check reverse integration against independent forward searches, including
  // destination-weighted terrain and a blocked diagonal corner.
  const std::array<TileCoord,2> targets{{{5,4},{5,5}}};
  const auto clear=[](TileCoord c) {
    return c.x>=0 && c.y>=0 && c.x<=6 && c.y<=6 && c!=TileCoord{3,2} && c!=TileCoord{3,3} && c!=TileCoord{3,4};
  };
  const auto edge=[](TileCoord a,TileCoord b) { return (a.x!=b.x && a.y!=b.y?14:10)*(b.x==2?4:1); };
  traffic::FlowField incremental({0,0},{6,6},targets),whole({0,0},{6,6},targets);
  require(incremental.distance({0,0})==traffic::unreachable,"Unsettled flow cell was exposed as a route");
  while(!incremental.finished()) require(incremental.expand(3,edge,clear)<=3,"Flow integration exceeded its slice budget");
  whole.expand(10000,edge,clear);
  require(whole.stateHash()==incremental.stateHash(),"Sliced flow integration changed the final field");
  for(int y=0;y<=6;++y) for(int x=0;x<=6;++x) if(clear({x,y})) {
    std::size_t budget=10000;
    const auto reference=traffic::travelCosts({x,y},targets,edge,clear,budget,10);
    require(whole.distance({x,y})==*std::min_element(reference.begin(),reference.end()),"Reverse flow disagreed with forward shortest paths");
  }
  traffic::FlowField corner({0,0},{1,1},std::array<TileCoord,1>{{{1,1}}});
  corner.expand(100,edge,[](TileCoord c) { return c==TileCoord{} || c==TileCoord{1,1}; });
  require(corner.finished() && corner.distance({})==traffic::unreachable,"Flow cut through a blocked diagonal corner");
  const auto optimal=traffic::assign({{20,1,9},{2,20,9},{9,9,1}});
  require(optimal==std::vector<int>({1,0,2}),"Minimum-cost assignment chose crossing/high-cost slots");
  require(traffic::assign({{traffic::unreachable,50},{1,50}})==std::vector<int>({1,0}),"Unreachable pair displaced a reachable assignment");
  const auto inf=traffic::unreachable;
  require(traffic::assignCompact({{1,1,inf,4},{1,inf,inf,5},{inf,inf,1,6}},std::array<traffic::Cost,4>{0,1,2,3})==
    std::vector<int>({1,0,2}),"Compact matching failed to rematch an earlier tank for a nearer slot");
  require(traffic::assignCompact({{inf,5,1,1},{inf,1,5,1}},std::array<traffic::Cost,4>{0,1,1,2})==
    std::vector<int>({2,1}),"Compact matching kept an unreachable centre or preferred a farther slot");
  // Compare the three priorities against exhaustive assignments on small
  // disconnected graphs, including dummy and cancelled/unreachable rows.
  std::uint32_t seed=12345;
  const std::array<traffic::Cost,5> radii{0,1,1,2,4};
  for(int n=1;n<=4;++n) for(int trial=0;trial<24;++trial) {
    std::vector<std::vector<traffic::Cost>> costs(n,std::vector<traffic::Cost>(5+n,inf/2));
    for(auto& row:costs) for(int j=0;j<5;++j) {
      seed=1664525*seed+1013904223;
      row[j]=seed%4?seed%100:inf;
    }
    using Score=std::tuple<int,traffic::Cost,traffic::Cost>;
    Score best{n+1,0,0};
    const auto enumerate=[&](const auto& self,int actor,unsigned used,Score score)->void {
      if(actor==n) { best=std::min(best,score); return; }
      const auto [missing,radius,travel]=score;
      self(self,actor+1,used,{missing+1,radius,travel+inf/2});
      for(int j=0;j<5;++j) if(!(used&(1u<<j)) && costs[actor][j]<inf/2)
        self(self,actor+1,used|(1u<<j),{missing,radius+radii[j],travel+costs[actor][j]});
    };
    enumerate(enumerate,0,0,{0,0,0});
    const auto assignment=traffic::assignCompact(costs,radii);
    int missing=0; traffic::Cost radius=0,travel=0; std::set<int> unique;
    for(int i=0;i<n;++i) {
      const int j=assignment[i]; require(j>=0 && unique.insert(j).second,"Compact matching reused a column");
      if(j>=5) { ++missing; travel+=costs[i][j]; }
      else { require(costs[i][j]<inf/2,"Compact matching selected an unreachable pair"); radius+=radii[j]; travel+=costs[i][j]; }
    }
    require(Score{missing,radius,travel}==best,"Compact matching disagrees with the exhaustive optimum");
  }
}
void sharedFlow(const DefinitionRegistry& defs) {
  World w(defs); std::vector<EntityId> ids;
  for(int y=-2;y<=2;y+=2) for(int x=0;x<4;x+=2) ids.push_back(w.spawn("HTNK",{x,y}));
  for(int y=-8;y<=8;++y) w.setBuildable({18,y},false);
  group(w,ids,{40,0},1);
  Measurements m; m.finish(w,2200,"shared field around a long wall");
  require(m.flowBuilds==1 && m.flowSteps>=12,"Group did not reuse one flow field for its obstacle route");
  require(double(m.travel)/m.direct<1.4,"Flow wall detour was excessive");

  World changed(defs); ids.clear();
  for(int y=-2;y<=2;y+=2) ids.push_back(changed.spawn("HTNK",{0,y}));
  for(int y=-8;y<=8;++y) changed.setBuildable({18,y},false);
  group(changed,ids,{40,0},1); Measurements dynamic;
  for(int i=0;i<80;++i) dynamic.step(changed);
  for(int y=-8;y<=-4;++y) changed.setBuildable({12,y},false);
  dynamic.finish(changed,2200,"flow invalidation after terrain changes");
  require(dynamic.flowBuilds>=2,"Terrain change reused a stale flow field");

  World replayA(defs),replayB(defs);
  ids.clear();
  for(int y=-2;y<=2;y+=2) {
    ids.push_back(replayA.spawn("HTNK",{0,y})); replayB.spawn("HTNK",{0,y});
  }
  for(auto* world:{&replayA,&replayB}) {
    for(int y=-8;y<=8;++y) world->setBuildable({18,y},false);
    group(*world,ids,{40,0},1);
  }
  Position stopped{};
  for(int frame=0;frame<240;++frame) {
    if(frame==40) for(auto* world:{&replayA,&replayB}) world->enqueue({world->tick()+1,0,2,CommandKind::Stop,ids[0]});
    if(frame==70) for(auto* world:{&replayA,&replayB}) group(*world,{ids[2],ids[1]},{-8,16},3);
    replayA.step(); replayB.step();
    require(replayA.stateHash()==replayB.stateHash(),"Shared flow/forecast state was not replay deterministic");
    if(frame==65) stopped=view(replayA,ids[0]).position;
    if(frame>65) require(view(replayA,ids[0]).position==stopped,"A cached flow resumed a stopped group member");
  }
}
void clusteredTargets(const DefinitionRegistry& defs) {
  World spread(defs); const auto a=spread.spawn("HTNK",{}),b=spread.spawn("HTNK",{24,0});
  group(spread,{a,b},{12,20},1); Measurements wide; wide.finish(spread,1000,"widely separated pair gathers at the click");
  int radius=0;
  for(const auto id:{a,b}) { const auto c=view(spread,id).destination; radius+=(c.x-12)*(c.x-12)+(c.y-20)*(c.y-20); }
  require(radius==1,"Widely separated members left the centre empty or retained their starting separation");

  World blocked(defs); std::vector<EntityId> ids;
  for(int y=0;y<=3;y+=3) for(int x=-8;x<=-5;x+=3) ids.push_back(blocked.spawn("HTNK",{x,y}));
  blocked.setBuildable({8,8},false);
  const auto parked=blocked.spawn("HTNK",{9,8}); group(blocked,ids,{8,8},1);
  Measurements obstacles; obstacles.finish(blocked,1000,"compact slots around terrain and a parked vehicle");
  radius=0;
  for(const auto id:ids) { const auto c=view(blocked,id).destination; radius+=(c.x-8)*(c.x-8)+(c.y-8)*(c.y-8); }
  require(radius==5,"Blocked click did not expand to the nearest free destination cells");
  require(view(blocked,parked).position==Position{9000,8000},"Gathering displaced a tank outside the selected group");
}
void collisionAndObstacles(const DefinitionRegistry& defs) {
  for(const bool swap:{false,true}) {
    World w(defs); const auto a=w.spawn("HTNK",{}),b=w.spawn("HTNK",{1,0});
    w.enqueue({1,0,1,CommandKind::Move,a,0,swap?TileCoord{1,0}:TileCoord{1,1}});
    w.enqueue({1,0,2,CommandKind::Move,b,0,swap?TileCoord{0,0}:TileCoord{0,1}});
    Measurements m; m.finish(w,800,swap?"adjacent cell swap":"crossing diagonals");
  }
  World w(defs); std::vector<EntityId> ids;
  for(int y=0;y<3;++y) for(int x=0;x<3;++x) ids.push_back(w.spawn("HTNK",{x,y}));
  w.setBuildable({6,1},false); group(w,ids,{14,1},1);
  Measurements m; m.finish(w,1200,"formation around a static obstacle");
  require(double(m.travel)/m.direct<1.3,"A small obstacle caused excessive formation detours");

  World redirected(defs); ids.clear();
  for(int y=0;y<3;++y) for(int x=0;x<3;++x) ids.push_back(redirected.spawn("HTNK",{x,y}));
  group(redirected,ids,{14,2},1);
  for(int i=0;i<40;++i) redirected.step();
  group(redirected,ids,{-8,-3},2);
  Measurements changed; changed.finish(redirected,700,"moving formation receives a replacement order");
  require(changed.yields==0,"Replacing an open-ground order caused the formation to cross its own routes");

  auto rules=defs.rules();
  rules.overlay(IniDocument::parse("[HTNK]\nStrength=1\nPrimary=TrafficTestGun\nCanPassiveAquire=no\n[MTNK]\nSpeed=6\nPrimary=none\n"
    "[TrafficTestGun]\nDamage=100\nROF=100\nRange=100\nProjectile=Invisible\nWarhead=AP\nSpeed=100\n"));
  const auto combatDefs=DefinitionRegistry::compile(rules,defs.art(),defs.replica());
  World combat(combatDefs);
  const auto doomed=combat.spawn("HTNK",{}),survivor=combat.spawn("MTNK",{1,0}),attacker=combat.spawn("HTNK",{5,-5},1);
  group(combat,{doomed,survivor},{20,20},1);
  combat.enqueue({1,1,1,CommandKind::AttackEntity,attacker,doomed});
  bool destroyed=false;
  for(int i=0;i<100 && !destroyed;++i) {
    combat.step(); const auto state=combat.snapshot();
    destroyed=std::none_of(state.begin(),state.end(),[&](const auto& e) { return e.id==doomed; });
  }
  require(destroyed,"Shared clearance fixture failed to destroy the leading owner");
  const auto moving=view(combat,survivor);
  require(moving.position!=Position{moving.cell.x*1000LL,moving.cell.y*1000LL},"Shared clearance fixture stopped on a cell centre");
  for(const auto cell:{TileCoord{moving.cell.x+1,moving.cell.y},TileCoord{moving.cell.x,moving.cell.y+1},TileCoord{moving.cell.x+1,moving.cell.y+1}}) {
    require(combat.map().cell(cell).occupied,"Destroying one tank released another tank's shared clearance");
    requireRejected<std::invalid_argument>([&] { combat.setBuildable(cell,false); },"Terrain editing entered a surviving tank's sweep");
  }
}
void openGround(const DefinitionRegistry& defs) {
  for(const int columns:{3,6}) for(const int spacing:{1,2}) for(const TileCoord goal:{TileCoord{14,2},TileCoord{14,14}}) {
    World w(defs); std::vector<EntityId> ids; std::map<EntityId,Position> starts;
    for(int y=0;y<(columns==3?3:4);++y) for(int x=0;x<columns;++x) {
      const auto id=w.spawn("HTNK",{x*spacing,y*spacing}); ids.push_back(id); starts[id]=view(w,id).position;
    }
    group(w,ids,goal,1); Measurements m; std::map<EntityId,Tick> departed;
    bool arrived=false; Tick lastDeparture=0,firstDeparture=100000; int movingStalls=0;
    for(int frame=0;frame<1200 && !arrived;++frame) {
      arrived=m.step(w);
      for(const auto& e:w.snapshot()) {
        if(!departed.contains(e.id) && e.position!=starts.at(e.id)) {
          departed[e.id]=w.tick(); firstDeparture=std::min(firstDeparture,w.tick()); lastDeparture=std::max(lastDeparture,w.tick());
        }
        if(departed.contains(e.id) && e.cell!=e.destination && e.position==e.previous) ++movingStalls;
      }
    }
    std::cout<<"open ground units="<<ids.size()<<" spacing="<<spacing<<" goal="<<goal.x<<','<<goal.y<<" first="<<firstDeparture
      <<" last="<<lastDeparture<<" waits="<<m.waits<<" stall="<<m.longestStall<<" yields="<<m.yields
      <<" ratio="<<double(m.travel)/m.direct<<" frames="<<w.tick()<<'\n';
    require(arrived && departed.size()==ids.size(),"Open-ground formation failed to arrive");
    require(lastDeparture<=20,"Open-ground formation waited unnecessarily before departing");
    if(columns==3 && spacing==1) require(firstDeparture==lastDeparture && movingStalls==0 && m.yields==0 && m.waits==0,
      "An already compact parallel formation regressed to serial movement");
    require(double(m.travel)/m.direct<1.2,"Open-ground formation took an unnecessary detour");
    std::vector<int> nearest;
    for(int y=-8;y<=8;++y) for(int x=-8;x<=8;++x) nearest.push_back(x*x+y*y);
    std::sort(nearest.begin(),nearest.end()); int expected=0,actual=0;
    for(std::size_t i=0;i<ids.size();++i) {
      expected+=nearest[i]; const auto c=view(w,ids[i]).destination;
      actual+=(c.x-goal.x)*(c.x-goal.x)+(c.y-goal.y)*(c.y-goal.y);
    }
    require(actual==expected,"Formation destinations did not fill the closest cells around the clicked target");
  }
}
void mixedFormation(const DefinitionRegistry& defs) {
  auto rules=defs.rules(); rules.overlay(IniDocument::parse("[HTNK]\nSpeed=2\nROT=3\nCanPassiveAquire=no\n[MTNK]\nCanPassiveAquire=no"));
  const auto mixed=DefinitionRegistry::compile(rules,defs.art(),defs.replica());
  World w(mixed); std::vector<EntityId> ids;
  for(int y=0;y<3;++y) for(int x=0;x<3;++x) ids.push_back(w.spawn(x==1 && y==1?"HTNK":"MTNK",{x*2,y*2}));
  group(w,ids,{12,2},1); Measurements m; m.finish(w,2200,"mixed nine-unit formation");
  std::set<std::pair<int,int>> goals; for(const auto& [id,c]:m.goals) { (void)id; goals.emplace(c.x,c.y); }
  require(goals.size()==ids.size(),"Assignment gave multiple units one slot");
}
void corridor(const DefinitionRegistry& defs) {
  World w(defs);
  for(int x=0;x<=12;++x) for(int y=-8;y<=8;++y) if(y!=0) w.setBuildable({x,y},false);
  std::vector<EntityId> left,right;
  for(const TileCoord c:{TileCoord{-2,-1},TileCoord{-2,1},TileCoord{-4,0}}) left.push_back(w.spawn("HTNK",c));
  for(const TileCoord c:{TileCoord{14,-1},TileCoord{14,1},TileCoord{16,0}}) right.push_back(w.spawn("MTNK",c));
  group(w,left,{15,0},1); group(w,right,{-3,0},2);
  Measurements m; m.finish(w,2600,"opposing groups in one-cell corridor");
  require(w.tick()<1100 && m.yields<12,"Corridor approach coordination regressed to excessive retreating");

  World cancelled(defs);
  for(int x=0;x<=10;++x) for(int y=-4;y<=4;++y) if(y) cancelled.setBuildable({x,y},false);
  const auto first=cancelled.spawn("HTNK",{-3,0}),opposite=cancelled.spawn("HTNK",{14,0});
  cancelled.enqueue({1,0,1,CommandKind::Move,first,0,{14,0}});
  cancelled.enqueue({1,0,2,CommandKind::Move,opposite,0,{-6,0}});
  cancelled.step(); cancelled.enqueue({2,0,3,CommandKind::Stop,first});
  Measurements expiry; bool arrived=false;
  for(int frame=0;frame<900;++frame) {
    expiry.step(cancelled);
    const auto tank=view(cancelled,opposite);
    if(tank.cell==TileCoord{-6,0} && tank.activity==Activity::Idle) { arrived=true; break; }
  }
  require(arrived,"A cancelled corridor approach retained its direction lease forever");
}
void headOn(const DefinitionRegistry& defs) {
  World w(defs);
  for(int x=0;x<=8;++x) for(int y=-12;y<=12;++y) if(y!=0) w.setBuildable({x,y},false);
  const auto a=w.spawn("HTNK",{1,0}),b=w.spawn("MTNK",{7,0});
  w.enqueue({1,0,1,CommandKind::Move,a,0,{10,0}}); w.enqueue({1,0,2,CommandKind::Move,b,0,{-2,0}});
  Measurements m; m.finish(w,1800,"head-on retreat to passing bay");
  require(m.yields>0,"Head-on recovery never used a passing bay");
}
void intersection(const DefinitionRegistry& defs) {
  World w(defs);
  for(int i=0;i<4;++i) {
    const std::array<TileCoord,4> starts{{{-8,i-2},{8,2-i},{2-i,-8},{i-2,8}}};
    const std::array<TileCoord,4> goals{{{8,i-2},{-8,2-i},{2-i,8},{i-2,-8}}};
    for(int j=0;j<4;++j) {
      const auto id=w.spawn((i+j)%2?"MTNK":"HTNK",starts[j]);
      w.enqueue({1,0,id,CommandKind::Move,id,0,goals[j]});
    }
  }
  Measurements m; m.finish(w,2200,"sixteen-unit crossing");
  require(m.forecastHolds>0 && m.waits<1200 && w.tick()<743,"Crossing prediction did not reduce avoidable traffic waits");
}
void unreachableAndTerrain(const DefinitionRegistry& defs) {
  World w(defs);
  for(int x=7;x<=9;++x) for(int y=-1;y<=1;++y) if(x!=8 || y!=0) w.setBuildable({x,y},false);
  const auto a=w.spawn("HTNK",{}),b=w.spawn("MTNK",{0,3});
  group(w,{a,b},{8,0},1); Measurements m; m.finish(w,800,"enclosed target alternatives");
  require(view(w,a).destination!=TileCoord{8,0} && view(w,b).destination!=TileCoord{8,0},"Assigned a walkable but unreachable cell");
}
void replayAndInterrupt(const DefinitionRegistry& defs) {
  World a(defs),b(defs); std::vector<EntityId> ids;
  for(int i=0;i<6;++i) { ids.push_back(a.spawn("HTNK",{0,i})); b.spawn("HTNK",{0,i}); }
  group(a,ids,{12,3},1); std::reverse(ids.begin(),ids.end()); group(b,ids,{12,3},1);
  for(int tick=0;tick<600;++tick) {
    if(tick==50) { group(a,ids,{-4,3},2); group(b,ids,{-4,3},2); }
    if(tick==85) for(auto* w:{&a,&b}) w->enqueue({w->tick()+1,0,3,CommandKind::Stop,ids.front()});
    a.step(); b.step(); require(a.stateHash()==b.stateHash(),"Group member order changed deterministic replay");
  }
  const auto stopped=view(a,ids.front()).position;
  for(int i=0;i<30;++i) a.step();
  require(view(a,ids.front()).position==stopped,"Stopped tank resumed an obsolete yielding route");
  for(const auto id:ids) if(id!=ids.front()) require(view(a,id).cell==view(a,id).destination,"Mid-route replacement retained an old route reservation");
}
void largerFormation(const DefinitionRegistry& defs) {
  World w(defs); std::vector<EntityId> ids;
  for(int y=0;y<4;++y) for(int x=0;x<6;++x) ids.push_back(w.spawn((x+y)%2?"HTNK":"MTNK",{x*2,y*2}));
  group(w,ids,{20,4},1); Measurements m; m.finish(w,2600,"twenty-four-unit formation");
  for(const auto id:ids) require(view(w,id).destination.x>11,"Budget exhaustion dropped a group member");
}
void factoryTraffic(const DefinitionRegistry& defs) {
  auto profile=defs.replica(); profile.overlay(IniDocument::parse("[Simulation]\nProductionFramesOverride=15"));
  const auto fast=DefinitionRegistry::compile(defs.rules(),defs.art(),profile);
  World w(fast); const auto factory=w.spawn("GAWEAP",{-2,-1});
  std::vector<EntityId> traffic;
  for(int i=0;i<3;++i) traffic.push_back(w.spawn("HTNK",{3,4+i*2}));
  group(w,traffic,{3,-5},1); w.enqueue({1,0,2,CommandKind::Produce,factory,0,{},"MTNK"});
  EntityId child=0;
  for(int tick=0;tick<700;++tick) {
    w.step();
    if(!child && view(w,factory).exitingUnit) {
      child=view(w,factory).exitingUnit;
      w.enqueue({w.tick()+1,0,3,CommandKind::Move,child,0,{9,0}});
    }
    std::set<std::pair<int,int>> occupied;
    for(const auto& e:w.snapshot()) if(e.id!=factory) {
      require(occupied.emplace(e.cell.x,e.cell.y).second,"Factory exit overlapped passing traffic");
      if(e.id!=child) require(!w.structureAt(e.cell),"Passing traffic entered the factory footprint");
    }
  }
  require(child && view(w,child).cell==TileCoord{9,0},"Factory exit/deferred order was starved by crossing traffic");
  for(const auto id:traffic) require(view(w,id).cell==view(w,id).destination,"Factory reservation stranded passing traffic");
}
void sharedTargetAndPendingStop(const DefinitionRegistry& defs) {
  World w(defs); std::vector<EntityId> first,second;
  for(int y=-2;y<=2;y+=2) { first.push_back(w.spawn("HTNK",{-6,y})); second.push_back(w.spawn("MTNK",{6,y})); }
  group(w,first,{},1); group(w,second,{},2);
  Measurements m; m.finish(w,1400,"separate groups sharing a clicked target");
  std::set<std::pair<int,int>> destinations;
  for(const auto& e:w.snapshot()) require(destinations.emplace(e.destination.x,e.destination.y).second,"Independent groups claimed the same final slot");

  World pending(defs); std::vector<EntityId> ids;
  for(int y=0;y<3;++y) for(int x=0;x<4;++x) ids.push_back(pending.spawn("HTNK",{x*2,y*2}));
  for(int y=-2;y<=2;++y) for(int x=20;x<=24;++x)
    if(x==20 || x==24 || y==-2 || y==2) pending.setBuildable({x,y},false);
  group(pending,ids,{22,0},1); pending.step();
  require(view(pending,ids.front()).awaitingFormation,"Fixture did not exercise a multi-frame matching job");
  pending.enqueue({pending.tick()+1,0,2,CommandKind::Stop,ids.front()});
  for(int i=0;i<16;++i) pending.step();
  require(!view(pending,ids.front()).awaitingFormation && view(pending,ids.front()).position==Position{},
    "A pending formation overrode a newer Stop command");
  for(const auto id:ids) require(!view(pending,id).awaitingFormation,"Matching budget exhausted without continuing the job");
}
}
int main(int argc,char** argv) {
  try {
    require(argc==2,"Expected settings directory"); const auto defs=DefinitionRegistry::load(argv[1]);
    algorithms(); openGround(defs); clusteredTargets(defs); collisionAndObstacles(defs); mixedFormation(defs); headOn(defs); corridor(defs); intersection(defs); unreachableAndTerrain(defs); replayAndInterrupt(defs);
    largerFormation(defs); factoryTraffic(defs); sharedTargetAndPendingStop(defs); sharedFlow(defs);
    std::cout<<"Cooperative traffic regressions passed\n"; return 0;
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
