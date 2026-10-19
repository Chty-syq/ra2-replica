#include "rhino_unit.h"
#include "weapon_system.h"
#include "test_support.h"
#include "simulation_fixture.h"
#include "presentation/world_view.h"
#include "presentation/building_health.h"
#include "foundation.h"
#include "iso_world.h"
#include "demo_scene.h"
#include "interaction/unit_selection.h"

#include <cmath>
#include <iostream>
#include <tuple>

namespace {
void sidebarPowerTransitions() {
  SidebarState state;state.powerProduced=200;state.powerConsumed=100;state.credits=10000;
  updateSidebarState(state,1000);
  auto& display=state.powerAnimation;
  require(display.initialized && display.produced==200 && display.consumed==100 && display.pulse==0,
          "Sidebar did not initialize from actual power without a fake transition");
  updateSidebarState(state,1100);
  require(display.pulse==0,"Unchanged initial power flashed");
  state.powerProduced=400;updateSidebarState(state,1200);
  require(display.produced==200 && display.consumed==100 && display.pulse>0,"Power change jumped to its target");
  state.credits=0;updateSidebarState(state,1440);
  require(display.produced>200 && display.produced<400 && display.consumed==100 && display.started==1200,
          "Money changes restarted power animation or output animation moved the load");
  const auto halfway=display.produced;
  updateSidebarState(state,1680);
  require(display.produced==400 && display.consumed==100,"Power animation did not reach its target");
  state.powerConsumed=450;updateSidebarState(state,1800);updateSidebarState(state,2040);
  require(display.produced==400 && display.consumed>100 && display.consumed<450,
          "Increasing load failed to animate independently from production");
  const auto intermediate=display.consumed;
  state.powerProduced=0;state.powerConsumed=0;updateSidebarState(state,2040);
  require(display.produced==400 && display.consumed==intermediate,"Retargeting in flight caused a visual jump");
  updateSidebarState(state,2200);
  require(display.produced>0 && display.produced<400 && display.consumed>0 && display.consumed<intermediate,
          "Power removal overshot or skipped the downward transition");
  updateSidebarState(state,2800);
  require(display.produced==0 && display.consumed==0 && display.scale>=400 && display.pulse==0,
          "Empty power network retained power or an invalid display scale");
  state.powerOutage=true;updateSidebarState(state,2900);
  require(display.outage && display.pulse>0 && display.produced==0,"Outage status failed to refresh the power meter");

  SidebarState regular,wrapped;
  for(auto* item:{&regular,&wrapped}){item->powerProduced=200;item->powerConsumed=100;}
  updateSidebarState(regular,0);updateSidebarState(wrapped,UINT32_MAX-200);
  regular.powerProduced=wrapped.powerProduced=400;
  updateSidebarState(regular,100);updateSidebarState(wrapped,UINT32_MAX-100);
  for(std::uint32_t t=116;t<340;t+=16)updateSidebarState(regular,t);
  updateSidebarState(regular,340);updateSidebarState(wrapped,139);
  require(std::abs(regular.powerAnimation.produced-halfway)<1e-8 &&
          std::abs(regular.powerAnimation.produced-wrapped.powerAnimation.produced)<1e-8,
          "Power animation depends on refresh rate or breaks when the UI timer wraps");
  state.powerProduced=state.powerConsumed=INT32_MAX;updateSidebarState(state,3000);updateSidebarState(state,4000);
  require(std::isfinite(display.scale) && display.scale>display.produced && display.consumed==INT32_MAX,
          "Large power values overflowed the meter scale");
}

void mixedSidebarProduction(const ra2::DefinitionRegistry& definitions) {
  for(const auto tab:{SidebarTab::Infantry,SidebarTab::Vehicles})for(const auto side:kAllBuildFactions) {
    const bool infantry=tab==SidebarTab::Infantry,allied=side==BuildFaction::Allied;
    const auto icons=makeSidebarPanel(tab);
    const auto entry=std::find_if(icons.begin(),icons.end(),[&](const auto& icon) {
      return icon.id==(infantry?"engnicon":"mcvicon") && icon.faction==side;
    });
    require(entry!=icons.end(),"Combined sidebar lost a shared-cameo faction variant");
    const auto* requested=definitions.sidebarUnit(entry->id,allied?"ALLIED":"SOVIET");
    require(requested && requested->id==(infantry?(allied?"ENGINEER":"SENGINEER"):(allied?"AMCV":"SMCV")),
            "Combined sidebar resolved a shared cameo to the wrong unit");
    ra2::World world(definitions);
    const auto a=world.spawn(infantry?"GAPILE":"GAWEAP",{}),s=world.spawn(infantry?"NAHAND":"NAWEAP",{8,0});
    ra2::EntityId factory=0;
    for(const auto& entity:world.snapshot())if(definitions.sidebarFactoryMatches(definitions.unit(entity.type),*requested,allied?"ALLIED":"SOVIET"))factory=entity.id;
    require(factory && factory==(allied?a:s),"Mixed sidebar selected the other faction's production building");
    world.enqueue({1,0,1,ra2::CommandKind::Produce,factory,0,{},requested->id});
    for(int i=0;i<360;++i)world.step();
    bool produced=false;for(const auto& entity:world.snapshot())produced|=entity.type==requested->id && !entity.exitFactory;
    require(produced,"Mixed sidebar request failed to produce/exit the selected unit");
  }
}
}

