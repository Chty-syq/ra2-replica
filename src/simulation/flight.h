#pragma once
#include "config/definitions.h"
#include "kinematics.h"
#include <deque>
namespace ra2::flight {
struct State {
  int height=0, previousHeight=0, speed=0;
  int subCell=0, destinationSlot=-1, nextSlot=-1;
  TileCoord destination{};
  std::deque<TileCoord> path;
  GameFrame retry=0;
  std::int64_t remainder=0;
  bool requested=false, takingOff=true;
};
struct Advance { motion::Vector position;int heading=0;bool arrived=false; };
// Geometry is independent of the ECS, ground pathfinding and rendering.
// Height/speed use original leptons; x/y use World's integer milli-cells.
Advance advance(State&,const FlightDef&,motion::Vector current,motion::Vector target,
                int heading,int requiredHeight,bool travel);
}
