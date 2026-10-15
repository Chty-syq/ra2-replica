#include "foundation.h"
#include <cstdint>
#include <limits>
#include <stdexcept>

std::vector<TileCoord> foundationTilesFromAxes(const TileCoord start, const int width, const int height) {
  if (width <= 0 || height <= 0) throw std::invalid_argument("Foundation dimensions must be positive");
  if (static_cast<std::int64_t>(start.x) + width - 1 > std::numeric_limits<int>::max() ||
      static_cast<std::int64_t>(start.y) + height - 1 > std::numeric_limits<int>::max())
    throw std::out_of_range("Foundation coordinate overflow");
  std::vector<TileCoord> cells;
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) cells.push_back({start.x + x, start.y + y});
  return cells;
}

std::vector<TileCoord> foundationTiles(const BuildingPlacement& placement) {
  return foundationTilesFromAxes(placement.topLeft, placement.width, placement.height);
}
