#pragma once
#include "simulation/game_clock.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace ra2::compat {
inline constexpr int kLeptonsPerCell = 256;
inline constexpr int kProductionStages = 54;
inline constexpr int kRatioScale = 1000000;
constexpr int buildingRefund(int cost,int strength,int hp,int percentMillionths) {
  if(strength<=0)return 0;
  const auto scaled=std::int64_t(cost)*percentMillionths;
  const auto health=std::clamp(hp,0,strength);
  return int(((scaled/kRatioScale)*health+(scaled%kRatioScale)*health/kRatioScale)/strength);
}
constexpr int buildingRepairCost(int cost,int strength,int amount,int percentMillionths) {
  if(cost<=0 || percentMillionths<=0 || amount<=0 || strength<=0)return 0;
  return std::max(1,buildingRefund(cost,strength,amount,percentMillionths));
}

// Evidence and scope: docs/gameplay-calibration-2026-09-12.md.
constexpr int movementLeptons(int rawSpeed) {
  return std::min(255, std::clamp(rawSpeed,0,100) * 256 / 100);
}
constexpr int rotationFrames(int signedAngle, int rawRot) {
  const int rate = std::clamp(rawRot,0,127) * 256;
  return rate == 0 ? 0 : (signedAngle < 0 ? -signedAngle : signedAngle) / rate;
}
constexpr int productionFrames(int cost, int buildSpeedMillionths, int multiplierMillionths) {
  // Preserve the two truncation points before the 54-stage factory clock.
  const auto base = static_cast<std::int64_t>(cost) * buildSpeedMillionths * 9 / (10LL * kRatioScale);
  const auto adjusted = base * multiplierMillionths / kRatioScale;
  return kProductionStages * static_cast<int>(std::clamp<std::int64_t>(adjusted / kProductionStages,1,255));
}
constexpr int animationDelay(int rawRate) {
  return rawRate <= 0 ? 0 : 900 / rawRate;
}
inline int normalizedAnimationDelay(int delay, GameSpeed speed) {
  (void)targetGameFrameRate(speed);
  if (delay <= 0) return 0;
  const int index = 6 - speed.level;
  // Small delays use the original integer lookup rather than a float multiplier.
  constexpr std::array<std::array<int,8>,4> small{{
    {{2,2,1,1,1,1,1,1}}, {{3,3,3,2,2,2,1,1}},
    {{5,4,4,3,3,2,2,1}}, {{7,6,5,4,4,4,3,2}}
  }};
  return delay < 5 ? small[delay-1][index] : delay * 8 / (index+1);
}
}
