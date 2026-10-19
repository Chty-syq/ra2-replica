#include "naval_aircraft.h"
#include "compat/rules_timing.h"
#include <algorithm>
namespace ra2::naval_aircraft {
bool advance(State& s,motion::Vector target,int rawSpeed,int rawRot,int altitude,int radius) {
 s.previous=s.position;
 const auto dz=std::int64_t(altitude)*motion::lepton-s.position.z;
 s.position.z+=std::clamp(dz,-20*motion::lepton,20*motion::lepton);
 const auto delta=target-s.position;const auto distance=motion::length({delta.x,delta.y,0});
 if(distance<=std::int64_t(radius)*motion::lepton)return true;
 if(s.position.z<std::int64_t(altitude-20)*motion::lepton)return false;
 const int desired=motion::direction(delta.x,delta.y);
 s.heading=(s.heading+std::clamp(motion::angleDelta(s.heading,desired),-std::max(1,rawRot)*256,std::max(1,rawRot)*256)+65536)%65536;
 const int landingCap=radius<=64?std::max(2,int(distance/motion::lepton)*std::max(1,rawRot)/64):255;
 s.speedMilli=std::min(std::min(compat::movementLeptons(rawSpeed),landingCap)*1000,s.speedMilli+1000);
 const auto step=std::min(distance,std::int64_t(s.speedMilli)*motion::lepton/1000);const auto direction=motion::unitVector(s.heading);
 s.position.x+=direction.x*step/65536;s.position.y+=direction.y*step/65536;
 return false;
}
}
