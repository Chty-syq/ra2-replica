#include "flight.h"
#include <algorithm>
namespace ra2::flight {
Advance advance(State& state,const FlightDef& type,motion::Vector current,motion::Vector target,
                int heading,int requiredHeight,bool travel) {
  state.previousHeight=state.height;
  state.height+=std::clamp(requiredHeight-state.height,-type.climb,type.climb);
  if(state.height>=requiredHeight)state.takingOff=false;
  if(!travel || state.height<requiredHeight) {state.speed=0;return {current,heading,false};}
  const auto delta=target-current;const auto length=motion::length(delta);
  if(!length) {state.speed=0;return {target,heading,true};}
  const auto desired=motion::direction(delta.x,delta.y),turn=motion::angleDelta(heading,desired);
  heading=(heading+std::clamp(turn,-type.turnRate*256,type.turnRate*256)+65536)%65536;
  if(std::abs(motion::angleDelta(heading,desired))>512) {state.speed=0;return {current,heading,false};}
  state.speed=std::min(type.speed,state.speed+type.acceleration);
  const auto total=state.speed*1000+state.remainder,step=std::min(length,total/256);state.remainder=total%256;
  if(step>=length)return {target,heading,true};
  return {{current.x+delta.x*step/length,current.y+delta.y*step/length,0},heading,false};
}
}
