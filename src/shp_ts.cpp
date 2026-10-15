#include "shp_ts.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
constexpr std::uint32_t kFrameTypeRaw0 = 0x0;
constexpr std::uint32_t kFrameTypeRaw1 = 0x1;
constexpr std::uint32_t kFrameTypeRle3 = 0x3;
}

ShpTsFile ShpTsFile::load(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Failed to open SHP: " + path.string());
  }

  ShpTsFile file;
  file.bytes_ = std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                          std::istreambuf_iterator<char>());

  if (file.bytes_.size() < 8) {
    throw std::runtime_error("SHP file too small: " + path.string());
  }

  const auto empty = readU16(file.bytes_, 0);
  if (empty != 0) {
    throw std::runtime_error("Not a TS/RA2 SHP file: " + path.string());
  }

  file.fullWidth_ = readU16(file.bytes_, 2);
  file.fullHeight_ = readU16(file.bytes_, 4);
  const auto frameCount = readU16(file.bytes_, 6);

  const std::size_t tableBytes = static_cast<std::size_t>(frameCount) * 24;
  if (file.bytes_.size() < 8 + tableBytes) {
    throw std::runtime_error("Truncated SHP frame table: " + path.string());
  }

  file.frames_.reserve(frameCount);
  for (std::size_t i = 0; i < frameCount; ++i) {
    const std::size_t offset = 8 + i * 24;
    file.frames_.push_back(FrameEntry{
      readU16(file.bytes_, offset + 0),
      readU16(file.bytes_, offset + 2),
      readU16(file.bytes_, offset + 4),
      readU16(file.bytes_, offset + 6),
      readU32(file.bytes_, offset + 8),
      readU32(file.bytes_, offset + 20)
    });
  }

  return file;
}

std::size_t ShpTsFile::frameCount() const noexcept {
  return frames_.size();
}

