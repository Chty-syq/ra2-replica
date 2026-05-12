#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

struct TileCoord {
  int x = 0;
  int y = 0;
};

struct BuildingPlacement {
  TileCoord topLeft{};
  int width = 1;
  int height = 1;
};

class MapGrid {
public:
  struct Cell {
    int terrainIndex = 0;
    bool buildable = true;
    bool occupied = false;
  };

  MapGrid(int width, int height);

  [[nodiscard]] int width() const noexcept;
  [[nodiscard]] int height() const noexcept;
  [[nodiscard]] bool inBounds(TileCoord coord) const noexcept;
  [[nodiscard]] bool isBuildable(TileCoord coord) const;
  [[nodiscard]] bool canPlace(const BuildingPlacement& placement) const;
  [[nodiscard]] bool canPlace(const std::vector<TileCoord>& coords) const;

  void setTerrainIndex(TileCoord coord, int terrainIndex);
  void setBuildable(TileCoord coord, bool buildable);
  void setOccupied(const BuildingPlacement& placement, bool occupied);
  void setOccupied(const std::vector<TileCoord>& coords, bool occupied);

  [[nodiscard]] const Cell& cell(TileCoord coord) const;

private:
  [[nodiscard]] std::uint64_t key(TileCoord coord) const noexcept;

  int width_ = 0;
  int height_ = 0;
  std::unordered_map<std::uint64_t, Cell> cells_;
};
