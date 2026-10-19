#pragma once
#include "config/definitions.h"
#include "kinematics.h"
namespace ra2::aircraft {
struct State {
  motion::Vector position{},previous{};
  int heading=0,speedMilli=0;
};
// Physical fixed-point coordinates. Arrival decelerates only when landing or
// hovering; fighters retain forward flight and bank through a bounded turn.
bool advance(State&,const AircraftDef&,motion::Vector target,int altitude,int radiusLeptons,bool landing=false);
}
