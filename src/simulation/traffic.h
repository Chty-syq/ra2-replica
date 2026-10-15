#pragma once
#include "map_grid.h"
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <queue>
#include <span>
#include <tuple>
#include <vector>

namespace ra2::traffic {
using Cost = std::int64_t;
inline constexpr Cost unreachable = 1'000'000'000;
// Rectangular Hungarian matching. Columns may include dummy (unassigned) slots.
std::vector<int> assign(const std::vector<std::vector<Cost>>& costs);
// Lexicographic matching: maximize reachable members, minimize total squared
// distance to the click, then minimize route/spatial-order cost. Columns beyond
// radii are dummy assignments. Compactness cannot be traded for travel savings.
std::vector<int> assignCompact(const std::vector<std::vector<Cost>>& costs,std::span<const Cost> radii);
// One bounded multi-target A* search per actor, shared by all destinations.
std::vector<Cost> travelCosts(TileCoord start, std::span<const TileCoord> targets,
  const std::function<int(TileCoord,TileCoord)>& duration,
  const std::function<bool(TileCoord)>& clear, std::size_t& budget, int minimumStep=1);

// A bounded, resumable reverse Dijkstra integration field. All members of a
// group with the same locomotor share it. Only settled distances are exposed;
// an unfinished search is never mistaken for an unreachable cell.
class FlowField {
public:
  FlowField(TileCoord minimum,TileCoord maximum,std::span<const TileCoord> goals);
  std::size_t expand(std::size_t budget,const std::function<int(TileCoord,TileCoord)>& cost,
                     const std::function<bool(TileCoord)>& clear);
  Cost distance(TileCoord cell) const;
  bool finished() const { return frontier_.empty(); }
  std::uint64_t stateHash() const;
private:
  using Key=std::pair<int,int>;
  using Entry=std::tuple<Cost,int,int>;
  struct Cell { Cost cost=unreachable; bool settled=false; };
  TileCoord minimum_,maximum_;
  std::map<Key,Cell> cells_;
  std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> frontier_;
};

}
