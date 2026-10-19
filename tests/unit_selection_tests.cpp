#include "interaction/unit_selection.h"
#include "presentation/world_view.h"
#include "test_support.h"
#include <array>
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
void dogDroneSelection(const DefinitionRegistry& base) {
  auto rules=base.rules();for(const auto id:{"DOG","ADOG","DRON","HTNK","MTNK","E2"})rules.overlay(IniDocument::parse(std::string("[")+id+"]\nCanPassiveAquire=no\n"));
  const auto d=DefinitionRegistry::compile(rules,base.art(),base.replica());World w(d);
  const auto dog=w.spawn("DOG",{}),drone=w.spawn("DRON",{1,1}),victim=w.spawn("HTNK",{1,0},1);
  auto view=makeWorldView(w,d,0);require(view.infantry.size()==2 && view.vehicles.size()==1 && !entity(w,drone).infantry,"Sprite vehicle was routed through VXL or foot occupancy");
  UnitSelection selection;selection.selectBox({-10000,-10000,10000,10000},view.selectables,{},60,30);require(selection.size()==2,"Mixed dog/drone selection failed");
  std::uint64_t seq=0;require(issueSelectionCommand(w,d,selection,CommandKind::AttackEntity,{1,0},victim,seq)==1,"Dog UI accepted a tank as prey");
  require(issueSelectionCommand(w,d,selection,CommandKind::AttackGround,{2,2},0,seq)==0,"Leap unit accepted ground attack");
  bool sawLeap=false,sawInfest=false;
  for(int i=0;i<70;++i) {
    w.step();const auto robot=entity(w,drone);view=makeWorldView(w,d,selection.ids());
    if(robot.leaping) {
      sawLeap=true;require(std::none_of(view.selectables.begin(),view.selectables.end(),[&](const auto& u){return u.id==drone;}),"Jump projectile remained commandable");
      const auto sprite=std::find_if(view.infantry.begin(),view.infantry.end(),[&](const auto& u){return u.id==drone;});
      require(sprite!=view.infantry.end() && sprite->leaping && sprite->frame>=0 && sprite->frame<32,"Native jump atlas/facing missing");
    }
    if(robot.parasiteHost) {
      sawInfest=true;require(std::none_of(view.infantry.begin(),view.infantry.end(),[&](const auto& u){return u.id==drone;}),"Infesting drone still visible");
      selection.prune(w.snapshot());require(!selection.contains(drone) && selection.contains(dog),"Hidden parasite retained ordinary selection");break;
    }
  }
  require(sawLeap && sawInfest,"Presentation test never traversed jump/infestation");
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
void infantrySelection(const DefinitionRegistry& definitions) {
  World world(definitions);
  const auto tank=world.spawn("HTNK",{}),gi=world.spawn("E1",{1,0}),cons=world.spawn("E2",{1,0});
  const auto enemy=world.spawn("E2",{8,8},1);
  const auto view=makeWorldView(world,definitions,0);UnitSelection selection;
  selection.selectBox({-1000,-1000,1000,1000},view.selectables,{},60,30);
  require(selection.size()==3 && selection.contains(tank) && selection.contains(gi) && selection.contains(cons) && !selection.contains(enemy),"Mixed box selection failed");
  selection.toggleUnit(gi,view.selectables);require(selection.size()==2,"Infantry shift-toggle failed");
  selection.toggleUnit(gi,view.selectables);
  std::uint64_t sequence=0;require(issueSelectionCommand(world,definitions,selection,CommandKind::Move,{7,2},0,sequence)==3,"Mixed move command lost infantry");
  for(int i=0;i<320;++i)world.step();
  for(const auto id:{tank,gi,cons})require(entity(world,id).activity==Activity::Idle,"Mixed formation failed to arrive");
  selection.selectOne(gi);require(issueSelectionCommand(world,definitions,selection,CommandKind::Deploy,{},0,sequence)==1,"GI deploy capability was filtered out");
  world.step();require(entity(world,gi).infantry->posture==InfantryPosture::Deploying,"Selection did not deploy GI");
}
void airSelection(const DefinitionRegistry& definitions) {
  auto rules=definitions.rules();rules.overlay(IniDocument::parse("[FLAKT]\nCanPassiveAquire=no\n[JUMPJET]\nCanPassiveAquire=no\n"));
  const auto d=DefinitionRegistry::compile(rules,definitions.art(),definitions.replica());World world(d);
  const auto flak=world.spawn("FLAKT",{}),gi=world.spawn("E1",{1,0}),rock=world.spawn("JUMPJET",{1,0}),enemy=world.spawn("JUMPJET",{6,0},1);
  for(int i=0;i<30;++i)world.step();
  const auto view=makeWorldView(world,d,0);UnitSelection selection;
  selection.selectBox({-1000,-1000,1000,1000},view.selectables,{},60,30);std::uint64_t sequence=0;
  require(selection.size()==3 && selection.contains(flak) && selection.contains(gi) && selection.contains(rock),"Air/ground mixed box selection failed");
  require(issueSelectionCommand(world,d,selection,CommandKind::AttackEntity,{},enemy,sequence)==2,"Mixed selection sent GI an illegal AA order");
  selection.selectOne(rock);
  const auto point=*std::find_if(view.selectables.begin(),view.selectables.end(),[&](const auto& unit){return unit.id==rock;});
  const float x=(point.position.x-point.position.y)*30,y=(point.position.x+point.position.y)*15-point.heightLeptons*30/256-9;
  selection.selectBox({x-2,y-2,x+2,y+2},view.selectables,{},60,30);
  require(selection.size()==1 && selection.contains(rock),"Air box selection used the ground anchor");
  require(issueSelectionCommand(world,d,selection,CommandKind::EnterTransport,{},world.spawn("HTK",{2,0}),sequence)==0,"Selection let a jumpjet enter transport");
}
void specialVehicleSelection(const DefinitionRegistry& definitions) {
  auto rules=definitions.rules();rules.overlay(IniDocument::parse("[SREF]\nCanPassiveAquire=no\n[MGTK]\nCanPassiveAquire=no\n[HTNK]\nCanPassiveAquire=no\nStrength=10000\n"));
  const auto d=DefinitionRegistry::compile(rules,definitions.art(),definitions.replica());World world(d);
  const auto own=world.spawn("MGTK",{}),enemy=world.spawn("MGTK",{4,0},1),prism=world.spawn("SREF",{0,3}),target=world.spawn("HTNK",{4,3},1);
  world.step();auto view=makeWorldView(world,d,own);
  for(const auto& vehicle:view.vehicles) {
    if(vehicle.entityId==own)require(vehicle.disguiseVisible && hitTestRhinoUnit(vehicle,0,-5,{},60,30),"Friendly tree disguise is not selectable");
    if(vehicle.entityId==enemy)require(vehicle.disguiseVisible && !vehicle.disguiseTargetable && !hitTestRhinoUnit(vehicle,120,60,{},60,30),"Undetected enemy disguise exposes its attack hitbox");
  }
  UnitSelection selection;selection.selectBox({-1000,-1000,1000,1000},view.selectables,{},60,30);std::uint64_t sequence=0;
  require(selection.contains(own) && selection.contains(prism) && selection.size()==2,"Disguised friendly vehicle was lost from mixed box selection");
  selection.selectOne(prism);issueSelectionCommand(world,d,selection,CommandKind::AttackEntity,{},target,sequence);world.step();world.step();view=makeWorldView(world,d,prism);
  bool charged=false;for(const auto& vehicle:view.vehicles)if(vehicle.entityId==prism)charged=vehicle.turretVariant==3;
  require(charged && !view.beams.empty(),"Prism beam / turret stage did not reach presentation");
}
void miragePresentationTiming(const DefinitionRegistry& definitions) {
  auto rules=definitions.rules();rules.overlay(IniDocument::parse(
    "[MGTK]\nCanPassiveAquire=no\nStrength=10000\n[HTNK]\nCanPassiveAquire=yes\nStrength=10000\n[E2]\nCanPassiveAquire=no\n"));
  const auto d=DefinitionRegistry::compile(rules,definitions.art(),definitions.replica());
  const auto vehicle=[&](const World& world,EntityId id,std::uint32_t viewer=0) {
    const auto view=makeWorldView(world,d,EntityId{},viewer);
    for(const auto& unit:view.vehicles)if(unit.entityId==id)return unit;
    throw std::runtime_error("Mirage missing from viewer snapshot");
  };
  const auto stepTo=[](World& world,Tick tick){while(world.tick()<tick)world.step();};
  World idle(d);const auto first=idle.spawn("MGTK",{});
  stepTo(idle,7);const auto second=idle.spawn("MGTK",{2,0});
  require(entity(idle,second).disguiseStarted==7,"New disguise inherited the global blink clock");
  stepTo(idle,14);require(vehicle(idle,first).disguiseVisible && vehicle(idle,second).disguiseVisible,"Idle tree phase ended early");
  stepTo(idle,15);require(!vehicle(idle,first).disguiseVisible && vehicle(idle,second).disguiseVisible,"Independent owner blink phases became synchronized");
  require(vehicle(idle,first,1).disguiseVisible && !vehicle(idle,first,1).disguiseTargetable && !idle.recognizes(first,1),
    "Owner identification blink leaked into the enemy view or targeting");
  stepTo(idle,29);require(!vehicle(idle,first).disguiseVisible,"Owner vehicle phase ended early");
  stepTo(idle,30);require(vehicle(idle,first).disguiseVisible && !vehicle(idle,second).disguiseVisible,"Owner cycle boundary is wrong");
  idle.enqueue({31,0,1,CommandKind::Move,first,0,{0,3}});idle.step();
  require(!entity(idle,first).disguised && !vehicle(idle,first).disguiseVisible,"Moving unit retained its tree");
  for(int i=0;i<250 && !entity(idle,first).disguised;++i)idle.step();
  const auto arrival=idle.tick();require(entity(idle,first).disguised && entity(idle,first).disguiseStarted==arrival && vehicle(idle,first).disguiseVisible,
    "Stopping did not start a complete new tree phase");
  stepTo(idle,arrival+14);require(vehicle(idle,first).disguiseVisible,"Restarted tree phase was shortened by global time");
  idle.step();require(!vehicle(idle,first).disguiseVisible,"Restarted owner cycle missed its boundary");

  // One entity rendered from both sides: the actual weapon event starts the
  // visual timer; it never makes this stationary unit eligible for acquisition.
  World fire(d);const auto mirage=fire.spawn("MGTK",{},1),target=fire.spawn("HTNK",{4,0});
  fire.enqueue({1,1,1,CommandKind::AttackEntity,mirage,target});
  Tick shot=0;
  for(int i=0;i<100 && !shot;++i){fire.step();for(const auto& s:fire.shots())if(s.sourceType=="MGTK" && s.started==fire.tick())shot=s.started;}
  require(shot && entity(fire,mirage).disguiseFireUntil==shot+15,"Visual timer did not start on the actual shot");
  bool muzzle=false;for(const auto& effect:fire.effects())if(effect.animation=="VTMUZZLE" && effect.started==shot) {
    muzzle=true;require(effect.heightLeptons==80,"Mirage muzzle lost original PrimaryFireFLH height");
  }
  require(muzzle,"Original Mirage muzzle is missing on the reveal frame");
  fire.enqueue({shot+1,1,2,CommandKind::Stop,mirage});
  for(int age=0;age<15;++age) {
    stepTo(fire,shot+age);
    require(!vehicle(fire,mirage).disguiseVisible && vehicle(fire,mirage).disguiseTargetable && !vehicle(fire,mirage,1).disguiseVisible &&
            !fire.recognizes(mirage,0) && entity(fire,mirage).hp==10000,"Fire reveal flickered, lost its cursor, or caused logical acquisition");
    const auto hash=fire.stateHash();(void)makeWorldView(fire,d,mirage,1);(void)makeWorldView(fire,d,mirage,0);
    require(fire.stateHash()==hash,"Rendering an alternate viewer changed disguise timers");
  }
  stepTo(fire,shot+15);
  require(vehicle(fire,mirage).disguiseVisible && !vehicle(fire,mirage).disguiseTargetable && vehicle(fire,mirage,1).disguiseVisible,
    "15-frame fire reveal did not restore the tree in both views");
  stepTo(fire,shot+29);require(vehicle(fire,mirage,1).disguiseVisible,"Fire expiry resumed a partial owner tree phase");
  fire.step();require(!vehicle(fire,mirage,1).disguiseVisible && vehicle(fire,mirage).disguiseVisible,"Resumed owner blink leaked to enemies");

  World exposure(d);const auto hidden=exposure.spawn("MGTK",{});exposure.spawn("E2",{0,1},1);exposure.step();
  require(entity(exposure,hidden).disguiseFireUntil==0 && entity(exposure,hidden).disguiseExposedUntil==21 && exposure.recognizes(hidden,1) &&
          !vehicle(exposure,hidden,1).disguiseVisible && vehicle(exposure,hidden,1).disguiseTargetable,"Infantry exposure was confused with a visual shot");

  // Sample the same command stream at different render rates and game speeds.
  // These boundaries describe the current compatibility profile, not a claim
  // that the idle cadence has been measured from an unmodified RA2 executable.
  constexpr std::array<std::pair<Tick,bool>,9> changes{{{0,true},{15,false},{30,true},{35,false},{50,true},{65,false},{80,true},{95,false},{110,true}}};
  for(int rate:{15,60}) {
    std::uint64_t expectedHash=0;
    for(int hz:{30,60,144}) {
      World sampled(d);sampled.setGameSpeed({GameSpeedMode::Multiplayer,rate==15?2:6});
      const auto m=sampled.spawn("MGTK",{}),t=sampled.spawn("HTNK",{4,0},1);
      sampled.enqueue({35,0,1,CommandKind::AttackEntity,m,t});sampled.enqueue({36,0,2,CommandKind::Stop,m});
      for(int frame=1;frame<=hz*120/rate;++frame) {
        sampled.advance(std::uint64_t(frame)*1000000/hz-std::uint64_t(frame-1)*1000000/hz);
        bool tree=true;for(const auto& [at,show]:changes)if(at<=sampled.tick())tree=show;
        const bool shotVisible=sampled.tick()>=35 && sampled.tick()<50;
        require(vehicle(sampled,m).disguiseVisible==tree && vehicle(sampled,m,1).disguiseVisible==!shotVisible &&
                !sampled.recognizes(m,1),"Wall clock/render FPS changed the owner blink or fire reveal");
      }
      require(sampled.tick()==120,"Mirage timeline did not run the same number of game frames");
      if(expectedHash)require(sampled.stateHash()==expectedHash,"Rendering frequency changed Mirage replay state");
      else expectedHash=sampled.stateHash();
    }
  }
}
void mirageMuzzleTiming(const DefinitionRegistry& definitions) {
  const auto& muzzle=definitions.effectAnimation("VTMUZZLE");
  require(muzzle.frameCount==11 && muzzle.frameDelay==1 && !muzzle.normalized,"Original 11-frame Mirage muzzle metadata changed");
  for(const auto speed:{GameSpeed{GameSpeedMode::Multiplayer,6},GameSpeed{GameSpeedMode::Singleplayer,6}}) {
  World world(definitions);world.setGameSpeed(speed);const auto tank=world.spawn("MGTK",{}),target=world.spawn("HTNK",{4,0},1);
  world.enqueue({1,0,1,CommandKind::AttackEntity,tank,target});Tick fired=0;
  for(int i=0;i<100 && !fired;++i){world.step();for(const auto& effect:world.effects())if(effect.animation=="VTMUZZLE")fired=effect.started;}
  require(fired,"Mirage did not create a muzzle animation");world.enqueue({fired+1,0,2,CommandKind::Stop,tank});
  for(int age=0;age<=11;++age) {
    while(world.tick()<fired+age)world.step();
    const auto view=makeWorldView(world,definitions,tank);
    bool visible=false;for(const auto& effect:view.effects)if(effect.animation=="VTMUZZLE") {
      visible=true;require(effect.animationFrame==age,"Muzzle repeated/delayed a source frame after the firing event");
    }
    require(visible==(age<11),"Original muzzle did not play exactly 11 game frames");
    for(const auto& effect:view.effects)if(effect.animation=="IRONFX") {
      Tick impact=0;for(const auto& snapshot:world.effects())if(snapshot.animation=="IRONFX")impact=snapshot.started;
      require(effect.animationFrame==int(world.tick()-impact) && effect.alpha==0.5f,"Mirage hit animation timing/translucency is wrong");
    }
  }
  const auto view=makeWorldView(world,definitions,tank,1);
  for(const auto& unit:view.vehicles)if(unit.entityId==tank)require(!unit.disguiseVisible,"Ending muzzle animation shortened the independent 15-frame reveal");
  }
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
void teslaSelection(const DefinitionRegistry& definitions) {
  auto rules=definitions.rules();rules.overlay(IniDocument::parse("[SHK]\nCanPassiveAquire=no\n[TTNK]\nCanPassiveAquire=no\n[E1]\nCanPassiveAquire=no\n[TESLA]\nCanPassiveAquire=no\n"));
  const auto d=DefinitionRegistry::compile(rules,definitions.art(),definitions.replica());
  World world(d);const auto coil=world.spawn("TESLA",{3,0}),foot=world.spawn("SHK",{2,0}),tank=world.spawn("TTNK",{0,2});world.spawn("E1",{});
  UnitSelection selected;const auto mixed=makeWorldView(world,d,0);
  selected.selectBox({-10000,-10000,10000,10000},mixed.selectables,{},60,30);std::uint64_t sequence=0;
  require(issueSelectionCommand(world,d,selected,CommandKind::AttackEntity,{},coil,sequence)==1,"Friendly coil click must command only Tesla infantry");
  for(int i=0;i<50;++i)world.step();
  require(entity(world,coil).coilChargers==1 && entity(world,coil).hp==600 && entity(world,foot).activeWeapon=="ASSAULTBOLT","Friendly coil interaction did not reach simulation");
  const auto view=makeWorldView(world,d,foot);bool selectedFoot=false;
  for(const auto& unit:view.infantry)if(unit.id==foot)selectedFoot=unit.selected && unit.frame>=0 && unit.frame<definitions.unit("SHK").infantry->bodyFrames;
  require(selectedFoot && entity(world,tank).cell==TileCoord{0,2},"Tesla mixed selection lost infantry or moved friendly tank");
  const auto victim=world.spawn("E2",{6,0},1);selected.selectOne(foot);
  require(issueSelectionCommand(world,d,selected,CommandKind::AttackEntity,{},victim,sequence)==1,"Charging trooper cannot change to enemy target");
  world.step();require(entity(world,coil).coilChargers==0,"Changing selection target retained coil charging");
}
void missileSelection(const DefinitionRegistry& definitions) {
  auto rules=definitions.rules();rules.overlay(IniDocument::parse("[FLAKT]\nCanPassiveAquire=no\n[HTNK]\nCanPassiveAquire=no\n"));
  const auto d=DefinitionRegistry::compile(rules,definitions.art(),definitions.replica());World world(d);
  const auto launcher=world.spawn("V3",{}),target=world.spawn("GAPOWR",{10,0},1),flak=world.spawn("FLAKT",{3,0},1),tank=world.spawn("HTNK",{3,2},1);
  world.enqueue({1,0,1,CommandKind::AttackEntity,launcher,target});for(int i=0;i<70;++i)world.step();
  EntityId rocket=0;for(const auto& e:world.snapshot())if(e.missile)rocket=e.id;require(rocket,"Missile selection fixture failed");
  const auto own=makeWorldView(world,d,0),enemy=makeWorldView(world,d,EntityId{},1);
  require(std::none_of(own.selectables.begin(),own.selectables.end(),[&](const auto& e){return e.id==rocket;}),"Owned missile entered selection list");
  UnitSelection selected;selected.selectBox({-10000,-10000,10000,10000},own.vehicles,{},60,30);require(!selected.contains(rocket),"Legacy vehicle box selected owned missile");
  selected.selectOne(rocket);selected.prune(world.snapshot());require(selected.size()==0,"Selection retained spawned missile");
  bool hittable=false;for(const auto& v:enemy.vehicles)if(v.entityId==rocket) {
    const auto p=rhinoGroundAnchor(v,{},60,30);hittable=hitTestRhinoUnit(v,p.x,p.y-v.heightLeptons*30/256,{},60,30);
    require(!v.commandable && v.pitchDegrees>0,"Missile pitch/commandability lost in presentation");
  }
  require(hittable,"Enemy missile hitbox did not follow flight height");
  std::uint64_t sequence=0;selected.selectOne(tank);require(!issueSelectionCommand(world,d,selected,CommandKind::AttackEntity,{},rocket,sequence,1),"Ground-only tank targeted missile");
  selected.selectOne(flak);require(issueSelectionCommand(world,d,selected,CommandKind::AttackEntity,{},rocket,sequence,1)==1,"AA selection could not target missile");
}
void passengerSelection(const DefinitionRegistry& definitions) {
  World world(definitions);const auto carrier=world.spawn("HTK",{3,0}),gi=world.spawn("E1",{}),engineer=world.spawn("ENGINEER",{});
  UnitSelection selected;const auto view=makeWorldView(world,definitions,0);
  selected.selectBox({-10000,-10000,10000,10000},view.selectables,{},60,30);
  std::uint64_t sequence=0;
  require(issueSelectionCommand(world,definitions,selected,CommandKind::EnterTransport,{},carrier,sequence)==2,"Mixed selection did not board its infantry");
  for(int i=0;i<250;++i)world.step();
  const auto loaded=makeWorldView(world,definitions,0);
  require(loaded.infantry.empty() && loaded.selectables.size()==1 && entity(world,carrier).cargo->used==2,"Contained infantry remained visible/selectable");
  selected.prune(world.snapshot());require(selected.size()==1 && selected.primary()==carrier,"Selection retained passengers after boarding");
  require(issueSelectionCommand(world,definitions,selected,CommandKind::Unload,{},0,sequence)==1,"Selected carrier could not unload");
  for(int i=0;i<40;++i)world.step();
  const auto exited=makeWorldView(world,definitions,0);require(exited.infantry.size()==2 && exited.selectables.size()==3,"Unloaded passengers did not regain their presentation");
  const auto house=world.spawn("CAHSE01",{8,0},2);selected.clear();
  selected.selectBox({-10000,-10000,10000,10000},exited.selectables,{},60,30);
  require(issueSelectionCommand(world,definitions,selected,CommandKind::EnterBuilding,{},house,sequence)==1,"Garrison selection treated engineer/vehicle as occupants");
  (void)gi;(void)engineer;
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
void ivanSelection(const DefinitionRegistry& base) {
  auto r=base.rules();for(const auto id:{"IVAN","ENGINEER","HTNK"})r.overlay(IniDocument::parse(std::string("[")+id+"]\nCanPassiveAquire=no\n"));
  const auto d=DefinitionRegistry::compile(r,base.art(),base.replica());World w(d);const auto ivan=w.spawn("IVAN",{}),target=w.spawn("HTNK",{1,0}),engineer=w.spawn("ENGINEER",{0,-1});
  UnitSelection selected;selected.selectOne(ivan);std::uint64_t seq=0;
  require(issueSelectionCommand(w,d,selected,CommandKind::AttackEntity,{},target,seq)==1,"Selection refused a friendly Ivan bomb");
  require(issueSelectionCommand(w,d,selected,CommandKind::AttackGround,{},0,seq)==0,"Ivan selection issued a ground bomb");
  for(int i=0;i<160 && !entity(w,target).bomb;++i)w.step();require(entity(w,target).bomb.has_value(),"Friendly bomb did not attach");
  selected.selectOne(engineer);require(issueSelectionCommand(w,d,selected,CommandKind::AttackEntity,{},target,seq)==1,"Engineer selection refused disarm");
  for(int i=0;i<160 && entity(w,target).bomb;++i)w.step();require(!entity(w,target).bomb && entity(w,engineer).hp==75,"Selected engineer failed to disarm without consumption");
  require(issueSelectionCommand(w,d,selected,CommandKind::AttackEntity,{},target,seq)==0,"Engineer attacked an unbombed vehicle");
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
void commandoSelection(const ra2::DefinitionRegistry& definitions) {
  using namespace ra2;
  World w(definitions);const auto spy=w.spawn("SPY",{}),copy=w.spawn("E2",{20,0},1),tanya=w.spawn("TANY",{0,3}),building=w.spawn("GAPOWR",{2,3},1),chrono=w.spawn("CLEG",{0,6});
  std::uint64_t sequence=0;UnitSelection selected;selected.selectOne(spy);
  require(issueSelectionCommand(w,definitions,selected,CommandKind::AttackEntity,{},copy,sequence)==1,"Spy disguise selection rejected");
  require(issueSelectionCommand(w,definitions,selected,CommandKind::AttackGround,{},0,sequence)==0,"Spy ground attack accepted");w.step();
  const auto own=makeWorldView(w,definitions,spy,0),enemy=makeWorldView(w,definitions,EntityId{},1);
  const auto appearance=[&](const auto& view)->const InfantryVisual& {return *std::find_if(view.infantry.begin(),view.infantry.end(),[&](auto& e){return e.id==spy;});};
  require(appearance(own).type=="SPY" && appearance(enemy).type=="E2" && appearance(enemy).owner==1,"Spy viewer perspective/remap incorrect");
  selected.selectOne(tanya);require(issueSelectionCommand(w,definitions,selected,CommandKind::AttackEntity,{},building,sequence)==1,"C4 target command rejected");
  selected.selectOne(spy);require(issueSelectionCommand(w,definitions,selected,CommandKind::EnterBuilding,{},building,sequence)==1,"Spy infiltration selector rejected");
  selected.selectOne(chrono);require(issueSelectionCommand(w,definitions,selected,CommandKind::Move,{8,6},0,sequence)==1,"Chrono move selector rejected");
}
int main(int argc,char** argv) {
  try {
    require(argc==2,"Expected settings directory"); const auto definitions=DefinitionRegistry::load(argv[1]);
    gestureTests(); commandoSelection(definitions); ivanSelection(definitions); dogDroneSelection(definitions); selectionTests(definitions); infantrySelection(definitions); airSelection(definitions); specialVehicleSelection(definitions); miragePresentationTiming(definitions); mirageMuzzleTiming(definitions); teslaSelection(definitions); missileSelection(definitions); passengerSelection(definitions); groupMovement(definitions); largerGroup(definitions); groupAttack(definitions);
    std::cout<<"Unit selection regressions passed\n"; return 0;
  } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
