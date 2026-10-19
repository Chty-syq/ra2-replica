#pragma once
#include "kinematics.h"
#include "game_clock.h"
#include <cstdint>

namespace ra2::leap {
enum class Phase { Ground, Flying, Infesting, Landing };
struct State {
  Phase phase=Phase::Ground;
  std::uint64_t victim=0;
  motion::Vector start{},position{},previous{};
  GameFrame started=0,duration=1,nextDamage=0,suppressedUntil=0;
  bool removed=false;
};
void launch(State&,motion::Vector start,motion::Vector target,int speed,GameFrame now);
// Fixed-point homing along the ground plane, with a bounded visual jump arc.
// The native bullet locomotor still needs a frame trace for exact calibration.
bool advance(State&,motion::Vector target,int speed,GameFrame now);
}
