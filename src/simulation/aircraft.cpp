#include "aircraft.h"
#include "compat/rules_timing.h"
#include <algorithm>
namespace ra2::aircraft {
bool advance(State& s,const AircraftDef& type,motion::Vector target,int altitude,int radius,bool landing) {
  s.previous=s.position;const auto& m=type.movement;
  const auto dz=std::int64_t(altitude)*motion::lepton-s.position.z;
  s.position.z+=std::clamp(dz,-std::int64_t(landing?m.climb:m.crash)*motion::lepton,std::int64_t(m.climb)*motion::lepton);
  const auto delta=target-s.position;const auto distance=motion::length({delta.x,delta.y,0});
  if(distance<=std::int64_t(radius)*motion::lepton && (landing || !type.fixedWing)){s.speedMilli=0;return true;}
  if(!landing && s.position.z<std::int64_t(altitude-20)*motion::lepton)return false;
  const int desired=motion::direction(delta.x,delta.y),turn=std::max(1,m.turnRate)*256;
  s.heading=(s.heading+std::clamp(motion::angleDelta(s.heading,desired),-turn,turn)+65536)%65536;
  // JumpjetSpeed is a native lepton/frame value, unlike tracked/fixed-wing
  // Speed, which is imported as a percentage of 255 leptons/frame.
  const int maximumSpeed=type.fixedWing?compat::movementLeptons(m.speed):m.speed;
  const int limit=landing?std::max(1,int(distance/motion::lepton)*m.turnRate/64):maximumSpeed;
  s.speedMilli=std::min(std::min(limit,maximumSpeed)*1000,s.speedMilli+std::max(1,m.acceleration)*100);
  const auto step=std::min(distance,std::int64_t(s.speedMilli)*motion::lepton/1000);
  // Jumpjets can slide; a fighter follows its nose, including a real turning circle.
  const auto vector=(type.fixedWing && !(landing && distance<=96*motion::lepton))?motion::unitVector(s.heading):motion::unitVector(desired);
  s.position.x+=vector.x*step/65536;s.position.y+=vector.y*step/65536;
  return type.fixedWing && distance<=std::int64_t(radius)*motion::lepton;
}
}
