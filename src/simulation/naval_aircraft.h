#pragma once
#include "kinematics.h"
namespace ra2::naval_aircraft {
struct State {
  motion::Vector position{},previous{};
  int heading=0,speedMilli=0;
};
// Ships own sortie decisions. This deterministic geometry only flies toward a
// point, preserving aircraft turn rates and vertical takeoff/landing progress.
bool advance(State&,motion::Vector target,int rawSpeed,int rawRot,int altitude,int arrivalRadiusLeptons);
}
