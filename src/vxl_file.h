#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// VXL 里的单个体素。color=0 表示该位置为空。
struct VoxelSample {
  std::uint8_t color = 0;
  std::uint8_t normal = 0;
};

// VXL 文件头里自带的 256 色调色板条目。
struct VxlColor {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
};

// HVA 使用的 3x4 矩阵。这里按文件里的原始布局保存，渲染时再转换成 4x4。
struct VxlMatrix3x4 {
  std::array<float, 12> values{};

  [[nodiscard]] float at(const std::size_t row, const std::size_t column) const {
    return values[row * 4 + column];
  }
};

struct VxlPalette {
  std::array<VxlColor, 256> entries{};
  std::uint8_t remapStart = 0xFF;
  std::uint8_t remapEnd = 0xFF;
};

struct VxlSectionHeader {
  std::string name;
  std::int32_t limbNumber = 0;
};

struct VxlSectionTailer {
  float scale = 1.0f;
  std::array<float, 3> minBounds{};
  std::array<float, 3> maxBounds{};
  std::uint8_t xsize = 0;
  std::uint8_t ysize = 0;
  std::uint8_t zsize = 0;
  std::uint8_t normalType = 0;
};

// 一个 section 就是一块独立的体素部件。
class VxlSection {
public:
  VxlSection() = default;
  VxlSection(VxlSectionHeader header, VxlSectionTailer tailer, std::vector<VoxelSample> voxels);

  [[nodiscard]] const VxlSectionHeader& header() const noexcept { return header_; }
  [[nodiscard]] const VxlSectionTailer& tailer() const noexcept { return tailer_; }

  [[nodiscard]] const VoxelSample& voxel(std::size_t x, std::size_t y, std::size_t z) const noexcept;

private:
  VxlSectionHeader header_;
  VxlSectionTailer tailer_;
  std::vector<VoxelSample> voxels_;
};

class VxlFile {
public:
  static VxlFile load(const std::filesystem::path& path);

  [[nodiscard]] const VxlPalette& palette() const noexcept { return palette_; }
  [[nodiscard]] std::size_t sectionCount() const noexcept { return sections_.size(); }
  [[nodiscard]] const VxlSection& section(const std::size_t index) const;
  [[nodiscard]] std::uint8_t normalTypeIndex() const noexcept { return normalTypeIndex_; }

private:
  VxlPalette palette_{};
  std::vector<VxlSection> sections_;
  std::uint8_t normalTypeIndex_ = 4;
};

class HvaFile {
public:
  static HvaFile load(const std::filesystem::path& path);

  [[nodiscard]] std::size_t frameCount() const noexcept { return frameCount_; }
  [[nodiscard]] std::size_t sectionCount() const noexcept { return sectionCount_; }
  [[nodiscard]] const std::vector<std::string>& sectionNames() const noexcept { return sectionNames_; }
  [[nodiscard]] const VxlMatrix3x4& matrix(std::size_t frame, std::size_t section) const;

private:
  std::size_t frameCount_ = 0;
  std::size_t sectionCount_ = 0;
  std::vector<std::string> sectionNames_;
  std::vector<VxlMatrix3x4> matrices_;
};
