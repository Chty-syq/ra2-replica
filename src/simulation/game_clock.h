#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace ra2 {
using GameFrame = std::uint64_t;
// Rules seconds/minutes use this reference. It is NOT the wall-clock tick rate.
inline constexpr int kGameFramesPerSecond = 15;
inline constexpr int kGameFramesPerMinute = 900;
inline constexpr std::uint64_t kFrameFractionScale = 1000000;

enum class GameSpeedMode { Multiplayer, Singleplayer };
struct GameSpeed {
  GameSpeedMode mode = GameSpeedMode::Multiplayer;
  int level = 6;
  bool operator==(const GameSpeed&) const = default;
};
[[nodiscard]] inline std::optional<int> targetGameFrameRate(GameSpeed speed) {
  if (speed.level < 0 || speed.level > 6 ||
      (speed.mode != GameSpeedMode::Multiplayer && speed.mode != GameSpeedMode::Singleplayer))
    throw std::invalid_argument("Game speed requires a valid mode and level 0..6");
  constexpr std::array multiplayer{10,12,15,20,30,45,60};
  constexpr std::array singleplayer{10,12,15,20,30,60,0};
  const auto rate = (speed.mode == GameSpeedMode::Multiplayer ? multiplayer : singleplayer)[speed.level];
  return rate ? std::optional{rate} : std::nullopt;
}
[[nodiscard]] inline std::uint32_t gameMilliseconds(GameFrame frame) {
  // Divide first to avoid overflow; the final 32-bit wrap matches SDL clocks.
  return static_cast<std::uint32_t>((frame / kGameFramesPerSecond) * 1000 +
                                    (frame % kGameFramesPerSecond) * 1000 / kGameFramesPerSecond);
}

// Scheduling state only. No rule, movement or cooldown depends on this clock.
// A speed change affects future elapsed time, preserving all earned frame credit.
class GameClock {
public:
  explicit GameClock(GameSpeed speed = {}) { setSpeed(speed); }
  void setSpeed(GameSpeed speed) { (void)targetGameFrameRate(speed); speed_ = speed; }
  GameSpeed speed() const { return speed_; }
  std::uint32_t advance(std::uint64_t elapsedMicroseconds, std::uint32_t budget = 120);
  double interpolation() const;
  std::uint64_t pendingFrames() const { return accumulated_ / kFrameFractionScale; }
private:
  GameSpeed speed_;
  std::uint64_t accumulated_ = 0;
};
}
