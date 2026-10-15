#include "simulation/world.h"
#include "test_support.h"
#include "simulation_fixture.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>

using namespace ra2;
namespace {
void clockTests() {
  constexpr std::array mp{10,12,15,20,30,45,60};
  constexpr std::array sp{10,12,15,20,30,60,0};
  for (const auto mode : {GameSpeedMode::Multiplayer, GameSpeedMode::Singleplayer}) for (int level = 0; level <= 6; ++level) {
    const auto rate = (mode == GameSpeedMode::Multiplayer ? mp : sp)[level];
    require(targetGameFrameRate({mode,level}).value_or(0) == rate, "Wrong original speed table");
    if (!rate) continue;
    for (const auto fps : {30,60,144}) {
      GameClock clock({mode,level});
      std::uint64_t frames = 0;
      for (int i = 0; i < fps * 10; ++i)
        frames += clock.advance(static_cast<std::uint64_t>(i+1)*1000000/fps - static_cast<std::uint64_t>(i)*1000000/fps);
      require(frames == static_cast<std::uint64_t>(rate)*10 && clock.pendingFrames() == 0 && clock.interpolation() == 0,
              "Finite speed drifted across render intervals");
    }
  }
  GameClock clock({GameSpeedMode::Multiplayer,4});
  require(clock.advance(10000) == 0, "Fractional frame ran too early"); // 0.3 frames
  clock.setSpeed({GameSpeedMode::Multiplayer,6});
  require(clock.advance(10000) == 0 && std::abs(clock.interpolation()-0.9) < 1e-9, "Speed switch discarded fractional credit");
  require(clock.advance(1667) == 1, "Fractional frame failed to complete after speed switch");
  GameClock stalled;
  require(stalled.advance(10000000) == 120 && stalled.pendingFrames() == 480, "Long frame lost backlog or exceeded budget");
  stalled.setSpeed({GameSpeedMode::Multiplayer,0});
  require(stalled.advance(0) == 120 && stalled.pendingFrames() == 360, "Speed switch rescaled earned backlog");
  for (int i = 0; i < 3; ++i) require(stalled.advance(0) == 120, "Backlog drain failed");
  require(stalled.advance(100000) == 1, "GS0 was treated as pause");
  GameClock unlimited({GameSpeedMode::Singleplayer,6});
  require(unlimited.advance(0,7) == 7 && unlimited.advance(10000000,13) == 13, "Unlimited mode has a hidden real-time cap");
  require(unlimited.interpolation() == 1 && unlimited.pendingFrames() == 0, "Unlimited mode invented timing debt");
  unlimited.setSpeed({GameSpeedMode::Multiplayer,6});
  require(unlimited.advance(0) == 0 && unlimited.advance(1000000) == 60, "Leaving unlimited mode caused a catch-up burst");
  const auto previous = clock.speed();
  requireRejected<std::invalid_argument>([&] { clock.setSpeed({GameSpeedMode::Multiplayer,7}); }, "Invalid speed accepted");
  require(clock.speed() == previous, "Invalid speed mutated the clock");
  requireRejected<std::invalid_argument>([] { (void)targetGameFrameRate({static_cast<GameSpeedMode>(99),3}); }, "Invalid mode accepted");
  requireRejected<std::out_of_range>([&] { clock.advance(std::numeric_limits<std::uint64_t>::max()); }, "Clock overflow accepted");
  require(gameMilliseconds(15) == 1000 && gameMilliseconds(900) == 60000, "Reference game seconds depend on speed");
}
std::vector<Tick> firingFrames(World& world, int frames) {
  std::set<std::uint64_t> seen;
  std::vector<Tick> fired;
  for (int i = 0; i < frames; ++i) {
    world.step();
    for (const auto& shot : world.shots()) if (seen.insert(shot.id).second) fired.push_back(shot.started);
  }
  return fired;
}
void weaponTests(const DefinitionRegistry& defs) {
  for (const auto* type : {"HTNK","MTNK"}) {
    World world(defs);
    const auto id = world.spawn(type,{});
    world.enqueue({0,0,1,CommandKind::AttackGround,id,0,{3,0}});
    const auto fired = firingFrames(world,200);
    const auto rof = defs.weapon(defs.unit(type).primary).rof;
    require(fired.size() >= 3 && fired[1]-fired[0] == static_cast<Tick>(rof) && fired[2]-fired[1] == static_cast<Tick>(rof),
            "ROF was scaled by wall-clock frequency");
    std::cout << type << " ROF: " << rof << " game frames, " << rof/60.0 << " seconds at GS6 multiplayer\n";
  }
  World changing(defs);
  const auto id = changing.spawn("HTNK",{});
  changing.enqueue({0,0,1,CommandKind::AttackGround,id,0,{3,0}});
  changing.advance(500000); // 30 of the first 65-frame cooldown elapsed.
  const auto hash = changing.stateHash();
  changing.setGameSpeed({GameSpeedMode::Multiplayer,2});
  require(changing.stateHash() == hash, "Speed setting changed gameplay state");
  const auto fired = firingFrames(changing,45);
  require(fired.size() == 1 && fired.front() == 66, "Speed switch reset an active weapon cooldown");
}
void replayTests(const DefinitionRegistry& defs) {
  const auto setup = [](World& world) {
    const auto tank = world.spawn("HTNK",{}), factory = world.spawn("GAWEAP",{13,14});
    world.enqueue({1,0,1,CommandKind::Move,tank,0,{10,0}});
    world.enqueue({1,0,2,CommandKind::Produce,factory,0,{},"MTNK"});
    world.enqueue({125,0,3,CommandKind::Move,3,0,{23,15}});
  };
  World reference(defs); setup(reference);
  for (int i = 0; i < 180; ++i) reference.step();
  for (const auto mode : {GameSpeedMode::Multiplayer,GameSpeedMode::Singleplayer}) for (int level = 0; level <= 6; ++level) {
    const auto rate = targetGameFrameRate({mode,level});
    if (!rate) continue;
    for (int fps : {30,60,144}) {
      World world(defs); setup(world); world.setGameSpeed({mode,level});
      const auto renderFrames = fps * 180 / *rate;
      for (int i = 0; i < renderFrames; ++i)
        world.advance(static_cast<std::uint64_t>(i+1)*1000000/fps - static_cast<std::uint64_t>(i)*1000000/fps);
      require(world.tick() == 180 && world.stateHash() == reference.stateHash(), "Movement/production/egress replay changes with speed or display rate");
    }
  }
  World unlimited(defs); setup(unlimited); unlimited.setGameSpeed({GameSpeedMode::Singleplayer,6});
  unlimited.advance(0,180);
  require(unlimited.stateHash() == reference.stateHash(), "Unlimited replay used different simulation logic");
  unlimited.setGameSpeed({GameSpeedMode::Multiplayer,6});
  for (const auto& entity : unlimited.snapshot()) require(entity.previous == entity.position, "Leaving unlimited mode rewound presentation");
  require(unlimited.stateHash() == reference.stateHash(), "Interpolation reset changed authoritative state");
  std::cout << "Speed replay hash: " << reference.stateHash() << '\n';
}
void configTests(const DefinitionRegistry& defs) {
  for (const auto* invalid : {"[GameSpeed]\nLevel=7", "[GameSpeed]\nLevel=-1", "[GameSpeed]\nMode=Unknown",
                             "[Simulation]\nSpeedSubcellsDenominator=0", "[Simulation]\nTicksPerSecond=240",
                             "[Factory.GAWEAP]\nPrepareTicks=12"}) {
    auto profile = defs.replica(); profile.overlay(IniDocument::parse(invalid,"bad-speed.ini"));
    requireRejected([&] { DefinitionRegistry::compile(defs.rules(),defs.art(),profile); },"Invalid/legacy timing configuration was silently accepted");
  }
  auto profile = defs.replica(); profile.overlay(IniDocument::parse("[GameSpeed]\nMode=Singleplayer\nLevel=5"));
  const auto alternate = DefinitionRegistry::compile(defs.rules(),defs.art(),profile);
  World configured(alternate);
  require(configured.gameSpeed() == GameSpeed{GameSpeedMode::Singleplayer,5}, "Configured startup speed ignored");
  configured.advance(1000000);
  require(configured.tick() == 60, "Configured singleplayer GS5 is not 60 game frames/sec");
}
}
int main(int argc, char** argv) {
  try {
    require(argc == 2,"Expected settings path");
    const auto defs = fastProductionFixture(argv[1]);
    clockTests(); weaponTests(defs); replayTests(defs); configTests(defs);
    std::cout << "Game speed regressions passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
