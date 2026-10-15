#pragma once

#include "map_grid.h"
#include <cstdint>
#include <filesystem>
#include <vector>

namespace ra2 {
// Original flat-cell overlays: twelve variants, twelve density frames each.
// The second half of each SHP is an empty shadow sequence, not more densities.
inline constexpr int kResourceVariants = 12;
inline constexpr int kResourceDensities = 12;
struct ResourceAtlasPixels {
  int frameWidth = 0, frameHeight = 0, columns = 0, rows = 0;
  std::vector<std::uint8_t> indices;
};
ResourceAtlasPixels loadResourceAtlas(const std::filesystem::path& directory,
                                     const char* prefix, const char* extension);
ResourceAtlasPixels loadGatherAtlas(const std::filesystem::path& path);
int resourceVariant(TileCoord cell);
// Full-density quantity is a demo presentation setting, not an economy limit.
int resourceDensityFrame(int amount, int fullDensityAmount);
int gatherDirection(int heading);
}
