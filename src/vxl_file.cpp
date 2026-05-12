#include "vxl_file.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
#pragma pack(push, 1)
struct RawVxlColor {
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
};

struct RawVxlHeader {
  char signature[16];
  std::uint32_t reserved;
  std::uint32_t limbCount;
  std::uint32_t duplicateLimbCount;
  std::uint32_t bodySize;
  std::uint8_t remapStart;
  std::uint8_t remapEnd;
  RawVxlColor internalPalette[256];
};

struct RawVxlSectionHeader {
  char name[16];
  std::int32_t limbNumber;
  std::uint32_t reserved1;
  std::uint32_t reserved2;
};

struct RawVxlSectionTailer {
  std::uint32_t spanStartOffset;
  std::uint32_t spanEndOffset;
  std::uint32_t spanDataOffset;
  float scale;
  VxlMatrix3x4 matrix;
  float minBounds[3];
  float maxBounds[3];
  std::uint8_t xsize;
  std::uint8_t ysize;
  std::uint8_t zsize;
  std::uint8_t normalType;
};
#pragma pack(pop)

std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("Failed to open file: " + path.string());
  }

  stream.seekg(0, std::ios::end);
  const auto size = static_cast<std::size_t>(stream.tellg());
  stream.seekg(0, std::ios::beg);

  std::vector<std::uint8_t> data(size);
  if (size > 0) {
    stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
  }

  if (!stream.good() && !stream.eof()) {
    throw std::runtime_error("Failed to read file: " + path.string());
  }

  return data;
}

template <typename T>
T readStruct(const std::vector<std::uint8_t>& buffer, const std::size_t offset) {
  if (offset + sizeof(T) > buffer.size()) {
    throw std::runtime_error("Unexpected end of file while reading binary structure");
  }

  T value{};
  std::memcpy(&value, buffer.data() + offset, sizeof(T));
  return value;
}

std::string trimZeroTerminated(const char* raw, const std::size_t size) {
  std::size_t length = 0;
  while (length < size && raw[length] != '\0') {
    ++length;
  }
  return std::string(raw, raw + length);
}

void requireRange(const std::vector<std::uint8_t>& buffer, const std::size_t offset, const std::size_t size) {
  if (offset + size > buffer.size()) {
    throw std::runtime_error("Binary file contains an out-of-range data block");
  }
}
}  // namespace

VxlSection::VxlSection(VxlSectionHeader header, VxlSectionTailer tailer, std::vector<VoxelSample> voxels)
  : header_(std::move(header)), tailer_(tailer), voxels_(std::move(voxels)) {}

const VoxelSample& VxlSection::voxel(const std::size_t x, const std::size_t y, const std::size_t z) const noexcept {
  static const VoxelSample kEmpty{};
  if (x >= tailer_.xsize || y >= tailer_.ysize || z >= tailer_.zsize) {
    return kEmpty;
  }

  const auto index = (y * static_cast<std::size_t>(tailer_.xsize) + x) * static_cast<std::size_t>(tailer_.zsize) + z;
  if (index >= voxels_.size()) {
    return kEmpty;
  }
  return voxels_[index];
}

