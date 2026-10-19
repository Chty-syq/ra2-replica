#include "infantry_art.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace ra2 {
int InfantrySequence::frame(int facing,int elapsed,bool loop) const {
  if(count<=0) return -1;
  const int local=std::max(0,elapsed)/std::max(1,delay);
  if(clockwise)facing=(8-facing)%8;
  return start+(fixedFacing>=0?fixedFacing:facing)*stride+(loop?local%count:std::min(local,count-1));
}
const InfantrySequence& InfantryDef::sequence(std::string_view name) const {
  const auto found=sequences.find(canonicalId(name));
  if(found!=sequences.end() && found->second.count>0) return found->second;
  const auto ready=sequences.find("READY");
  if(ready==sequences.end()) throw std::runtime_error("Infantry is missing Ready sequence");
  return ready->second;
}
InfantryDef readInfantryArt(const IniDocument& art,std::string_view image) {
  InfantryDef result;
  result.fireUp=art.integer(image,"FireUp"); result.fireProne=art.integer(image,"FireProne");
  if(result.fireUp<0 || result.fireUp>1024 || result.fireProne<0 || result.fireProne>1024)
    art.invalid(image,"FireUp/FireProne","expected 0..1024");
  result.crawls=art.boolean(image,"Crawls",true);
  const auto section=art.string(image,"Sequence");
  constexpr std::array<const char*,8> directions{"N","NW","W","SW","S","SE","E","NE"};
  for(const auto* name:{"Ready","Guard","Walk","Prone","Crawl","FireUp","FireProne","Down","Up",
                       "Idle1","Idle2","Die1","Die2","Deploy","Deployed","DeployedFire","DeployedIdle","Undeploy","Cheer","Paradrop","Panic",
                       "Fly","Hover","FireFly","Tumble","AirDeathStart","AirDeathFalling","AirDeathFinish",
                       "Tread","Swim","WetAttack","WetIdle1","WetIdle2","WetDie1","WetDie2"}) {
    const auto values=art.list(section,name); if(values.empty()) continue;
    if(values.size()<3 || values.size()>4) art.invalid(section,name,"expected Start,Count,FacingMultiplier[,Facing]");
    InfantrySequence value;
    const auto parsed=IniDocument::parse("[S]\nStart="+values[0]+"\nCount="+values[1]+"\nStride="+values[2]);
    value.start=parsed.integer("S","Start"); value.count=parsed.integer("S","Count"); value.stride=parsed.integer("S","Stride");
    if(value.start<0 || value.count<0 || value.stride<0 || value.start>65535 || value.count>65535 || value.stride>65535)
      art.invalid(section,name,"sequence values outside 0..65535");
    if(values.size()==4) {
      const auto facing=canonicalId(values[3]);
      const auto it=std::find(directions.begin(),directions.end(),facing);
      if(it==directions.end()) art.invalid(section,name,"unknown fixed facing");
      value.fixedFacing=static_cast<int>(it-directions.begin());
    }
    const auto key=canonicalId(name);
    if(key=="WALK" || key=="SWIM" || key=="TREAD" || key=="FLY" || key=="HOVER" || key=="IDLE1" || key=="IDLE2" || key=="CHEER") value.delay=3;
    if(key=="PANIC") value.delay=4;
    value.normalized=key=="IDLE1" || key=="IDLE2" || key=="CHEER";
    result.sequences.emplace(key,value);
  }
  return result;
}
int infantryFacing(int heading) {
  const auto normalized=(heading%65536+65536)%65536;
  return (13-(normalized+4096)/8192)%8;
}
}
