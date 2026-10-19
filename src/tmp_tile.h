#pragma once

#include "palette.h"

#include <cstdint>
#include <filesystem>
#include <vector>

struct TmpTile {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> rgba;
  int landType=0,heightLevel=0;
};

class TmpFile {
public:
  static TmpFile load(const std::filesystem::path& path);

  [[nodiscard]] TmpTile decodeSingleTile(const Palette& palette) const;
  // Native TS/RA2 templates have a 52-byte cell header and may span many cells.
  [[nodiscard]] std::vector<TmpTile> decodeCells(const Palette& palette) const;

private:
  static std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, std::size_t offset);

  std::vector<std::uint8_t> bytes_;
  std::uint32_t blockCountX_ = 0;
  std::uint32_t blockCountY_ = 0;
  std::uint32_t imageWidth_ = 0;
  std::uint32_t imageHeight_ = 0;
};
