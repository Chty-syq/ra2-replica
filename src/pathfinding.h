#pragma once

#include "map_grid.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

struct TilePath {
  TileCoord target{};
  // Waypoints exclude the starting cell. An empty path means already at target.
  std::vector<TileCoord> cells;
};

// The sandbox map is unbounded, so every synchronous search needs a work limit.
// Occupied destinations resolve to a nearby free cell (within 10 cells).
// nullopt means no path was found within the expansion budget.
[[nodiscard]] std::optional<TilePath> findTilePath(const MapGrid& map,
                                                TileCoord start,
                                                TileCoord requestedTarget,
                                                std::size_t maxExpandedNodes = 8192,
                                                std::size_t* expandedNodes = nullptr,
                                                const std::function<int(TileCoord,TileCoord)>& speedPercent = {},
                                                const std::function<bool(TileCoord)>& passable = {},
                                                bool adjustTarget = true);
