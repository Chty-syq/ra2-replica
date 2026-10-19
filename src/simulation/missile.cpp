#include "missile.h"
#include "compat/rules_timing.h"
#include <algorithm>
namespace ra2::missile {
Advance advance(State& s,const MissileDef& d,int rawSpeed,GameFrame now) {
  s.previous=s.position;s.previousPitch=s.pitch;Advance result;
  const auto age=now-s.started;
  if(s.phase==Phase::Preparing) {
    const auto tilt=std::clamp<std::int64_t>(static_cast<std::int64_t>(age)-d.pauseFrames,0,d.tiltFrames);
    s.pitch=d.initialPitch+(d.finalPitch-d.initialPitch)*tilt/d.tiltFrames;
    if(age<GameFrame(d.pauseFrames+d.tiltFrames))return result;
    s.phase=Phase::Climbing;s.launched=true;result.takeoff=true;
    s.origin=s.position;
    // Arc-length samples keep the final descent continuous even at the five
    // cell minimum range. This integer parabola is our compatibility curve;
    // native RocketLocomotor's exact pitch integration remains unmeasured.
    for(int i=0;i<=64;++i) {
      const auto delta=s.target-s.origin;
      s.curve[i]={s.origin.x+delta.x*i/64,s.origin.y+delta.y*i/64,
        s.origin.z+delta.z*i/64+std::int64_t(d.altitude)*motion::lepton*4*i*(64-i)/(64*64)};
      if(!d.lazyCurve) {
        // The naval rocket climbs, levels at cruise altitude, then dives; V3
        // retains its existing lazy arc. Exact native pitch integration is pending.
        const int along=i<16?i*12:i<48?192+(i-16)*22:896+(i-48)*8;
        const auto z=i<16?s.origin.z+std::int64_t(d.altitude)*motion::lepton*i/16:i<48?s.origin.z+std::int64_t(d.altitude)*motion::lepton:s.target.z+(s.origin.z+std::int64_t(d.altitude)*motion::lepton-s.target.z)*(64-i)/16;
        s.curve[i]={s.origin.x+delta.x*along/1024,s.origin.y+delta.y*along/1024,z};
      }
      if(i)s.distances[i]=s.distances[i-1]+motion::length(s.curve[i]-s.curve[i-1]);
    }
  }
  s.speedMilli=std::min(compat::movementLeptons(rawSpeed)*1000,s.speedMilli+d.accelerationMilli);
  const auto travel=std::int64_t(s.speedMilli)*motion::lepton/1000;
  s.travelled=std::min(s.distances.back(),s.travelled+travel);
  int segment=1;while(segment<64 && s.distances[segment]<s.travelled)++segment;
  const auto length=std::max<std::int64_t>(1,s.distances[segment]-s.distances[segment-1]);
  const auto offset=s.travelled-s.distances[segment-1];const auto delta=s.curve[segment]-s.curve[segment-1];
  s.position={s.curve[segment-1].x+delta.x*offset/length,s.curve[segment-1].y+delta.y*offset/length,s.curve[segment-1].z+delta.z*offset/length};
  s.phase=segment<24?Phase::Climbing:segment<40?Phase::Cruising:Phase::Diving;
  const int desired=motion::angleDelta(0,motion::direction(motion::sqrt(delta.x*delta.x+delta.y*delta.y),delta.z));
  s.pitch+=std::clamp(desired-s.pitch,-d.turnRate,d.turnRate);
  if(s.travelled>=s.distances.back()){s.position=s.target;result.impact=true;}
  return result;
}
}
