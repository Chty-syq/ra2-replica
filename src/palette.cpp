#include "palette.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {
// RA2 的 PAL 文件里每个颜色通道本质上是 6 bit。
// 先把它展开到 8 bit，再模拟原版 16 位高彩色的 RGB565 量化，
// 最后再回写到我们当前用的 8 bit RGBA 纹理里。
[[nodiscard]] std::uint8_t convert6BitTo8Bit(const std::uint8_t color) {
  return static_cast<std::uint8_t>((static_cast<std::uint32_t>(color) * 255u) / 63u);
}

[[nodiscard]] std::uint8_t convert8BitTo5Or6Bit(const std::uint8_t color, const bool isGreen) {
  const auto divider = isGreen ? 4u : 8u;
  return static_cast<std::uint8_t>(static_cast<std::uint32_t>(color) / divider);
}

[[nodiscard]] std::uint8_t convert5Or6BitTo8Bit(const std::uint8_t color, const bool isGreen) {
  const auto maxValue = isGreen ? 63u : 31u;
  return static_cast<std::uint8_t>((static_cast<std::uint32_t>(color) * 255u) / maxValue);
}

[[nodiscard]] Rgba quantizeToRgb565(const std::uint8_t r,
                                    const std::uint8_t g,
                                    const std::uint8_t b,
                                    const std::uint8_t a = 255) {
  const auto r5 = convert8BitTo5Or6Bit(r, false);
  const auto g6 = convert8BitTo5Or6Bit(g, true);
  const auto b5 = convert8BitTo5Or6Bit(b, false);
  return Rgba{
    convert5Or6BitTo8Bit(r5, false),
    convert5Or6BitTo8Bit(g6, true),
    convert5Or6BitTo8Bit(b5, false),
    a
  };
}

[[nodiscard]] Rgba hsvToRgb565(const HsvColor& hsv, const std::uint8_t a = 255) {
  const float hue = (static_cast<float>(hsv.h) / 255.0f) * 360.0f;
  const float saturation = static_cast<float>(hsv.s) / 255.0f;
  const float value = static_cast<float>(hsv.v) / 255.0f;

  const float chroma = value * saturation;
  const float hueSection = std::fmod(hue / 60.0f, 6.0f);
  const float x = chroma * (1.0f - std::fabs(std::fmod(hueSection, 2.0f) - 1.0f));
  const float match = value - chroma;

  float red = 0.0f;
  float green = 0.0f;
  float blue = 0.0f;

  if (hueSection >= 0.0f && hueSection < 1.0f) {
    red = chroma;
    green = x;
  } else if (hueSection < 2.0f) {
    red = x;
    green = chroma;
  } else if (hueSection < 3.0f) {
    green = chroma;
    blue = x;
  } else if (hueSection < 4.0f) {
    green = x;
    blue = chroma;
  } else if (hueSection < 5.0f) {
    red = x;
    blue = chroma;
  } else {
    red = chroma;
    blue = x;
  }

  const auto toByte = [&](const float channel) {
    return static_cast<std::uint8_t>(std::clamp(std::lround((channel + match) * 255.0f), 0L, 255L));
  };

  return quantizeToRgb565(toByte(red), toByte(green), toByte(blue), a);
}

[[nodiscard]] Rgba remapStageColor(const Rgba& targetColor,
                                   const std::uint8_t stageIndex,
                                   const std::uint8_t alpha) {
  const float t = 1.0f - static_cast<float>(stageIndex) / 15.0f;
  const auto scaleChannel = [&](const std::uint8_t channel) {
    return static_cast<std::uint8_t>(std::clamp(std::lround(static_cast<double>(channel) * t), 0L, 255L));
  };

  return quantizeToRgb565(scaleChannel(targetColor.r),
                          scaleChannel(targetColor.g),
                          scaleChannel(targetColor.b),
                          alpha);
}

[[nodiscard]] bool isFullBrightIndex(const std::uint8_t index) {
  return index >= 240 && index <= 254;
}

[[nodiscard]] Rgba applyBrightness(const Rgba& color, const float brightness) {
  const float clampedBrightness = std::clamp(brightness, 0.0f, 1.0f);
  const auto scaleChannel = [&](const std::uint8_t value) {
    const auto scaled = static_cast<int>(std::lround(static_cast<double>(value) * clampedBrightness));
    return static_cast<std::uint8_t>(std::clamp(scaled, 0, 255));
  };

  return quantizeToRgb565(scaleChannel(color.r),
                          scaleChannel(color.g),
                          scaleChannel(color.b),
                          color.a);
}
}

Palette Palette::load(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Failed to open palette: " + path.string());
  }

  const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                        std::istreambuf_iterator<char>());

  if (bytes.size() != 256 * 3) {
    throw std::runtime_error("Unexpected palette size for: " + path.string());
  }

  Palette palette;
  for (std::size_t i = 0; i < 256; ++i) {
    const auto red8 = convert6BitTo8Bit(bytes[i * 3 + 0]);
    const auto green8 = convert6BitTo8Bit(bytes[i * 3 + 1]);
    const auto blue8 = convert6BitTo8Bit(bytes[i * 3 + 2]);
    palette.colors_[i] = quantizeToRgb565(red8,
                                          green8,
                                          blue8,
                                          static_cast<std::uint8_t>(i == 0 ? 0 : 255));
  }

  return palette;
}

const Rgba& Palette::color(std::uint8_t index) const noexcept {
  return colors_[index];
}

Rgba Palette::sample(const std::uint8_t index, const PaletteSampleOptions& options) const noexcept {
  const auto& base = colors_[index];
  if (base.a == 0) {
    return base;
  }

  if (options.preserveFullBrightRange && isFullBrightIndex(index)) {
    return base;
  }

  if (std::abs(options.brightness - 1.0f) <= 0.0001f) {
    return base;
  }

  return applyBrightness(base, options.brightness);
}

Palette Palette::remapped(const Rgba& targetColor) const {
  Palette palette = *this;
  const auto quantizedTarget = quantizeToRgb565(targetColor.r, targetColor.g, targetColor.b, targetColor.a);

  for (std::uint8_t index = 16; index <= 31; ++index) {
    const auto& source = colors_[index];
    palette.colors_[index] = remapStageColor(quantizedTarget,
                                             static_cast<std::uint8_t>(index - 16),
                                             source.a);
  }

  return palette;
}

Palette Palette::remapped(const HsvColor& targetColor) const {
  const auto targetRgb = hsvToRgb565(targetColor);
  return remapped(targetRgb);
}
