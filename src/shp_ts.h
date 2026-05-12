#pragma once

#include "palette.h"

#include <cstdint>
#include <filesystem>
#include <vector>

struct DecodedFrame {
  struct RowSegment {
    std::uint16_t startX = 0;
    std::uint16_t endX = 0;
  };

  struct RowSpan {
    std::uint16_t y = 0;
    std::uint16_t startX = 0;
    std::uint16_t endX = 0;
    bool hasPixels = false;
    std::vector<RowSegment> segments;
  };

  std::uint16_t width = 0;
  std::uint16_t height = 0;
  std::uint16_t frameX = 0;
  std::uint16_t frameY = 0;
  std::uint16_t frameWidth = 0;
  std::uint16_t frameHeight = 0;
  std::uint32_t flags = 0;
  std::vector<std::uint8_t> indices;
  std::vector<RowSpan> rowSpans;
};

class ShpTsFile {
public:
  static ShpTsFile load(const std::filesystem::path& path);

  [[nodiscard]] std::size_t frameCount() const noexcept;
  [[nodiscard]] DecodedFrame decodeFrame(std::size_t frameIndex) const;
  [[nodiscard]] std::vector<std::uint8_t> decodeFrameRgba(std::size_t frameIndex, const Palette& palette) const;
  [[nodiscard]] std::vector<std::uint8_t> decodeFrameRgba(std::size_t frameIndex,
                                                          const Palette& palette,
                                                          const PaletteSampleOptions& sampleOptions) const;

private:
  struct FrameEntry {
    std::uint16_t frameX = 0;
    std::uint16_t frameY = 0;
    std::uint16_t frameWidth = 0;
    std::uint16_t frameHeight = 0;
    std::uint32_t flags = 0;
    std::uint32_t dataOffset = 0;
  };

  static std::uint16_t readU16(const std::vector<std::uint8_t>& bytes, std::size_t offset);
  static std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, std::size_t offset);

  std::uint16_t fullWidth_ = 0;
  std::uint16_t fullHeight_ = 0;
  std::vector<FrameEntry> frames_;
  std::vector<std::uint8_t> bytes_;
};
