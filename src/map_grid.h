#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct TileCoord {
  int x = 0;
  int y = 0;
  bool operator==(const TileCoord&) const = default;
};

struct BuildingPlacement {
  // Northwest starting cell, not the footprint center or the sprite center.
  TileCoord topLeft{};
  int width = 1;
  int height = 1;
};

struct TerrainSurface {
  std::string land = "CLEAR";
  int heightLeptons = 0;
  // Height difference across one cell along each map axis (ramps).
  int slopeX = 0, slopeY = 0;
  int wallHeightLeptons = 0;
  bool cliff = false;
};

// Sparse, unbounded sandbox grid. Width/height are nominal viewport metadata;
// negative coordinates and cells outside those dimensions remain valid.
class MapGrid {
public:
  struct Cell {
    int terrainIndex = 0;
    bool buildable = true;
    bool occupied = false;
    TerrainSurface surface;
  };

  MapGrid(int width, int height);

  [[nodiscard]] int width() const noexcept;
  [[nodiscard]] int height() const noexcept;
  [[nodiscard]] bool inBounds(TileCoord coord) const noexcept;
  [[nodiscard]] bool isBuildable(TileCoord coord) const;
  [[nodiscard]] bool canPlace(const BuildingPlacement& placement) const;
  [[nodiscard]] bool canPlace(const std::vector<TileCoord>& coords) const;

  void setTerrainIndex(TileCoord coord, int terrainIndex);
  void setSurface(TileCoord coord, TerrainSurface surface);
  int heightAt(std::int64_t xMilliCells, std::int64_t yMilliCells) const;
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
