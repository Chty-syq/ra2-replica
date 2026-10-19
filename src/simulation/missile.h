#pragma once
#include "config/definitions.h"
#include "kinematics.h"
namespace ra2::missile {
enum class Phase { Preparing,Climbing,Cruising,Diving };
struct State {
  motion::Vector position{},previous{},target{},origin{};
  std::array<motion::Vector,65> curve{};
  std::array<std::int64_t,65> distances{};
  std::int64_t travelled=0;
  GameFrame started=0;
  int heading=0,pitch=0,previousPitch=0,speedMilli=0;
  Phase phase=Phase::Preparing;
  bool launched=false;
};
struct Advance {bool takeoff=false,impact=false;};
// Fixed-point trajectory: all headings, pitches, velocities and positions are
// integers. The visual model never advances or steers the missile.
Advance advance(State&,const MissileDef&,int speed,GameFrame now);
}
