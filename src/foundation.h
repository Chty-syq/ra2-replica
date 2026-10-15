#pragma once
#include "map_grid.h"
// Rectangular Foundation=width x height. Anchor is the northwest starting cell;
// shared by placement, simulation occupancy and rendering, regardless of size.
std::vector<TileCoord> foundationTilesFromAxes(TileCoord anchor, int width, int height);
std::vector<TileCoord> foundationTiles(const BuildingPlacement& placement);
