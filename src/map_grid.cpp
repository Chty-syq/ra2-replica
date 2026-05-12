#include "map_grid.h"

MapGrid::MapGrid(const int width, const int height)
  : width_(width), height_(height) {}

int MapGrid::width() const noexcept {
  return width_;
}

int MapGrid::height() const noexcept {
  return height_;
}

bool MapGrid::inBounds(const TileCoord coord) const noexcept {
  (void)coord;
  return true;
}

bool MapGrid::isBuildable(const TileCoord coord) const {
  const auto& current = cell(coord);
  return current.buildable && !current.occupied;
}

bool MapGrid::canPlace(const BuildingPlacement& placement) const {
  for (int dy = 0; dy < placement.height; ++dy) {
    for (int dx = 0; dx < placement.width; ++dx) {
      if (!isBuildable(TileCoord{placement.topLeft.x + dx, placement.topLeft.y + dy})) {
        return false;
      }
    }
  }

  return true;
}

bool MapGrid::canPlace(const std::vector<TileCoord>& coords) const {
  for (const auto coord : coords) {
    if (!isBuildable(coord)) {
      return false;
    }
  }
  return true;
}

void MapGrid::setTerrainIndex(const TileCoord coord, const int terrainIndex) {
  cells_[key(coord)].terrainIndex = terrainIndex;
}

void MapGrid::setBuildable(const TileCoord coord, const bool buildable) {
  cells_[key(coord)].buildable = buildable;
}

void MapGrid::setOccupied(const BuildingPlacement& placement, const bool occupied) {
  for (int dy = 0; dy < placement.height; ++dy) {
    for (int dx = 0; dx < placement.width; ++dx) {
      cells_[key(TileCoord{placement.topLeft.x + dx, placement.topLeft.y + dy})].occupied = occupied;
    }
  }
}

void MapGrid::setOccupied(const std::vector<TileCoord>& coords, const bool occupied) {
  for (const auto coord : coords) {
    cells_[key(coord)].occupied = occupied;
  }
}

const MapGrid::Cell& MapGrid::cell(const TileCoord coord) const {
  static const Cell defaultCell{};
  const auto it = cells_.find(key(coord));
  if (it == cells_.end()) {
    return defaultCell;
  }
  return it->second;
}

std::uint64_t MapGrid::key(const TileCoord coord) const noexcept {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(coord.x)) << 32) |
         static_cast<std::uint32_t>(coord.y);
}
