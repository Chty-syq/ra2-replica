#include "interaction/unit_selection.h"
#include "presentation/world_view.h"
#include "test_support.h"
#include <iostream>
#include <set>

using namespace ra2;
namespace {
EntitySnapshot entity(const World& world,EntityId id) {
  for(const auto& item:world.snapshot()) if(item.id==id) return item;
  throw std::runtime_error("Selected entity disappeared");
}
void selectAll(UnitSelection& selection,const World& world,const DefinitionRegistry& definitions) {
  const auto view=makeWorldView(world,definitions,0);
  selection.selectBox({-10000,-10000,10000,10000},view.vehicles,{},60,30);
}
void gestureTests() {
  SelectionGesture gesture;
  gesture.begin({100,100}); gesture.update({102,102});
  require(!gesture.release({102,102})->dragged,"A click was mistaken for a selection drag");
  for(const Vec2 end: {Vec2{40,40},Vec2{140,40},Vec2{40,140},Vec2{140,140}}) {
    gesture.begin({100,100},true); gesture.update(end);
    const auto result=gesture.release(end);
    require(result && result->dragged && result->additive && result->rectangle.contains({100,100}) &&
            result->rectangle.contains(end),"Reverse-direction/additive drag lost its rectangle");
  }
  gesture.begin({100,100}); gesture.update({150,150});
  require(gesture.release({100,100})->dragged,"Returning to the drag origin issued an accidental click command");
  gesture.begin({100,100}); gesture.cancel();
  require(!gesture.release({150,150}),"Cancelled drag selected units");
}
void selectionTests(const DefinitionRegistry& definitions) {
  World world(definitions);
  const auto first=world.spawn("HTNK",{}),second=world.spawn("MTNK",{1,0});
  const auto enemy=world.spawn("HTNK",{2,0},1),building=world.spawn("GAPOWR",{-4,-4});
  const auto view=makeWorldView(world,definitions,0);
  UnitSelection selection;
  selection.selectBox(SelectionRect::between({580,440},{480,380}),view.vehicles,{500,400},60,30);
  require(selection.size()==2 && selection.contains(first) && selection.contains(second) && !selection.contains(enemy),
          "Box selection failed to select both friendly tanks or selected an enemy");
  const auto selected=makeWorldView(world,definitions,selection.ids());
  int highlights=0; for(const auto& vehicle:selected.vehicles) highlights+=vehicle.selected;
  require(highlights==2,"Only the primary group member has a selection indicator");
  selection.toggleVehicle(first,view.vehicles);
  require(selection.size()==1 && selection.contains(second),"Shift click did not remove a selected tank");
  selection.toggleVehicle(first,view.vehicles); selection.toggleVehicle(enemy,view.vehicles);
  require(selection.size()==2 && !selection.contains(enemy),"Shift click added an enemy");
  selection.selectOne(building); selectAll(selection,world,definitions);
  require(selection.size()==2 && !selection.contains(building),"Group selection retained a building");
  selection.selectBox({0,0,1,1},view.vehicles,{500,400},60,30,true);
  require(selection.size()==2,"Empty additive selection discarded the current group");
  auto living=world.snapshot(); for(auto& item:living) if(item.id==first) item.hp=0;
  selection.prune(living); require(selection.size()==1 && selection.primary()==second,"Dead unit was not removed from selection");
  selection.selectBox({0,0,1,1},view.vehicles,{500,400},60,30);
  require(selection.size()==0,"Empty replacement box did not clear the group");
}
void groupMovement(const DefinitionRegistry& definitions) {
  for(const bool blocked:{false,true}) {
    World world(definitions);
    if(blocked) world.setBuildable({8,1},false);
    const auto first=world.spawn("HTNK",{}),second=world.spawn("MTNK",{0,2});
    UnitSelection selection; selectAll(selection,world,definitions); std::uint64_t sequence=0;
    require(issueSelectionCommand(world,definitions,selection,CommandKind::Move,{8,1},0,sequence)==2 && sequence==1,
            "Group move did not enqueue both units");
    world.step(); const auto a=entity(world,first),b=entity(world,second);
    require(a.destination!=b.destination,"Group members contend for one destination cell");
    for(const auto destination:{a.destination,b.destination})
      require(std::abs(destination.x-8)<=1 && std::abs(destination.y-1)<=1,"Formation moved away from the shared target");
    for(int i=0;i<350;++i) world.step();
    require(entity(world,first).cell==a.destination && entity(world,second).cell==b.destination,
            "Group move deadlocked before reaching distinct slots");
    require(world.occupant(a.destination)==first && world.occupant(b.destination)==second,"Formation occupancy overlaps");
    issueSelectionCommand(world,definitions,selection,CommandKind::Move,{20,4},0,sequence);
    for(int i=0;i<20;++i) world.step();
    require(issueSelectionCommand(world,definitions,selection,CommandKind::Stop,{},0,sequence)==2,"Stop ignored a group member");
    for(int i=0;i<35;++i) world.step();
    const auto pausedA=entity(world,first).position,pausedB=entity(world,second).position;
    for(int i=0;i<15;++i) world.step();
    require(entity(world,first).position==pausedA && entity(world,second).position==pausedB,"Group stop left a unit moving");
  }
}
void largerGroup(const DefinitionRegistry& definitions) {
  World world(definitions); std::vector<EntityId> tanks;
  for(int y=0;y<=4;y+=2) for(int x=0;x<=4;x+=2)
    tanks.push_back(world.spawn((x+y)%4?"MTNK":"HTNK",{x,y}));
  UnitSelection selection; selectAll(selection,world,definitions); std::uint64_t sequence=0;
  require(issueSelectionCommand(world,definitions,selection,CommandKind::Move,{10,3},0,sequence)==tanks.size(),
          "Larger group did not receive every movement command");
  world.step(); std::set<std::pair<int,int>> slots;
  for(const auto tank:tanks) { const auto destination=entity(world,tank).destination; slots.emplace(destination.x,destination.y); }
  require(slots.size()==tanks.size(),"Larger formation contains duplicate destination cells");
  for(int i=0;i<800;++i) world.step();
  for(const auto tank:tanks) { const auto view=entity(world,tank);
    if(view.cell!=view.destination) std::cerr<<"Unfinished tank "<<tank<<" at "<<view.cell.x<<','<<view.cell.y<<" target "<<view.destination.x<<','<<view.destination.y<<'\n';
    require(view.cell==view.destination,"Larger formation blocks a slower member from its destination");
  }
}
void groupAttack(const DefinitionRegistry& definitions) {
  auto rules=definitions.rules(); rules.overlay(IniDocument::parse("[HTNK]\nCanPassiveAquire=no\n[MTNK]\nCanPassiveAquire=no"));
  const auto commanded=DefinitionRegistry::compile(rules,definitions.art(),definitions.replica());
  World world(commanded); world.spawn("HTNK",{}); world.spawn("MTNK",{0,2});
  const auto target=world.spawn("AMCV",{4,1},1);
  UnitSelection selection; selectAll(selection,world,commanded); std::uint64_t sequence=0;
  require(issueSelectionCommand(world,commanded,selection,CommandKind::AttackEntity,{4,1},target,sequence)==2,
          "Group attack did not command both weapons");
  std::set<std::string> shooters;
  for(int i=0;i<50;++i) { world.step(); for(const auto& shot:world.shots()) shooters.insert(shot.sourceType); }
  require(shooters.contains("HTNK") && shooters.contains("MTNK") && entity(world,target).hp<entity(world,target).maxHp,
          "Both tanks did not fire at the shared enemy");
  require(issueSelectionCommand(world,commanded,selection,CommandKind::AttackGround,{3,1},0,sequence)==2,
          "Forced ground attack ignored a group member");
  selection.selectOne(target);
  require(issueSelectionCommand(world,commanded,selection,CommandKind::Move,{8,1},0,sequence)==0,
          "Selection issued an order to an enemy unit");
}
}
int main(int argc,char** argv) {
  try {
    require(argc==2,"Expected settings directory"); const auto definitions=DefinitionRegistry::load(argv[1]);
    gestureTests(); selectionTests(definitions); groupMovement(definitions); largerGroup(definitions); groupAttack(definitions);
    std::cout<<"Unit selection regressions passed\n"; return 0;
  } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
