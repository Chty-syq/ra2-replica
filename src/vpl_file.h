#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>

struct VplHeader {
  std::uint32_t remapStart = 0;
  std::uint32_t remapEnd = 0;
  std::uint32_t sectionCount = 0;
  std::uint32_t reserved = 0;
};

class VplFile {
public:
  static VplFile load(const std::filesystem::path& path);

  [[nodiscard]] const VplHeader& header() const noexcept { return header_; }
  [[nodiscard]] std::size_t sectionCount() const noexcept { return static_cast<std::size_t>(header_.sectionCount); }
  [[nodiscard]] std::uint8_t mapIndex(std::size_t sectionIndex, std::uint8_t colorIndex) const;

private:
  VplHeader header_{};
  std::array<std::array<std::uint8_t, 256>, 32> sections_{};
};
