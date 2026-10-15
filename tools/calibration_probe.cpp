#include "simulation/world.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace ra2;
namespace {
EntitySnapshot entity(const World& world, EntityId id) {
  const auto views = world.snapshot();
  const auto found = std::find_if(views.begin(),views.end(),[&](const auto& v) { return v.id == id; });
  if (found == views.end()) throw std::runtime_error("Calibration entity disappeared");
  return *found;
}
template<class Predicate> Tick measure(World& world, Predicate done) {
  const auto start = world.tick();
  do {
    world.step();
    if (world.tick()-start > 20000) throw std::runtime_error("Calibration scenario timed out");
  } while (!done());
  return world.tick()-start;
}
void row(const char* type, const char* scenario, Tick frames) {
  std::cout << type << ',' << scenario << ',' << frames << ',' << frames/60.0 << ',' << frames/30.0 << '\n';
}
}
int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("Usage: ra2_calibration_probe <settings-directory>");
    const auto defs = DefinitionRegistry::load(argv[1]);
    std::cerr << "Configuration: " << defs.fingerprint() << '\n';
    std::cout << "type,scenario,game_frames,seconds_at_60,seconds_at_30\n";
    for (const auto* type : {"HTNK","MTNK","AMCV"}) {
      World move(defs); const auto id = move.spawn(type,{});
      move.enqueue({1,0,1,CommandKind::Move,id,0,{10,0}});
      row(type,"straight_10_cells_from_rest",measure(move,[&] { return entity(move,id).cell == TileCoord{10,0}; }));
      World build(defs); const auto factory = build.spawn(std::string_view(type)=="HTNK" ? "NAWEAP" : "GAWEAP",{});
      build.enqueue({1,0,1,CommandKind::Produce,factory,0,{},type});
      row(type,"single_factory_first_internal_spawn",measure(build,[&] { return entity(build,factory).exitingUnit != 0; }));
      if (std::string_view(type)=="AMCV") continue;
      World turn(defs); const auto tank = turn.spawn(type,{});
      turn.enqueue({1,0,1,CommandKind::AttackGround,tank,0,{0,3}});
      row(type,"turret_90_degrees_first_shot",measure(turn,[&] { return !turn.shots().empty(); }));
    }
    for (const auto* type : {"GAPOWR","GAWEAP","NAWEAP"}) {
      World building(defs); const auto id = building.spawn(type,{},0,true);
      row(type,"placed_building_becomes_complete",measure(building,[&] { return entity(building,id).activity == Activity::Idle; }));
    }
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
