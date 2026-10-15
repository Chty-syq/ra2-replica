#include "game_clock.h"
#include <algorithm>
#include <limits>

namespace ra2 {
std::uint32_t GameClock::advance(std::uint64_t elapsed, std::uint32_t budget) {
  const auto rate = targetGameFrameRate(speed_);
  if (rate) {
    if (elapsed > (std::numeric_limits<std::uint64_t>::max() - accumulated_) / *rate)
      throw std::out_of_range("Game clock overflow");
    accumulated_ += elapsed * *rate;
  }
  const auto earned = static_cast<std::uint32_t>(std::min<std::uint64_t>(pendingFrames(), budget));
  accumulated_ -= static_cast<std::uint64_t>(earned) * kFrameFractionScale;
  // Unlimited mode has no wall-clock rate. The host runs bounded batches between
  // render/input deadlines, rather than tying N game frames to each display frame.
  return rate ? earned : budget;
}
double GameClock::interpolation() const {
  return targetGameFrameRate(speed_) ? std::min(1.0, static_cast<double>(accumulated_) / kFrameFractionScale) : 1.0;
}
}
