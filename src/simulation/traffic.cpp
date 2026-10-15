#include "traffic.h"
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <compare>
#include <queue>
#include <tuple>
#include <unordered_map>

namespace ra2::traffic {
FlowField::FlowField(TileCoord minimum,TileCoord maximum,std::span<const TileCoord> goals)
  :minimum_(minimum),maximum_(maximum) {
  for(const auto c:goals) if(c.x>=minimum.x && c.x<=maximum.x && c.y>=minimum.y && c.y<=maximum.y &&
      cells_.try_emplace({c.x,c.y},Cell{0,false}).second) frontier_.emplace(0,c.x,c.y);
}
std::size_t FlowField::expand(std::size_t budget,const std::function<int(TileCoord,TileCoord)>& cost,
                            const std::function<bool(TileCoord)>& clear) {
  const auto inside=[&](TileCoord c) { return c.x>=minimum_.x && c.x<=maximum_.x && c.y>=minimum_.y && c.y<=maximum_.y; };
  constexpr std::array<TileCoord,8> offsets{{{1,0},{0,1},{-1,0},{0,-1},{1,1},{-1,1},{-1,-1},{1,-1}}};
  std::size_t work=0;
  while(!frontier_.empty() && work<budget) {
    const auto [distance,x,y]=frontier_.top(); frontier_.pop(); ++work;
    auto& at=cells_.at({x,y}); if(at.settled || at.cost!=distance) continue;
    const TileCoord to{x,y};
    if(!clear(to)) { at.cost=unreachable; continue; }
    at.settled=true;
    for(const auto d:offsets) {
      const TileCoord from{x+d.x,y+d.y};
      if(!inside(from) || !clear(from)) continue;
      // Reverse edges must charge entry into 'to', including directed slopes
      // and destination terrain costs, not entry into the expanding neighbour.
      if(d.x && d.y && (!clear({x,from.y}) || !clear({from.x,y}) ||
          cost(from,{x,from.y})<=0 || cost(from,{from.x,y})<=0)) continue;
      const auto step=cost(from,to); if(step<=0) continue;
      auto& next=cells_[{from.x,from.y}];
      if(!next.settled && distance+step<next.cost) {
        next.cost=distance+step; frontier_.emplace(next.cost,from.x,from.y);
      }
    }
  }
  return work;
}
Cost FlowField::distance(TileCoord c) const {
  const auto it=cells_.find({c.x,c.y});
  return it!=cells_.end() && it->second.settled?it->second.cost:unreachable;
}
std::uint64_t FlowField::stateHash() const {
  std::uint64_t hash=14695981039346656037ULL;
  const auto add=[&](std::uint64_t value) { hash^=value; hash*=1099511628211ULL; };
  add(minimum_.x); add(minimum_.y); add(maximum_.x); add(maximum_.y);
  for(const auto& [cell,value]:cells_) { add(cell.first); add(cell.second); add(value.cost); add(value.settled); }
  auto frontier=frontier_;
  while(!frontier.empty()) { const auto [cost,x,y]=frontier.top(); frontier.pop(); add(cost); add(x); add(y); }
  return hash;
}
namespace {
struct CompactCost {
  Cost missing=0,radius=0,travel=0;
  auto operator<=>(const CompactCost&) const = default;
  CompactCost& operator+=(const CompactCost& other) { missing+=other.missing; radius+=other.radius; travel+=other.travel; return *this; }
  CompactCost& operator-=(const CompactCost& other) { missing-=other.missing; radius-=other.radius; travel-=other.travel; return *this; }
  friend CompactCost operator-(CompactCost a,const CompactCost& b) { return a-=b; }
};
template<class Value>
std::vector<int> match(const std::vector<std::vector<Value>>& costs,Value infinity) {
  const int n=static_cast<int>(costs.size());
  if (!n) return {};
  const int m=static_cast<int>(costs.front().size());
  if (m<n) return std::vector<int>(n,-1);
  std::vector<Value> u(n+1),v(m+1);
  std::vector<int> p(m+1),way(m+1),result(n,-1);
  for(int i=1;i<=n;++i) {
    p[0]=i; int j0=0;
    std::vector<Value> minv(m+1,infinity);
    std::vector<bool> used(m+1);
    do {
      used[j0]=true; const int i0=p[j0];
      Value delta=infinity; int j1=0;
      for(int j=1;j<=m;++j) if (!used[j]) {
        const auto cur=costs[i0-1][j-1]-u[i0]-v[j];
        if (cur<minv[j]) { minv[j]=cur; way[j]=j0; }
        if (minv[j]<delta) { delta=minv[j]; j1=j; }
      }
      for(int j=0;j<=m;++j) {
        if (used[j]) { u[p[j]]+=delta; v[j]-=delta; }
        else minv[j]-=delta;
      }
      j0=j1;
    } while(p[j0]);
    do { const int j1=way[j0]; p[j0]=p[j1]; j0=j1; } while(j0);
  }
  for(int j=1;j<=m;++j) if(p[j]) result[p[j]-1]=j-1;
  return result;
}
}
std::vector<int> assign(const std::vector<std::vector<Cost>>& costs) {
  return match(costs,std::numeric_limits<Cost>::max());
}
std::vector<int> assignCompact(const std::vector<std::vector<Cost>>& costs,std::span<const Cost> radii) {
  std::vector<std::vector<CompactCost>> priorities;
  for(const auto& row:costs) {
    std::vector<CompactCost> values;
    for(std::size_t j=0;j<row.size();++j) {
      if(j>=radii.size()) values.push_back({1,0,row[j]});
      else if(row[j]>=unreachable/2) values.push_back({2,0,0});
      else values.push_back({0,radii[j],row[j]});
    }
    priorities.push_back(std::move(values));
  }
  return match(priorities,CompactCost{unreachable,0,0});
}
std::vector<Cost> travelCosts(TileCoord start,std::span<const TileCoord> targets,
  const std::function<int(TileCoord,TileCoord)>& duration,
  const std::function<bool(TileCoord)>& clear,std::size_t& budget,int minimumStep) {
  using Key=std::pair<int,int>;
  using Node=std::tuple<Cost,Cost,int,int>;
  const auto cellKey=[](int x,int y) { return (std::uint64_t(std::uint32_t(x))<<32)|std::uint32_t(y); };
  std::unordered_map<std::uint64_t,Cost> distances;
  std::map<Key,std::vector<std::size_t>> remaining;
  std::vector<Cost> result(targets.size(),unreachable);
  if(targets.empty()) return result;
  int minX=targets[0].x,maxX=minX,minY=targets[0].y,maxY=minY;
  for(const auto c:targets) { minX=std::min(minX,c.x); maxX=std::max(maxX,c.x); minY=std::min(minY,c.y); maxY=std::max(maxY,c.y); }
  const auto heuristic=[&](int x,int y) {
    return Cost(std::max(std::max({minX-x,0,x-maxX}),std::max({minY-y,0,y-maxY})))*minimumStep;
  };
  for(std::size_t i=0;i<targets.size();++i) remaining[{targets[i].x,targets[i].y}].push_back(i);
  std::priority_queue<Node,std::vector<Node>,std::greater<Node>> queue;
  queue.emplace(heuristic(start.x,start.y),0,start.x,start.y); distances[cellKey(start.x,start.y)]=0;
  constexpr std::array<TileCoord,8> offsets{{{1,0},{0,1},{-1,0},{0,-1},{1,1},{-1,1},{-1,-1},{1,-1}}};
  while(!queue.empty() && budget && !remaining.empty()) {
    const auto [priority,cost,x,y]=queue.top(); queue.pop(); (void)priority;
    if (distances.at(cellKey(x,y))!=cost) continue;
    --budget;
    if (const auto it=remaining.find({x,y});it!=remaining.end()) {
      for(const auto index:it->second) result[index]=cost;
      remaining.erase(it);
    }
    for(const auto d:offsets) {
      const TileCoord next{x+d.x,y+d.y};
      if (!clear(next) || (d.x && d.y && (!clear({next.x,y}) || !clear({x,next.y}) ||
          duration({x,y},{next.x,y})<=0 || duration({x,y},{x,next.y})<=0))) continue;
      const int step=duration({x,y},next); if(step<=0) continue;
      const auto candidate=cost+step;
      const auto it=distances.find(cellKey(next.x,next.y));
      if (it!=distances.end() && it->second<=candidate) continue;
      distances[cellKey(next.x,next.y)]=candidate; queue.emplace(candidate+heuristic(next.x,next.y),candidate,next.x,next.y);
    }
  }
  return result;
}
}
