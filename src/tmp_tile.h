#pragma once

#include "palette.h"

#include <cstdint>
#include <filesystem>
#include <vector>

struct TmpTile {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> rgba;
};

class TmpFile {
public:
  static TmpFile load(const std::filesystem::path& path);

  [[nodiscard]] TmpTile decodeSingleTile(const Palette& palette) const;

private:
  static std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, std::size_t offset);

  std::vector<std::uint8_t> bytes_;
  std::uint32_t blockCountX_ = 0;
  std::uint32_t blockCountY_ = 0;
  std::uint32_t imageWidth_ = 0;
  std::uint32_t imageHeight_ = 0;
};
