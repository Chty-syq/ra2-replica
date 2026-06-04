#pragma once

#include "palette.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

enum class TheaterStyle {
  Temperate,
  Snow,
  Urban
};

enum class BuildFaction {
  Allied,
  Soviet
};

struct DemoVisualStyle {
  BuildFaction faction = BuildFaction::Allied;
  TheaterStyle theater = TheaterStyle::Temperate;
  std::size_t houseColorIndex = 0;
};

[[nodiscard]] inline bool sameVisualStyle(const DemoVisualStyle& lhs, const DemoVisualStyle& rhs) {
  return lhs.faction == rhs.faction &&
         lhs.theater == rhs.theater &&
         lhs.houseColorIndex == rhs.houseColorIndex;
}

struct HouseColorEntry {
  std::string name;
  HsvColor hsv;
  Rgba color;
};

struct HouseColorSet {
  std::vector<HouseColorEntry> colors;
  std::size_t defaultIndex = 0;
};

inline constexpr std::array<TheaterStyle, 3> kAllTheaterStyles{
  TheaterStyle::Temperate,
  TheaterStyle::Snow,
  TheaterStyle::Urban
};

inline constexpr std::array<BuildFaction, 2> kAllBuildFactions{
  BuildFaction::Allied,
  BuildFaction::Soviet
};

[[nodiscard]] inline std::string_view theaterShortName(const TheaterStyle theater) {
  switch (theater) {
    case TheaterStyle::Temperate: return "TEM";
    case TheaterStyle::Snow: return "SNO";
    case TheaterStyle::Urban: return "URB";
  }
  return "TEM";
}

[[nodiscard]] inline Rgba theaterSwatchColor(const TheaterStyle theater) {
  switch (theater) {
    case TheaterStyle::Temperate: return Rgba{83, 132, 73, 255};
    case TheaterStyle::Snow: return Rgba{214, 224, 236, 255};
    case TheaterStyle::Urban: return Rgba{122, 127, 136, 255};
  }
  return Rgba{83, 132, 73, 255};
}

[[nodiscard]] inline Rgba houseColorValue(const std::size_t index, const HouseColorSet& colors) {
  if (colors.colors.empty()) {
    return Rgba{68, 126, 255, 255};
  }

  const auto safeIndex = std::min(index, colors.colors.size() - 1);
  return colors.colors[safeIndex].color;
}

[[nodiscard]] inline HsvColor houseColorHsv(const std::size_t index, const HouseColorSet& colors) {
  if (colors.colors.empty()) {
    return HsvColor{153, 214, 212};
  }

  const auto safeIndex = std::min(index, colors.colors.size() - 1);
  return colors.colors[safeIndex].hsv;
}

[[nodiscard]] inline std::string_view houseColorName(const std::size_t index, const HouseColorSet& colors) {
  if (colors.colors.empty()) {
    return "None";
  }

  const auto safeIndex = std::min(index, colors.colors.size() - 1);
  return colors.colors[safeIndex].name;
}

[[nodiscard]] inline Rgba activeHouseColorValue(const DemoVisualStyle& style, const HouseColorSet& colors) {
  return houseColorValue(style.houseColorIndex, colors);
}

[[nodiscard]] inline HsvColor activeHouseColorHsv(const DemoVisualStyle& style, const HouseColorSet& colors) {
  return houseColorHsv(style.houseColorIndex, colors);
}
