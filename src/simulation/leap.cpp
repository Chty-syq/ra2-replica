#include "leap.h"
#include <algorithm>
namespace ra2::leap {
void launch(State& s,motion::Vector start,motion::Vector target,int speed,GameFrame now) {
  s.phase=Phase::Flying;s.start=s.position=s.previous=start;s.started=now;
  const auto distance=motion::length({target.x-start.x,target.y-start.y,0});
  const auto step=std::max(1,speed)*motion::lepton;
  s.duration=std::max<std::int64_t>(1,(distance+step-1)/step);
}
bool advance(State& s,motion::Vector target,int speed,GameFrame now) {
  s.previous=s.position;
  const auto delta=motion::Vector{target.x-s.position.x,target.y-s.position.y,0};
  const auto distance=motion::length(delta),step=std::int64_t(std::max(1,speed))*motion::lepton;
  if(distance<=step){s.position=target;return true;}
  s.position.x+=delta.x*step/distance;s.position.y+=delta.y*step/distance;
  const auto t=static_cast<std::int64_t>(std::clamp<GameFrame>(now-s.started,0,s.duration)),n=static_cast<std::int64_t>(s.duration);
  s.position.z=s.start.z+(target.z-s.start.z)*t/n+96*motion::lepton*4*t*(n-t)/(n*n);
  return false;
}
}