DecodedFrame ShpTsFile::decodeFrame(const std::size_t frameIndex) const {
  if (frameIndex >= frames_.size()) {
    throw std::out_of_range("Frame index out of range");
  }

  const auto& frame = frames_[frameIndex];
  DecodedFrame decoded;
  decoded.width = fullWidth_;
  decoded.height = fullHeight_;
  decoded.frameX = frame.frameX;
  decoded.frameY = frame.frameY;
  decoded.frameWidth = frame.frameWidth;
  decoded.frameHeight = frame.frameHeight;
  decoded.flags = frame.flags;
  decoded.indices.assign(static_cast<std::size_t>(fullWidth_) * fullHeight_, 0);
  decoded.rowSpans.assign(frame.frameHeight, DecodedFrame::RowSpan{});

  if (frame.dataOffset == 0 || frame.frameWidth == 0 || frame.frameHeight == 0) {
    return decoded;
  }

  if (frame.dataOffset >= bytes_.size()) {
    throw std::runtime_error("SHP frame data offset is out of range");
  }

  if (static_cast<std::size_t>(frame.frameX) + frame.frameWidth > fullWidth_ ||
      static_cast<std::size_t>(frame.frameY) + frame.frameHeight > fullHeight_) {
    throw std::runtime_error("SHP frame rectangle exceeds its canvas");
  }
  if (frame.dataOffset < 8 + frames_.size() * 24) {
    throw std::runtime_error("SHP frame data overlaps the frame table");
  }

  std::size_t inputOffset = frame.dataOffset;
  // Compression occupies one byte; the remaining header bytes may be padding.
  const auto frameType = frame.flags & 0xffU;
  for (std::uint16_t y = 0; y < frame.frameHeight; ++y) {
    const std::size_t dst = static_cast<std::size_t>(frame.frameY + y) * fullWidth_ + frame.frameX;
    if (frameType == kFrameTypeRaw0 || frameType == kFrameTypeRaw1) {
      if (frame.frameWidth > bytes_.size() - inputOffset) {
        throw std::runtime_error("SHP opaque frame overruns file");
      }
      std::copy_n(bytes_.data() + inputOffset, frame.frameWidth, decoded.indices.begin() + dst);
      inputOffset += frame.frameWidth;
    } else if (frameType == kFrameTypeRle3) {
      if (bytes_.size() - inputOffset < 2) {
        throw std::runtime_error("SHP RLE line header overruns file");
      }
      const auto length = readU16(bytes_, inputOffset);
      if (length < 2 || length > bytes_.size() - inputOffset) {
        throw std::runtime_error("Invalid SHP RLE line length");
      }
      const std::size_t lineEnd = inputOffset + length;
      inputOffset += 2;
      std::size_t x = 0;
      while (inputOffset < lineEnd) {
        const auto value = bytes_[inputOffset++];
        if (value == 0) {
          if (inputOffset == lineEnd) {
            throw std::runtime_error("Truncated SHP zero run");
          }
          const auto count = bytes_[inputOffset++];
          if (count > frame.frameWidth - x && inputOffset != lineEnd) {
            throw std::runtime_error("SHP zero run exceeds row width");
          }
          // Existing assets pad the final transparent run beyond odd row widths.
          // It contains no visible pixels and must never write outside the row.
          x += std::min<std::size_t>(count, frame.frameWidth - x);
        } else {
          if (x >= frame.frameWidth) {
            throw std::runtime_error("SHP RLE pixels exceed row width");
          }
          decoded.indices[dst + x++] = value;
        }
      }
    } else {
      throw std::runtime_error("Unsupported SHP frame type: " + std::to_string(frame.flags));
    }

    // Derive visible spans from pixels for both raw and compressed rows.
    // In particular, a four-byte encoded row can contain two visible pixels.
    auto& span = decoded.rowSpans[y];
    span.y = static_cast<std::uint16_t>(frame.frameY + y);
    span.startX = span.endX = frame.frameX;
    std::size_t x = 0;
    while (x < frame.frameWidth) {
      if (decoded.indices[dst + x] == 0) {
        ++x;
        continue;
      }
      const auto startX = static_cast<std::uint16_t>(frame.frameX + x);
      while (x < frame.frameWidth && decoded.indices[dst + x] != 0) ++x;
      const auto endX = static_cast<std::uint16_t>(frame.frameX + x - 1);
      if (!span.hasPixels) span.startX = startX;
      span.hasPixels = true;
      span.endX = endX;
      span.segments.push_back({startX, endX});
    }
  }

  return decoded;
}

std::vector<std::uint8_t> ShpTsFile::decodeFrameRgba(const std::size_t frameIndex, const Palette& palette) const {
  return decodeFrameRgba(frameIndex, palette, PaletteSampleOptions{});
}

std::vector<std::uint8_t> ShpTsFile::decodeFrameRgba(const std::size_t frameIndex,
                                                     const Palette& palette,
                                                     const PaletteSampleOptions& sampleOptions) const {
  const auto frame = decodeFrame(frameIndex);
  std::vector<std::uint8_t> rgba;
  rgba.reserve(frame.indices.size() * 4);

  for (const auto index : frame.indices) {
    const auto color = palette.sample(index, sampleOptions);
    rgba.push_back(color.r);
    rgba.push_back(color.g);
    rgba.push_back(color.b);
    rgba.push_back(color.a);
  }

  return rgba;
}

std::uint16_t ShpTsFile::readU16(const std::vector<std::uint8_t>& bytes, const std::size_t offset) {
  return static_cast<std::uint16_t>(bytes.at(offset) | (bytes.at(offset + 1) << 8));
}

std::uint32_t ShpTsFile::readU32(const std::vector<std::uint8_t>& bytes, const std::size_t offset) {
  return static_cast<std::uint32_t>(bytes.at(offset)) |
         (static_cast<std::uint32_t>(bytes.at(offset + 1)) << 8) |
         (static_cast<std::uint32_t>(bytes.at(offset + 2)) << 16) |
         (static_cast<std::uint32_t>(bytes.at(offset + 3)) << 24);
}
