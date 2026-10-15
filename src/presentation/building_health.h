#pragma once
#include <algorithm>
#include <cstdint>

namespace ra2 {
struct BuildingHealthPips { int total = 0, filled = 0; };

// Preserve the established footprint-dependent bar size without inflating gameplay HP.
inline BuildingHealthPips buildingHealthPips(int width, int height, int hp, int maxHp) {
  if (maxHp <= 0) return {};
  const int shorter = std::min(width,height), longer = std::max(width,height);
  const int minimum = shorter >= 4 ? 30 : shorter >= 3 || longer >= 4 ? 22 : longer >= 2 ? 15 : 7;
  const int total = std::max(minimum,maxHp / 100);
  const int filled = static_cast<int>(static_cast<std::int64_t>(std::clamp(hp,0,maxHp)) * total / maxHp);
  return {total, hp > 0 ? std::max(1,filled) : 0};
}
}
