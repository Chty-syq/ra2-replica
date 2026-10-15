#include "map_grid.h"
#include "foundation.h"

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
  return canPlace(foundationTiles(placement));
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

void MapGrid::setSurface(TileCoord coord, TerrainSurface surface) {
  cells_[key(coord)].surface=std::move(surface);
}
int MapGrid::heightAt(std::int64_t x, std::int64_t y) const {
  const auto nearest=[](std::int64_t n) { return static_cast<int>((n>=-500 ? n+500 : n-499)/1000); };
  const TileCoord coord{nearest(x),nearest(y)};
  const auto& s=cell(coord).surface;
  return s.heightLeptons+static_cast<int>((s.slopeX*(x-coord.x*1000LL)+s.slopeY*(y-coord.y*1000LL))/1000);
}

void MapGrid::setBuildable(const TileCoord coord, const bool buildable) {
  cells_[key(coord)].buildable = buildable;
}

void MapGrid::setOccupied(const BuildingPlacement& placement, const bool occupied) {
  setOccupied(foundationTiles(placement), occupied);
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
