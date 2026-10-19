#pragma once
#include "config/ini_document.h"
#include <map>
#include <string>

namespace ra2 {
struct InfantrySequence {
  int start=0, count=0, stride=0, fixedFacing=-1, delay=1;
  bool normalized=false,clockwise=false;
  int frame(int facing, int elapsed, bool loop) const;
};
struct InfantryDef {
  std::map<std::string,InfantrySequence,std::less<>> sequences;
  int fireUp=0, fireProne=0, bodyFrames=0;
  bool crawls=true, fearless=false, deployer=false, crushable=true, deployedCrushable=false;
  const InfantrySequence& sequence(std::string_view name) const;
};
InfantryDef readInfantryArt(const IniDocument& art, std::string_view image);
// Project headings are map-axis angles; SHP uses N,NW,W,SW,S,SE,E,NE.
int infantryFacing(int heading);
}