int main(int argc, char** argv) {
  try {
    require(argc == 2, "Expected settings path");
    const auto definitions = fastProductionFixture(argv[1]);
    sidebarPowerTransitions();
    mixedSidebarProduction(definitions);
    // Golden placement examples: every rectangular Foundation starts at the
    // specified cell; dimensions never change the common SHP drawing origin.
    for (const auto size : {FoundationSize{1,1}, {2,2}, {3,2}, {2,3}, {3,3}, {4,3}, {4,4}, {5,3}, {1,2}, {3,4}}) {
      const BuildingPlacement placement{{7,-4},size.width,size.height};
      const auto cells = foundationTiles(placement);
      require(cells.size() == static_cast<std::size_t>(size.width*size.height), "Foundation area changed");
      MapGrid map(64,64);
      map.setOccupied(placement,true);
      for (int y=-5;y<=-4+size.height;++y) for (int x=6;x<=7+size.width;++x) {
        const bool inside = x>=7 && x<7+size.width && y>=-4 && y<-4+size.height;
        require(map.cell({x,y}).occupied == inside, "Foundation axes transposed or start shifted");
      }
      for (const auto cell : cells) require(map.cell(cell).occupied, "Rendering and map occupancy disagree");
      const auto center = buildingDrawCenter(placement,{100,200},60,30);
      require(center.x==430 && center.y==230, "Foundation size moved the SHP draw center");
      const auto depth = buildingLogicalDepth(placement,60,30);
      for (int i=0;i<depth.footprintCount;++i)
        require(depth.logicalAnchorX+depth.footprintOffsets[i*2]==cells[i].x &&
                depth.logicalAnchorY+depth.footprintOffsets[i*2+1]==cells[i].y,
                "Depth footprint uses a different origin from placement");
    }
    const auto center = buildingDrawCenter({{0,0},2,2},{640,360},60,30);
    const auto body = buildingSpriteTopLeft(center,142,110);
    const auto overlay = buildingSpriteTopLeft(center,81,75,{-7,12});
    require(body.x==569 && body.y==290, "Power plant disagrees with the documented upper-vertex reference");
    require(overlay.x==593 && overlay.y==320, "Layer lost its own integer canvas center or configured offset");
    require(buildingDrawCenter({{0,0},2,2},{640,360},120,60).y==330,
            "Draw center still contains a fixed 15-pixel offset");
    ra2::World blockedDeployment(definitions);
    const auto blockedMcv = blockedDeployment.spawn("AMCV",{});
    const auto cornerBlocker = blockedDeployment.spawn("HTNK",{-1,-1});
    blockedDeployment.enqueue({0,0,1,ra2::CommandKind::Deploy,blockedMcv});
    for (int i=0;i<150;++i) blockedDeployment.step();
    require(blockedDeployment.structureAt({0,0})==0 && blockedDeployment.occupant({-1,-1})==cornerBlocker &&
            ra2::makeWorldView(blockedDeployment,definitions,blockedMcv).vehicles.size()==2,
            "Deployment ignored the northwest corner after changing anchor conventions");
    // UI dimensions must not be derived only from the simulation's real Strength.
    for (const auto& [width,height,count] : {std::tuple{1,1,8}, {2,2,15}, {3,2,15}, {3,3,22}, {4,2,22}, {4,4,30}}) {
      const auto full = ra2::buildingHealthPips(width,height,800,800);
      require(full.total == count && full.filled == count,"Building health bar lost its footprint-dependent width");
      require(ra2::buildingHealthPips(width,height,400,800).filled == count/2,"Building health pips do not show the real HP ratio");
      require(ra2::buildingHealthPips(width,height,1,800).filled == 1,"Living building lost its last health pip");
      require(ra2::buildingHealthPips(width,height,0,800).filled == 0,"Destroyed building retained a health pip");
    }
    require(ra2::buildingHealthPips(2,2,1000,800).filled == 15 && ra2::buildingHealthPips(2,2,0,0).total == 0,
            "Building health display did not bound invalid values");
    for (const auto* mcvType : {"AMCV","SMCV"}) {
      ra2::World deployment(definitions);
      const auto id = deployment.spawn(mcvType,{});
      deployment.enqueue({1,0,1,ra2::CommandKind::Deploy,id});
      deployment.step();
      require(ra2::makeWorldView(deployment,definitions,id).vehicles.size()==1,"MCV disappeared before turning to deploy");
      for (int i=0;i<100 && ra2::makeWorldView(deployment,definitions,id).buildings.empty();++i) deployment.step();
      auto visual = ra2::makeWorldView(deployment,definitions,id);
      require(visual.buildings.size()==1 && visual.vehicles.empty(),"MCV did not become its target building");
      const auto start = visual.buildings[0];
      require(start.placement.topLeft == TileCoord{-1,-1}, "MCV deployment did not convert center to Foundation start");
      require(deployment.structureAt({-1,-1})==id && deployment.structureAt({2,2})==id,
              "Deployed yard does not occupy its full 4x4 footprint");
      require(start.entityId==id && start.assetId==definitions.unit(mcvType).deploysInto && start.state==BuildingState::Constructing,
              "Deployment skipped the target building's construction animation");
      require(start.hp==definitions.unit(start.assetId).strength && start.maxHp==start.hp,"UI bar sizing changed gameplay health");
      require(buildingBuildupFrameIndex(start,30,ra2::gameMilliseconds(deployment.tick()))==0,"Deployment did not start at its first SHP frame");
      for(int i=0;i<definitions.profile().buildupFrames/2;++i) deployment.step();
      visual = ra2::makeWorldView(deployment,definitions,id);
      require(visual.buildings[0].state==BuildingState::Constructing &&
              buildingBuildupFrameIndex(visual.buildings[0],30,ra2::gameMilliseconds(deployment.tick()))==15,
              "Deployment skipped or stalled in the middle of its SHP sequence");
      for(int i=definitions.profile().buildupFrames/2;i<definitions.profile().buildupFrames;++i) deployment.step();
      require(ra2::makeWorldView(deployment,definitions,id).buildings[0].state==BuildingState::Complete,"Deployment never completed");
      // The roof lies outside the ground footprint. Picking its visible pixels
      // must retain the deployed vehicle identity so the UI can issue Undeploy.
      const auto root=std::filesystem::path(argv[1]).parent_path();
      const bool allied=std::string_view(mcvType)=="AMCV";
      const auto shp=ShpTsFile::load(root/"buildings"/(allied?"allied/gacnst/gacnst.shp":"soviet/nacnst/nacnst.shp"));
      BuildingAsset asset;asset.id=start.assetId;asset.foundation={4,4};asset.completeFrame=shp.decodeFrame(0);
      asset.completeTexture.width=asset.completeFrame.width;asset.completeTexture.height=asset.completeFrame.height;
      asset.completeTexture.rgba=shp.decodeFrameRgba(0,Palette::load(root/"palettes/ui/palette.pal"));
      BuildingAssetMap assets;assets.emplace(asset.id,std::move(asset));
      visual=ra2::makeWorldView(deployment,definitions,id);
      const auto& frame=assets.at(start.assetId).completeFrame;
      const Vec2 origin{640,360};const auto drawCenter=buildingDrawCenter(start.placement,origin,60,30);
      const auto topLeft=buildingSpriteTopLeft(drawCenter,frame.width,frame.height);
      std::optional<std::size_t> roofHit;
      for(int y=0;y<frame.height && !roofHit;++y)for(int x=0;x<frame.width && !roofHit;++x)if(frame.indices[y*frame.width+x]) {
        const Vec2 point{topLeft.x+x+.5f,topLeft.y+y+.5f};const auto tile=screenToIso(point.x,point.y,origin,60,30);
        if(tile.x>=-1 && tile.x<=2 && tile.y>=-1 && tile.y<=2)continue;
        roofHit=hitTestBuildingAtPoint(assets,visual.buildings,point.x,point.y,origin,60,30);
        require(roofHit==0,"Construction yard roof cannot be selected outside its footprint");
      }
      require(roofHit.has_value(),"Construction yard fixture has no roof pixels outside its footprint");
      require(!hitTestBuildingAtPoint(assets,visual.buildings,topLeft.x,topLeft.y,origin,60,30),"Transparent building canvas intercepted a click");
      ra2::UnitSelection selected;selected.selectOne(visual.buildings[*roofHit].entityId);std::uint64_t sequence=1;
      require(ra2::issueSelectionCommand(deployment,definitions,selected,ra2::CommandKind::Deploy,{},0,sequence)==1,"Selected construction yard rejected packing");
      deployment.step();
      visual = ra2::makeWorldView(deployment,definitions,id);
      require(visual.buildings[0].state==BuildingState::Packing &&
              buildingBuildupFrameIndex(visual.buildings[0],30,ra2::gameMilliseconds(deployment.tick()))==29,
              "Construction yard did not start packing from its last SHP frame");
      for(int i=0;i<definitions.profile().deployFrames;++i) deployment.step();
      visual = ra2::makeWorldView(deployment,definitions,id);
      require(visual.buildings.empty() && visual.vehicles.size()==1 && visual.vehicles[0].entityId==id && visual.vehicles[0].typeId==mcvType,
              "Packing lost the selected MCV identity or type");
      require(deployment.occupant({0,0})==id && deployment.structureAt({-1,-1})==0,
              "Packing moved the MCV or leaked the northwest Foundation cell");
    }
    // Low game speeds must interpolate through the short arc across zero.
    ra2::World turning(definitions);
    const auto turningTank = turning.spawn("HTNK", {});
    turning.enqueue({0,0,1,ra2::CommandKind::AttackGround,turningTank,0,{3,-3}});
    turning.advance(41667); // Begin rotation, then interpolate half of its first advancing frame.
    const auto facing = ra2::makeWorldView(turning,definitions,0).vehicles[0].turretHeadingRadians;
    require(facing < 0 && facing > -0.2f, "Turret interpolated the long way around zero or skipped interpolation");

    ra2::World firing(definitions);
    const auto firingTank = firing.spawn("HTNK", {});
    firing.enqueue({0,0,1,ra2::CommandKind::AttackGround,firingTank,0,{3,0}});
    firing.advance(25000);
    auto firingView = ra2::makeWorldView(firing,definitions,0);
    require(firingView.projectiles.size() == 1 && firingView.projectiles[0].elapsedSeconds == 0,
            "New projectile was presented ahead of its first game frame");
    firing.advance(16667);
    firingView = ra2::makeWorldView(firing,definitions,0);
    require(firingView.projectiles[0].elapsedSeconds > 0.032f && firingView.projectiles[0].elapsedSeconds < 0.034f,
            "Projectile display clock disagrees with movement interpolation");
    firing.setGameSpeed({ra2::GameSpeedMode::Singleplayer,6}); firing.advance(0,1);
    firingView = ra2::makeWorldView(firing,definitions,0);
    require(std::fabs(firingView.projectiles[0].elapsedSeconds - 2.0f/15) < 0.00001f,
            "Unlimited mode presented a future projectile frame");

    firing.setGameSpeed({ra2::GameSpeedMode::Multiplayer,6});
    require(ra2::makeWorldView(firing,definitions,0).projectiles[0].elapsedSeconds == firingView.projectiles[0].elapsedSeconds,
            "Returning from unlimited mode rewound an in-flight projectile");
    firing.advance(16667);
    require(ra2::makeWorldView(firing,definitions,0).projectiles[0].elapsedSeconds > firingView.projectiles[0].elapsedSeconds,
            "Projectile interpolation did not resume after the next game frame");

    ra2::World constructing(definitions);
    constructing.enqueue({0,0,1,ra2::CommandKind::PlaceBuilding,0,0,{},"GAPILE"});
    constructing.step();
    auto buildup = ra2::makeWorldView(constructing,definitions,0).buildings.at(0);
    require(buildup.stateDurationMs == ra2::gameMilliseconds(definitions.profile().buildupFrames),
            "Building duration still uses wall-clock FPS");
    const auto middle = buildup.stateStartTicks + buildup.stateDurationMs / 2;
    require(buildingBuildupFrameIndex(buildup,10,buildup.stateStartTicks) == 0 &&
            buildingBuildupFrameIndex(buildup,10,middle) == 5 &&
            buildingBuildupFrameIndex(buildup,10,buildup.stateStartTicks+buildup.stateDurationMs) == 9,
            "Building animation does not follow construction progress");
    constructing.setGameSpeed({ra2::GameSpeedMode::Multiplayer,0});
    const auto afterSwitch = ra2::makeWorldView(constructing,definitions,0).buildings.at(0);
    require(afterSwitch.stateStartTicks == buildup.stateStartTicks && afterSwitch.stateDurationMs == buildup.stateDurationMs,
            "Speed switch reset or rescaled construction animation");
    buildup.state = BuildingState::Packing;
    require(buildingBuildupFrameIndex(buildup,10,middle) == 4 &&
            buildingBuildupFrameIndex(buildup,10,buildup.stateStartTicks+buildup.stateDurationMs) == 0,
            "Packing animation did not reverse the shared construction clock");

    ra2::World world(definitions);
    const auto factory = world.spawn("NAWEAP", {-2,-1});
    world.enqueue({0,0,1,ra2::CommandKind::Produce,factory,0,{},"HTNK"});
    for (int i = 0; i < definitions.profile().productionFramesOverride; ++i) world.step();
    auto presentation = ra2::makeWorldView(world,definitions,0);
    require(presentation.buildings.size() == 1 && presentation.vehicles.size() == 1, "Factory presentation lost entities");
    require(presentation.buildings[0].factoryExitActive && ra2::isFactoryExitVehicle(presentation.vehicles[0],presentation.buildings[0]),
            "Simulation-owned factory link was not presented");
    auto nearby = presentation.vehicles[0]; nearby.exitFactoryId = 0;
    require(!ra2::isFactoryExitVehicle(nearby,presentation.buildings[0]), "Geometry alone selected production rendering");
    auto anotherFactory = presentation.buildings[0]; ++anotherFactory.entityId;
    require(!ra2::isFactoryExitVehicle(presentation.vehicles[0],anotherFactory), "Vehicle rendered under an unrelated factory");
    BuildingAsset::RenderLayer layer;
    layer.textures.resize(3); layer.frameDurationMs = 100;
    layer.role = BuildingAsset::LayerRole::ProductionOverUnit;
    const auto& building = presentation.buildings[0];
    require(buildingLayerTexture(layer,building,building.factoryCycleStartTicks) == &layer.textures[0] &&
            buildingLayerTexture(layer,building,building.factoryCycleStartTicks + 100) == &layer.textures[1],
            "Production layer did not start its own animation cycle");
    layer.role = BuildingAsset::LayerRole::OverUnit;
    require(buildingLayerTexture(layer,building,building.stateStartTicks + 200) == &layer.textures[2], "Normal layer lost its building animation clock");
    for (int i = 0; i < 300; ++i) world.step();
    presentation = ra2::makeWorldView(world,definitions,0);
    require(!presentation.buildings[0].factoryExitActive && !ra2::isFactoryExitVehicle(presentation.vehicles[0],presentation.buildings[0]),
            "Completed exit retained production rendering");

    ra2::World animated(definitions);
    animated.spawn("GAPOWR",{});
    BuildingAsset::RenderLayer ruleLayer;
    ruleLayer.textures.resize(9);
    ruleLayer.gameFrameDelay = ra2::compat::animationDelay(220);
    ruleLayer.normalized = true;
    for (int i=0;i<7;++i) animated.step();
    auto animatedBuilding = ra2::makeWorldView(animated,definitions,0).buildings[0];
    require(buildingLayerTexture(ruleLayer,animatedBuilding,0) == &ruleLayer.textures[1],
            "Normalized Rate=220 did not use seven game frames at GS6");
    animated.setGameSpeed({ra2::GameSpeedMode::Multiplayer,0});
    animatedBuilding = ra2::makeWorldView(animated,definitions,0).buildings[0];
    require(buildingLayerTexture(ruleLayer,animatedBuilding,0) == &ruleLayer.textures[1],
            "Speed change restarted or jumped an already running animation");
    animated.spawn("GAPOWR",{10,10});
    for (int i=0;i<3;++i) animated.step();
    animatedBuilding = ra2::makeWorldView(animated,definitions,0).buildings[1];
    require(buildingLayerTexture(ruleLayer,animatedBuilding,0) == &ruleLayer.textures[1],
            "New animation did not capture the selected speed");
    ruleLayer.gameFrameDelay = 0;
    require(buildingLayerTexture(ruleLayer,animatedBuilding,0) == &ruleLayer.textures[0],
            "Rate=0 animation did not freeze");

    std::vector<ProjectileInstance> projectiles(1);
    projectiles[0].durationSeconds = 0.3f;
    projectiles[0].elapsedSeconds = 0.2f;
    std::vector<CombatEffectInstance> effects(1);
    effects[0].lifetimeSeconds = 1.0f;
    updateWeaponProjectilesAndEffects(projectiles, effects, 0.15f);
    require(projectiles.empty() && effects.size() == 2, "Projectile did not create explosion");
    require(std::fabs(effects[0].elapsedSeconds - 0.15f) < 0.0001f, "Existing effect clock failed");
    require(std::fabs(effects[1].elapsedSeconds - 0.05f) < 0.0001f,
            "New explosion aged by the full frame instead of time since impact");
    projectiles.resize(1);
    projectiles[0].durationSeconds = 0.4f;
    effects.clear();
    updateWeaponProjectilesAndEffects(projectiles, effects, 0.4f);
    require(effects.size() == 1 && effects[0].elapsedSeconds == 0.0f,
            "Explosion disappeared on a long frame landing exactly at impact");
    updateWeaponProjectilesAndEffects(projectiles, effects, -1.0f);
    require(effects[0].elapsedSeconds == 0.0f, "Negative time moved effects backwards");
    updateWeaponProjectilesAndEffects(projectiles, effects, 1.0f);
    require(effects.empty(), "Expired effect retained");

    MapGrid map(64, 64);
    RhinoUnitState unit;
    initializeRhinoUnit(unit, map, {0, 0});
    map.setOccupied(std::vector<TileCoord>{{3, 0}}, true);
    require(issueRhinoMoveCommand(unit, map, {3, 0}, 0), "Move near occupied target rejected");
    require(unit.moveTarget.x != 3 || unit.moveTarget.y != 0, "Unit entered occupied target");
    for (int i = 0; i < 600 && !unit.path.empty(); ++i) updateRhinoUnit(unit, map, 1.0f / 60.0f);
    require(unit.path.empty(), "Unit failed to finish planned movement");
    require(map.cell({3, 0}).occupied, "Movement cleared another object's occupancy");
    require(map.cell(unit.occupiedCell).occupied && !map.cell({0, 0}).occupied,
            "Movement did not transfer occupancy");
    std::cout << "Gameplay regression cases passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
