#include "tmp_tile.h"

#include <fstream>
#include <iterator>
#include <stdexcept>

TmpFile TmpFile::load(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Failed to open TMP tile: " + path.string());
  }

  TmpFile file;
  file.bytes_ = std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                          std::istreambuf_iterator<char>());

  if (file.bytes_.size() < 20) {
    throw std::runtime_error("TMP file too small: " + path.string());
  }

  file.blockCountX_ = readU32(file.bytes_, 0);
  file.blockCountY_ = readU32(file.bytes_, 4);
  file.imageWidth_ = readU32(file.bytes_, 8);
  file.imageHeight_ = readU32(file.bytes_, 12);

  return file;
}

TmpTile TmpFile::decodeSingleTile(const Palette& palette) const {
  if (blockCountX_ != 1 || blockCountY_ != 1) {
    throw std::runtime_error("This demo only supports single-cell TMP tiles for now");
  }

  const auto cellHeaderOffset = readU32(bytes_, 16);
  const std::size_t imageDataOffset = cellHeaderOffset + 32;
  if (imageDataOffset >= bytes_.size()) {
    throw std::runtime_error("TMP image data offset is out of range");
  }

  if (imageHeight_ == 0 || (imageHeight_ % 2) != 0) {
    throw std::runtime_error("Unexpected TMP dimensions");
  }

  const auto halfRows = imageHeight_ / 2;
  const auto rowStep = imageWidth_ / halfRows;

  TmpTile tile;
  tile.width = imageWidth_;
  tile.height = imageHeight_;
  tile.rgba.assign(static_cast<std::size_t>(imageWidth_ * imageHeight_ * 4), 0);

  std::size_t src = imageDataOffset;
  for (std::uint32_t y = 0; y < imageHeight_; ++y) {
    const auto rowWidth = y < halfRows
      ? rowStep * (y + 1)
      : rowStep * (imageHeight_ - y);
    const auto rowStartX = (imageWidth_ - rowWidth) / 2;

    for (std::uint32_t x = 0; x < rowWidth; ++x) {
      if (src >= bytes_.size()) {
        throw std::runtime_error("TMP image payload overruns file");
      }

      const auto color = palette.color(bytes_[src++]);
      const std::size_t dst = static_cast<std::size_t>((y * imageWidth_ + rowStartX + x) * 4);
      tile.rgba[dst + 0] = color.r;
      tile.rgba[dst + 1] = color.g;
      tile.rgba[dst + 2] = color.b;
      tile.rgba[dst + 3] = 255;
    }
  }

  return tile;
}

std::uint32_t TmpFile::readU32(const std::vector<std::uint8_t>& bytes, const std::size_t offset) {
  return static_cast<std::uint32_t>(bytes.at(offset)) |
         (static_cast<std::uint32_t>(bytes.at(offset + 1)) << 8) |
         (static_cast<std::uint32_t>(bytes.at(offset + 2)) << 16) |
         (static_cast<std::uint32_t>(bytes.at(offset + 3)) << 24);
}
