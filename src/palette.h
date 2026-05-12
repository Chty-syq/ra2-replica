#pragma once

#include <array>
#include <cstdint>
#include <filesystem>

struct Rgba {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 255;
};

struct HsvColor {
  std::uint8_t h = 0;
  std::uint8_t s = 0;
  std::uint8_t v = 0;
};

struct PaletteSampleOptions {
  float brightness = 1.0f;
  bool preserveFullBrightRange = false;
};

class Palette {
public:
  static Palette load(const std::filesystem::path& path);

  [[nodiscard]] const Rgba& color(std::uint8_t index) const noexcept;
  [[nodiscard]] Rgba sample(std::uint8_t index, const PaletteSampleOptions& options = {}) const noexcept;
  [[nodiscard]] Palette remapped(const Rgba& targetColor) const;
  [[nodiscard]] Palette remapped(const HsvColor& targetColor) const;

private:
  std::array<Rgba, 256> colors_{};
};