VxlFile VxlFile::load(const std::filesystem::path& path) {
  const auto data = readBinaryFile(path);
  const auto header = readStruct<RawVxlHeader>(data, 0);

  VxlFile file;
  file.palette_.remapStart = header.remapStart;
  file.palette_.remapEnd = header.remapEnd;
  for (std::size_t index = 0; index < file.palette_.entries.size(); ++index) {
    // VXL 内部调色板是 6-bit 颜色，参考项目也是直接左移 2 位恢复到 8-bit 区间。
    file.palette_.entries[index] = VxlColor{
      static_cast<std::uint8_t>(header.internalPalette[index].r << 2),
      static_cast<std::uint8_t>(header.internalPalette[index].g << 2),
      static_cast<std::uint8_t>(header.internalPalette[index].b << 2)
    };
  }

  const auto sectionCount = static_cast<std::size_t>(header.limbCount);
  const std::size_t headersOffset = sizeof(RawVxlHeader);
  const std::size_t bodyOffset = headersOffset + sectionCount * sizeof(RawVxlSectionHeader);
  const std::size_t tailersOffset = bodyOffset + static_cast<std::size_t>(header.bodySize);
  std::array<std::size_t, 256> normalTypeHistogram{};

  requireRange(data, headersOffset, sectionCount * sizeof(RawVxlSectionHeader));
  requireRange(data, tailersOffset, sectionCount * sizeof(RawVxlSectionTailer));

  file.sections_.reserve(sectionCount);
  for (std::size_t sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex) {
    const auto rawHeader = readStruct<RawVxlSectionHeader>(data, headersOffset + sectionIndex * sizeof(RawVxlSectionHeader));
    const auto rawTailer = readStruct<RawVxlSectionTailer>(data, tailersOffset + sectionIndex * sizeof(RawVxlSectionTailer));

    VxlSectionHeader sectionHeader;
    sectionHeader.name = trimZeroTerminated(rawHeader.name, sizeof(rawHeader.name));
    sectionHeader.limbNumber = rawHeader.limbNumber;

    VxlSectionTailer sectionTailer;
    sectionTailer.scale = rawTailer.scale;
    sectionTailer.minBounds = {rawTailer.minBounds[0], rawTailer.minBounds[1], rawTailer.minBounds[2]};
    sectionTailer.maxBounds = {rawTailer.maxBounds[0], rawTailer.maxBounds[1], rawTailer.maxBounds[2]};
    sectionTailer.xsize = rawTailer.xsize;
    sectionTailer.ysize = rawTailer.ysize;
    sectionTailer.zsize = rawTailer.zsize;
    sectionTailer.normalType = rawTailer.normalType;
    ++normalTypeHistogram[sectionTailer.normalType];

    const auto spanCount = static_cast<std::size_t>(sectionTailer.xsize) * static_cast<std::size_t>(sectionTailer.ysize);
    const auto spanStartOffset = bodyOffset + static_cast<std::size_t>(rawTailer.spanStartOffset);
    const auto spanEndOffset = bodyOffset + static_cast<std::size_t>(rawTailer.spanEndOffset);
    const auto spanDataOffset = bodyOffset + static_cast<std::size_t>(rawTailer.spanDataOffset);

    requireRange(data, spanStartOffset, spanCount * sizeof(std::uint32_t));
    requireRange(data, spanEndOffset, spanCount * sizeof(std::uint32_t));
    requireRange(data, spanDataOffset, 1);

    std::vector<std::uint32_t> spanStarts(spanCount);
    std::vector<std::uint32_t> spanEnds(spanCount);
    std::memcpy(spanStarts.data(), data.data() + spanStartOffset, spanCount * sizeof(std::uint32_t));
    std::memcpy(spanEnds.data(), data.data() + spanEndOffset, spanCount * sizeof(std::uint32_t));

    std::vector<VoxelSample> voxels(spanCount * static_cast<std::size_t>(sectionTailer.zsize));
    for (std::size_t spanIndex = 0; spanIndex < spanCount; ++spanIndex) {
      if (spanStarts[spanIndex] == std::numeric_limits<std::uint32_t>::max() ||
          spanEnds[spanIndex] == std::numeric_limits<std::uint32_t>::max()) {
        continue;
      }

      const auto currentSpanOffset = spanDataOffset + static_cast<std::size_t>(spanStarts[spanIndex]);
      const auto currentSpanEnd = spanDataOffset + static_cast<std::size_t>(spanEnds[spanIndex]);
      requireRange(data, currentSpanOffset, 1);
      requireRange(data, currentSpanEnd, 1);

      std::size_t zIndex = 0;
      std::size_t cursor = currentSpanOffset;
      do {
        requireRange(data, cursor, 3);
        const auto skipCount = data[cursor++];
        const auto voxelCount = data[cursor++];
        zIndex += skipCount;

        const auto spanBaseIndex = spanIndex * static_cast<std::size_t>(sectionTailer.zsize);
        for (std::size_t voxelOffset = 0; voxelOffset < voxelCount; ++voxelOffset) {
          requireRange(data, cursor, 2);
          if (zIndex + voxelOffset < sectionTailer.zsize) {
            voxels[spanBaseIndex + zIndex + voxelOffset] = VoxelSample{data[cursor], data[cursor + 1]};
          }
          cursor += 2;
        }
        zIndex += voxelCount;

        requireRange(data, cursor, 1);
        const auto voxelEnd = data[cursor++];
        (void)voxelEnd;
      } while (cursor <= currentSpanEnd);
    }

    file.sections_.emplace_back(std::move(sectionHeader), sectionTailer, std::move(voxels));
  }

  std::size_t maxCount = 0;
  std::uint8_t dominantNormalType = 4;
  for (std::size_t index = 0; index < normalTypeHistogram.size(); ++index) {
    if (normalTypeHistogram[index] > maxCount) {
      maxCount = normalTypeHistogram[index];
      dominantNormalType = static_cast<std::uint8_t>(index);
    }
  }
  file.normalTypeIndex_ = dominantNormalType;

  return file;
}

const VxlSection& VxlFile::section(const std::size_t index) const {
  if (index >= sections_.size()) {
    throw std::out_of_range("VXL section index is out of range");
  }
  return sections_[index];
}

HvaFile HvaFile::load(const std::filesystem::path& path) {
  const auto data = readBinaryFile(path);
  if (data.size() < 24) {
    throw std::runtime_error("HVA file is too small: " + path.string());
  }

  HvaFile file;
  std::size_t offset = 16;

  std::uint32_t frameCount = 0;
  std::uint32_t sectionCount = 0;
  std::memcpy(&frameCount, data.data() + offset, sizeof(frameCount));
  offset += sizeof(frameCount);
  std::memcpy(&sectionCount, data.data() + offset, sizeof(sectionCount));
  offset += sizeof(sectionCount);

  file.frameCount_ = frameCount;
  file.sectionCount_ = sectionCount;
  file.sectionNames_.reserve(sectionCount);

  requireRange(data, offset, static_cast<std::size_t>(sectionCount) * 16);
  for (std::size_t sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex) {
    file.sectionNames_.push_back(trimZeroTerminated(reinterpret_cast<const char*>(data.data() + offset), 16));
    offset += 16;
  }

  const auto matrixCount = static_cast<std::size_t>(frameCount) * static_cast<std::size_t>(sectionCount);
  requireRange(data, offset, matrixCount * sizeof(VxlMatrix3x4));
  file.matrices_.resize(matrixCount);
  std::memcpy(file.matrices_.data(), data.data() + offset, matrixCount * sizeof(VxlMatrix3x4));
  return file;
}

const VxlMatrix3x4& HvaFile::matrix(const std::size_t frame, const std::size_t section) const {
  if (frame >= frameCount_ || section >= sectionCount_) {
    throw std::out_of_range("HVA matrix index is out of range");
  }
  return matrices_[frame * sectionCount_ + section];
}
