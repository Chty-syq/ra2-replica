#include "resource_art.h"
#include "shp_ts.h"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ra2 {
namespace {
void putFrame(ResourceAtlasPixels& atlas, const DecodedFrame& frame, int column, int row) {
  if (frame.width != atlas.frameWidth || frame.height != atlas.frameHeight)
    throw std::runtime_error("Inconsistent original resource sprite canvas");
  const auto width = atlas.frameWidth * atlas.columns;
  for (int y = 0; y < atlas.frameHeight; ++y)
    std::copy_n(frame.indices.begin() + y * atlas.frameWidth, atlas.frameWidth,
                atlas.indices.begin() + (row * atlas.frameHeight + y) * width + column * atlas.frameWidth);
}
}
ResourceAtlasPixels loadResourceAtlas(const std::filesystem::path& directory,
                                     const char* prefix, const char* extension) {
  ResourceAtlasPixels atlas{60, 60, kResourceDensities, kResourceVariants, {}};
  atlas.indices.resize(atlas.frameWidth * atlas.frameHeight * atlas.columns * atlas.rows);
  for (int variant = 0; variant < kResourceVariants; ++variant) {
    const int number = variant + 1;
    const auto path = directory / (std::string(prefix) + (number < 10 ? "0" : "") +
                                   std::to_string(number) + extension);
    const auto shp = ShpTsFile::load(path);
    if (shp.frameCount() != 24) throw std::runtime_error("Expected 12 density + 12 shadow frames: " + path.string());
    for (int density = 0; density < kResourceDensities; ++density) {
      // Original TIB02 puts its fullest frame first in all three theaters.
      // Reorder references only so the final few units never look like a full cell.
      const int source = std::string_view(prefix)=="tib" && variant==1 ? (density+1)%12 : density;
      putFrame(atlas, shp.decodeFrame(source), density, variant);
    }
  }
  return atlas;
}
ResourceAtlasPixels loadGatherAtlas(const std::filesystem::path& path) {
  const auto shp = ShpTsFile::load(path);
  if (shp.frameCount() != 120) throw std::runtime_error("Expected 8 directions x 15 gathering frames: " + path.string());
  ResourceAtlasPixels atlas{144, 80, 15, 8, {}};
  atlas.indices.resize(atlas.frameWidth * atlas.frameHeight * atlas.columns * atlas.rows);
  for (int direction = 0; direction < atlas.rows; ++direction)
    for (int frame = 0; frame < atlas.columns; ++frame)
      putFrame(atlas, shp.decodeFrame(direction * atlas.columns + frame), frame, direction);
  return atlas;
}
int resourceVariant(TileCoord cell) {
  // Fixed cell-based variation; depletion and redraws never reshuffle a patch.
  std::uint32_t hash = static_cast<std::uint32_t>(cell.x) * 0x9e3779b9u ^
                       static_cast<std::uint32_t>(cell.y) * 0x85ebca6bu;
  hash ^= hash >> 16;
  return static_cast<int>(hash % kResourceVariants);
}
int resourceDensityFrame(int amount, int fullDensityAmount) {
  if (amount <= 0) return -1;
  const auto full = std::max(1, fullDensityAmount);
  if (full == 1) return kResourceDensities - 1;
  return static_cast<int>((static_cast<std::int64_t>(std::min(amount, full)) - 1) *
                           (kResourceDensities - 1) / (full - 1));
}
int gatherDirection(int heading) {
  // World heading 0 = logical +X (screen SE); OREGATH is N, NW, W, SW, S, SE, E, NE.
  const auto normalized = (heading % 65536 + 65536) % 65536;
  return (13 - (normalized + 4096) / 8192) % 8;
}
}
