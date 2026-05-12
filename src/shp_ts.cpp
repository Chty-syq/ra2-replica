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

  std::size_t inputOffset = frame.dataOffset;

  auto writeOpaqueLine = [&](std::uint16_t y) {
    const std::size_t dst = static_cast<std::size_t>(frame.frameY + y) * fullWidth_ + frame.frameX;
    const std::size_t count = frame.frameWidth;
    if (inputOffset + count > bytes_.size()) {
      throw std::runtime_error("SHP opaque frame overruns file");
    }
    std::copy_n(bytes_.data() + inputOffset, count, decoded.indices.begin() + dst);
    inputOffset += count;
    decoded.rowSpans[y] = DecodedFrame::RowSpan{
      static_cast<std::uint16_t>(frame.frameY + y),
      frame.frameX,
      static_cast<std::uint16_t>(frame.frameX + frame.frameWidth - 1),
      true,
      {DecodedFrame::RowSegment{frame.frameX, static_cast<std::uint16_t>(frame.frameX + frame.frameWidth - 1)}}
    };
  };

  auto writeTransparentLine = [&](std::uint16_t y, const bool useRle) {
    if (inputOffset + 2 > bytes_.size()) {
      throw std::runtime_error("SHP RLE line header overruns file");
    }

    const auto lineStart = inputOffset;
    const auto compressedLength = readU16(bytes_, inputOffset);
    inputOffset += 2;

    if (compressedLength < 2 || lineStart + compressedLength > bytes_.size()) {
      throw std::runtime_error("Invalid SHP RLE line length");
    }

    if (compressedLength == 4) {
      inputOffset += 2;
      decoded.rowSpans[y] = DecodedFrame::RowSpan{
        static_cast<std::uint16_t>(frame.frameY + y),
        frame.frameX,
        frame.frameX,
        false,
        {}
      };
      return;
    }

    const std::size_t payloadLength = compressedLength - 2;
    if (inputOffset + payloadLength > bytes_.size()) {
      throw std::runtime_error("SHP RLE payload overruns file");
    }

    std::vector<std::uint8_t> payload(bytes_.begin() + static_cast<std::ptrdiff_t>(inputOffset),
                                      bytes_.begin() + static_cast<std::ptrdiff_t>(inputOffset + payloadLength));
    inputOffset += payloadLength;

    std::size_t x = 0;
    std::size_t payloadBegin = 0;
    std::size_t payloadEnd = payload.size();

    if (payload.size() >= 2 && payload.front() == 0) {
      x = std::min<std::size_t>(payload[1], frame.frameWidth);
      payloadBegin = 2;
    }
    if (payload.size() >= 2 && payload[payload.size() - 2] == 0) {
      payloadEnd = payload.size() - 2;
    }

    const std::size_t dstLine = static_cast<std::size_t>(frame.frameY + y) * fullWidth_ + frame.frameX;
    std::size_t firstOpaqueX = frame.frameWidth;
    std::size_t lastOpaqueX = 0;
    bool hasPixels = false;
    bool segmentOpen = false;
    std::size_t segmentStartX = 0;
    std::vector<DecodedFrame::RowSegment> segments;

    for (std::size_t i = payloadBegin; i < payloadEnd && x < frame.frameWidth; ++i) {
      const auto value = payload[i];
      if (useRle && value == 0 && i + 1 < payloadEnd && payload[i + 1] > 0) {
        if (segmentOpen) {
          segments.push_back(DecodedFrame::RowSegment{
            static_cast<std::uint16_t>(frame.frameX + segmentStartX),
            static_cast<std::uint16_t>(frame.frameX + x - 1)
          });
          segmentOpen = false;
        }
        x += std::min<std::size_t>(payload[i + 1], frame.frameWidth - x);
        ++i;
        continue;
      }

      decoded.indices[dstLine + x] = value;
      firstOpaqueX = std::min(firstOpaqueX, x);
      lastOpaqueX = x;
      hasPixels = true;
      if (!segmentOpen) {
        segmentStartX = x;
        segmentOpen = true;
      }
      ++x;
    }

    if (segmentOpen) {
      segments.push_back(DecodedFrame::RowSegment{
        static_cast<std::uint16_t>(frame.frameX + segmentStartX),
        static_cast<std::uint16_t>(frame.frameX + x - 1)
      });
    }

    decoded.rowSpans[y] = DecodedFrame::RowSpan{
      static_cast<std::uint16_t>(frame.frameY + y),
      static_cast<std::uint16_t>(hasPixels ? frame.frameX + firstOpaqueX : frame.frameX),
      static_cast<std::uint16_t>(hasPixels ? frame.frameX + lastOpaqueX : frame.frameX),
      hasPixels,
      std::move(segments)
    };
  };

  for (std::uint16_t y = 0; y < frame.frameHeight; ++y) {
    switch (frame.flags) {
      case kFrameTypeRaw0:
      case kFrameTypeRaw1:
        writeOpaqueLine(y);
        break;
      case kFrameTypeRle3:
        writeTransparentLine(y, true);
        break;
      default:
        throw std::runtime_error("Unsupported SHP frame type: " + std::to_string(frame.flags));
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
