#include "pathfinding.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <unordered_map>
#include <utility>

namespace {
bool sameCell(const TileCoord a, const TileCoord b) {
  return a.x == b.x && a.y == b.y;
}

std::uint64_t tileKey(const TileCoord cell) {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell.x)) << 32) |
         static_cast<std::uint32_t>(cell.y);
}

std::int64_t distance(const TileCoord a, const TileCoord b) {
  const auto dx = std::abs(static_cast<std::int64_t>(a.x) - b.x);
  const auto dy = std::abs(static_cast<std::int64_t>(a.y) - b.y);
  return 1000 * std::max(dx, dy) + 414 * std::min(dx, dy);
}

std::optional<TileCoord> offsetCell(const TileCoord cell, const TileCoord offset) {
  const auto x = static_cast<std::int64_t>(cell.x) + offset.x;
  const auto y = static_cast<std::int64_t>(cell.y) + offset.y;
  if (x < std::numeric_limits<int>::min() || x > std::numeric_limits<int>::max() ||
      y < std::numeric_limits<int>::min() || y > std::numeric_limits<int>::max()) {
    return std::nullopt;
  }
  return TileCoord{static_cast<int>(x), static_cast<int>(y)};
}

std::optional<TileCoord> resolveTarget(const MapGrid& map,
                                     const TileCoord start,
                                     const TileCoord requested,
                                     const std::function<int(TileCoord,TileCoord)>& speedPercent,
                                     const std::function<bool(TileCoord)>& passable, bool adjustTarget) {
  const auto enterable=[&](TileCoord c) { return (passable ? passable(c) : map.isBuildable(c)) && (!speedPercent || speedPercent(c,c)>0); };
  if (sameCell(start, requested) || enterable(requested)) {
    return requested;
  }

  if (!adjustTarget) return std::nullopt;

  for (int radius = 1; radius <= 10; ++radius) {
    std::optional<TileCoord> best;
    auto bestDistance = std::numeric_limits<std::int64_t>::max();
    auto bestStartDistance = std::numeric_limits<std::int64_t>::max();
    for (int dy = -radius; dy <= radius; ++dy) {
      for (int dx = -radius; dx <= radius; ++dx) {
        if (std::max(std::abs(dx), std::abs(dy)) != radius) continue;
        const auto candidate = offsetCell(requested, {dx, dy});
        if (!candidate || (!sameCell(*candidate, start) && !enterable(*candidate))) continue;
        const auto targetDistance = distance(*candidate, requested);
        const auto startDistance = distance(*candidate, start);
        if (targetDistance < bestDistance ||
            (targetDistance == bestDistance && startDistance < bestStartDistance)) {
          best = candidate;
          bestDistance = targetDistance;
          bestStartDistance = startDistance;
        }
      }
    }
    if (best) return best;
  }
  return std::nullopt;
}

struct PathNode {
  TileCoord cell{};
  std::int64_t cost = 0;
  std::int64_t priority = 0;
};

struct PathNodeGreater {
  bool operator()(const PathNode& a, const PathNode& b) const {
    if (a.priority != b.priority) return a.priority > b.priority;
    if (a.cost != b.cost) return a.cost > b.cost;
    return tileKey(a.cell) > tileKey(b.cell);
  }
};
}  // namespace

std::optional<TilePath> findTilePath(const MapGrid& map,
                                   const TileCoord start,
                                   const TileCoord requestedTarget,
                                   const std::size_t maxExpandedNodes,
                                   std::size_t* expandedNodes,
                                   const std::function<int(TileCoord,TileCoord)>& speedPercent,
                                   const std::function<bool(TileCoord)>& passable, bool adjustTarget) {
  if (expandedNodes) *expandedNodes = 0;
  const auto clear = [&](TileCoord c) { return passable ? passable(c) : map.isBuildable(c); };
  const auto target = resolveTarget(map, start, requestedTarget,speedPercent,passable,adjustTarget);
  if (!target) return std::nullopt;
  if (sameCell(start, *target)) return TilePath{*target, {}};

  // Preserve the sandbox's eight-way movement, including diagonal neighbors.
  static constexpr std::array<TileCoord, 8> neighbors{{
    {1, 0}, {-1, 0}, {0, 1}, {0, -1},
    {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
  }};
  std::priority_queue<PathNode, std::vector<PathNode>, PathNodeGreater> frontier;
  std::unordered_map<std::uint64_t, std::int64_t> costs;
  std::unordered_map<std::uint64_t, TileCoord> previous;
  frontier.push({start, 0, distance(start, *target)});
  costs.emplace(tileKey(start), 0);

  std::size_t expanded = 0;
  while (!frontier.empty() && expanded < maxExpandedNodes) {
    const auto current = frontier.top();
    frontier.pop();
    if (current.cost > costs.at(tileKey(current.cell))) continue;
    ++expanded;
    if (expandedNodes) *expandedNodes = expanded;
    if (sameCell(current.cell, *target)) {
      TilePath result{*target, {}};
      for (auto cell = *target; !sameCell(cell, start); cell = previous.at(tileKey(cell))) {
        result.cells.push_back(cell);
      }
      std::reverse(result.cells.begin(), result.cells.end());
      return result;
    }

    for (const auto offset : neighbors) {
      const auto next = offsetCell(current.cell, offset);
      if (!next || (!sameCell(*next, start) && !clear(*next))) continue;
      if (offset.x != 0 && offset.y != 0 &&
          (!clear({next->x, current.cell.y}) || !clear({current.cell.x, next->y}))) continue;
      const int speed = speedPercent ? speedPercent(current.cell,*next) : 100;
      if (speed <= 0) continue;
      if (offset.x && offset.y && speedPercent &&
          (speedPercent(current.cell,{next->x,current.cell.y})<=0 || speedPercent(current.cell,{current.cell.x,next->y})<=0)) continue;
      const auto nextCost = current.cost + (offset.x != 0 && offset.y != 0 ? 1414LL : 1000LL)*100/speed;
      const auto key = tileKey(*next);
      const auto found = costs.find(key);
      if (found != costs.end() && nextCost >= found->second) continue;
      costs[key] = nextCost;
      previous[key] = current.cell;
      frontier.push({*next, nextCost, nextCost + distance(*next, *target)});
    }
  }
  return std::nullopt;
}
