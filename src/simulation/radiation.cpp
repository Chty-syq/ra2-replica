#include "radiation.h"
#include "kinematics.h"
#include <algorithm>
namespace ra2::radiation {
namespace {
int intensity(const Site& site,GameFrame now,const RadiationDef& rules,int interval) {
  if(now<site.started)return 0;
  const auto elapsed=now-site.started;const int duration=site.level*rules.durationMultiple;
  if(elapsed>=GameFrame(duration))return 0;
  const auto steps=std::max(1,duration/interval);
  return static_cast<int>(std::max<std::int64_t>(0,site.level-std::int64_t(site.level)*static_cast<std::int64_t>(elapsed/interval)/steps));
}
}
void Field::refresh(TileCoord cell,int spread,int level,GameFrame now,const RadiationDef& rules) {
  if(level<=0 || spread<=0 || spread>64000)return;
  level=std::min(level,rules.levelMax);
  for(auto& site:sites_)if(site.cell==cell && site.spread==spread){site.level=level;site.started=now;return;}
  sites_.push_back({cell,spread,level,now});
}
void Field::expire(GameFrame now,const RadiationDef& rules) {
  std::erase_if(sites_,[&](const auto& s){return now-s.started>=GameFrame(s.level)*rules.durationMultiple;});
}
int Field::sample(TileCoord cell,GameFrame now,const RadiationDef& rules,int interval) const {
  std::int64_t sum=0;
  for(const auto& site:sites_) {
    const auto value=intensity(site,now,rules,interval);if(!value)continue;
    const auto dx=(std::int64_t(cell.x)-site.cell.x)*1000,dy=(std::int64_t(cell.y)-site.cell.y)*1000;
    if(std::abs(dx)>=site.spread || std::abs(dy)>=site.spread)continue;
    const auto distance=motion::sqrt(dx*dx+dy*dy);if(distance>=site.spread)continue;
    sum+=std::int64_t(value)*(site.spread-distance)/site.spread;
  }
  return static_cast<int>(std::min<std::int64_t>(sum,rules.levelMax));
}
int Field::level(TileCoord c,GameFrame now,const RadiationDef& r) const{return sample(c,now,r,r.levelDelay);}
int Field::light(TileCoord c,GameFrame now,const RadiationDef& r) const{return sample(c,now,r,r.lightDelay);}
int Field::sourceLevel(TileCoord c,int spread,GameFrame now,const RadiationDef& r) const {
  for(const auto& site:sites_)if(site.cell==c && site.spread==spread)return intensity(site,now,r,r.levelDelay);
  return 0;
}
}
