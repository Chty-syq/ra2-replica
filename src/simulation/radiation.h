#pragma once
#include "config/definitions.h"
#include <vector>
namespace ra2::radiation {
struct Site {
  TileCoord cell{};
  int spread=0,level=0;
  GameFrame started=0;
};
// One site per center and radius. Repeated deployment refreshes that site;
// separate sites overlap, with damage capped by RadLevelMax at the cell.
class Field {
public:
  void refresh(TileCoord cell,int spread,int level,GameFrame now,const RadiationDef&);
  void expire(GameFrame now,const RadiationDef&);
  int level(TileCoord cell,GameFrame now,const RadiationDef&) const;
  int light(TileCoord cell,GameFrame now,const RadiationDef&) const;
  int sourceLevel(TileCoord cell,int spread,GameFrame now,const RadiationDef&) const;
  const std::vector<Site>& sites() const {return sites_;}
private:
  int sample(TileCoord,GameFrame,const RadiationDef&,int interval) const;
  std::vector<Site> sites_;
};
}
