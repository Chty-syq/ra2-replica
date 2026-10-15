#include "kinematics.h"
#include <algorithm>
#include <cstdlib>
#include <limits>

namespace ra2::motion {
Vector operator+(Vector a, Vector b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Vector operator-(Vector a, Vector b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
std::int64_t sqrt(std::int64_t value) {
  std::uint64_t n = static_cast<std::uint64_t>(std::max<std::int64_t>(0,value)), result = 0, bit = 1ULL << 62;
  while (bit > n) bit >>= 2;
  while (bit) { if (n >= result+bit) { n -= result+bit; result = (result>>1)+bit; } else result >>= 1; bit >>= 2; }
  return static_cast<std::int64_t>(result);
}
std::int64_t length(Vector v) { return sqrt(v.x*v.x+v.y*v.y+v.z*v.z); }
int angleDelta(int from, int to) { return ((to-from+98304)%65536)-32768; }
namespace {
constexpr std::array angles{8192,4836,2555,1297,651,326,163,81,41,20,10,5,3,1,1};
Vector lerp(Vector a, Vector b, std::int64_t t, std::int64_t denominator) {
  return {a.x+(b.x-a.x)*t/denominator,a.y+(b.y-a.y)*t/denominator,a.z+(b.z-a.z)*t/denominator};
}
}
int direction(std::int64_t x, std::int64_t y) {
  if (!y) return x < 0 ? 32768 : 0;
  if (!x) return y < 0 ? 49152 : 16384;
  if (std::abs(x)==std::abs(y)) return x>0 ? (y>0?8192:57344) : (y>0?24576:40960);
  int angle = 0;
  if (x < 0) { x=-x; y=-y; angle=32768; }
  x *= 65536; y *= 65536;
  for (std::size_t i=0;i<angles.size();++i) {
    const auto oldX=x;
    if (y>0) { x+=y>>i; y-=oldX>>i; angle+=angles[i]; }
    else { x-=y>>i; y+=oldX>>i; angle-=angles[i]; }
  }
  return (angle+65536)%65536;
}
Vector unitVector(int angle) {
  angle=(angle+65536)%65536;
  if (!(angle%16384)) {
    constexpr std::array<Vector,4> axes{{{65536,0,0},{0,65536,0},{-65536,0,0},{0,-65536,0}}};
    return axes[angle/16384];
  }
  int sign=1;
  if (angle>32768) angle-=65536;
  if (angle>16384) { angle-=32768; sign=-1; }
  if (angle<-16384) { angle+=32768; sign=-1; }
  std::int64_t x=652032874,y=0;
  for (std::size_t i=0;i<angles.size();++i) {
    const auto oldX=x;
    if (angle>=0) { x-=y>>i; y+=oldX>>i; angle-=angles[i]; }
    else { x+=y>>i; y-=oldX>>i; angle+=angles[i]; }
  }
  return {sign*x/16384,sign*y/16384,0};
}
Vector rotateFlh(int facing, const std::array<int,3>& flh) {
  const auto v=unitVector(facing);
  return {(v.x*flh[0]-v.y*flh[1])*lepton/65536,
          (v.y*flh[0]+v.x*flh[1])*lepton/65536,flh[2]*lepton};
}
Vector surfaceFlh(int heading, int facing, const std::array<int,3>& flh, int slopeX, int slopeY) {
  if (!slopeX && !slopeY) return rotateFlh(facing,flh);
  const auto forward=unitVector(heading);
  const auto pitch=(slopeX*forward.x+slopeY*forward.y)/65536;
  const auto roll=(slopeY*forward.x-slopeX*forward.y)/65536;
  const auto normalized=[](std::int64_t slope) {
    const auto norm=motion::sqrt((256*256+slope*slope)*65536*65536);
    return Vector{256LL*65536*65536/norm,slope*65536*65536/norm,0};
  };
  const auto p=normalized(pitch),r=normalized(roll);
  const auto local=rotateFlh(facing-heading,flh);
  const auto x=(local.x*p.x-local.z*p.y)/65536;
  const auto raised=(local.x*p.y+local.z*p.x)/65536;
  const auto y=(local.y*r.x-raised*r.y)/65536;
  const auto z=(local.y*r.y+raised*r.x)/65536;
  return {(x*forward.x-y*forward.y)/65536,(x*forward.y+y*forward.x)/65536,z};
}
void DriveSegment::build(Vector from, Vector to) {
  startHeading=endHeading=direction(to.x-from.x,to.y-from.y);
  headings.fill(startHeading);
  points[0]=from; distances[0]=0;
  for (int i=1;i<=samples+1;++i) {
    points[i]=lerp(from,to,i,samples+1);
    distances[i]=distances[i-1]+motion::length(points[i]-points[i-1]);
  }
}
void DriveSegment::buildCorner(Vector from, Vector corner, Vector after) {
  const auto incoming=corner-from,outgoing=after-corner;
  const auto inSpan=std::max(std::abs(incoming.x),std::abs(incoming.y));
  const auto outSpan=std::max(std::abs(outgoing.x),std::abs(outgoing.y));
  if (!inSpan || !outSpan) { build(from,corner); return; }
  // Turn inside the corner instead of forcing an S curve through its centre.
  // Both trimmed points stay within the corner cell; endpoints on straight
  // paths and final destinations remain exact. Snap the exit to subcells so
  // the next segment starts on precisely the same outgoing line.
  const auto trim=std::min<std::int64_t>({350*scale,inSpan,outSpan/2});
  const Vector entry{corner.x-incoming.x*trim/inSpan,corner.y-incoming.y*trim/inSpan,corner.z};
  Vector exit{corner.x+outgoing.x*trim/outSpan,corner.y+outgoing.y*trim/outSpan,corner.z};
  exit.x=exit.x/scale*scale; exit.y=exit.y/scale*scale;
  startHeading=direction(incoming.x,incoming.y); endHeading=direction(outgoing.x,outgoing.y);
  points[0]=from; distances[0]=0; headings[0]=startHeading;
  for (int i=0;i<=samples;++i) {
    points[i+1]=lerp(lerp(entry,corner,i,samples),lerp(corner,exit,i,samples),i,samples);
    distances[i+1]=distances[i]+motion::length(points[i+1]-points[i]);
    const auto tangent=lerp(corner-entry,exit-corner,i,samples);
    headings[i+1]=direction(tangent.x,tangent.y);
  }
  headings[1]=startHeading; headings.back()=endHeading;
}
Vector DriveSegment::at(std::int64_t distance) const {
  if (distance<=0) return points.front();
  if (distance>=length()) return points.back();
  const auto i=static_cast<std::size_t>(std::upper_bound(distances.begin(),distances.end(),distance)-distances.begin());
  return lerp(points[i-1],points[i],distance-distances[i-1],distances[i]-distances[i-1]);
}
int DriveSegment::heading(std::int64_t distance) const {
  if (distance<=0) return startHeading;
  if (distance>=length()) return endHeading;
  const auto i=static_cast<std::size_t>(std::upper_bound(distances.begin(),distances.end(),distance)-distances.begin());
  return static_cast<int>((headings[i-1]+angleDelta(headings[i-1],headings[i])*(distance-distances[i-1])/
                           (distances[i]-distances[i-1])+65536)%65536);
}
std::optional<Launch> ballisticLaunch(Vector delta, int rangeLeptons, int gravityLeptons) {
  // Far-away or teleported targets must not overflow fixed-point distance
  // squares. This bound exceeds all supported ranges plus FLH offsets.
  constexpr std::int64_t limit=1000000000;
  if (gravityLeptons<=0 || rangeLeptons<=0 ||
      delta.x < -limit || delta.x > limit || delta.y < -limit || delta.y > limit ||
      delta.z < -limit || delta.z > limit) return std::nullopt;
  const auto gravity=gravityLeptons*lepton;
  const auto speed=std::min(sqrt(static_cast<std::int64_t>(rangeLeptons)*gravityLeptons*6/5)*lepton,
                            std::max(lepton,length(delta)/2));
  const auto velocity=[&](int t) { return Vector{delta.x*256/t,delta.y*256/t,delta.z*256/t+gravity*(t-256)/512}; };
  const auto feasible=[&](int t) { return length(velocity(t))<=speed; };
  int previous=std::max(256,static_cast<int>(length(delta)*256/std::max<std::int64_t>(1,speed)));
  if (feasible(previous)) return Launch{velocity(previous),previous};
  for (int t=previous+256;t<=4096*256;t+=256) {
    if (feasible(t)) {
      int low=previous,high=t;
      while (high-low>1) { const int mid=(low+high)/2; if (feasible(mid)) high=mid; else low=mid; }
      return Launch{velocity(high),high};
    }
    // Beyond the minimum-speed arc, the required speed only increases.
    if (velocity(t).z>speed) break;
    previous=t;
  }
  return std::nullopt;
}
}
