#include "vpl_file.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
template <typename T>
T readStruct(const std::vector<std::uint8_t>& bytes, const std::size_t offset) {
  if (offset + sizeof(T) > bytes.size()) {
    throw std::runtime_error("Unexpected end of VPL file");
  }

  T value{};
  std::memcpy(&value, bytes.data() + offset, sizeof(T));
  return value;
}
}  // namespace

VplFile VplFile::load(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Failed to open VPL: " + path.string());
  }

  const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                        std::istreambuf_iterator<char>());

  if (bytes.size() < sizeof(VplHeader) + 256 * 3) {
    throw std::runtime_error("VPL file is too small: " + path.string());
  }

  VplFile file;
  file.header_ = readStruct<VplHeader>(bytes, 0);
  if (file.header_.sectionCount == 0 || file.header_.sectionCount > file.sections_.size()) {
    throw std::runtime_error("Unsupported VPL section count: " + std::to_string(file.header_.sectionCount));
  }

  const std::size_t paletteBytes = 256 * 3;
  const std::size_t sectionBytes = static_cast<std::size_t>(file.header_.sectionCount) * 256;
  const std::size_t expectedSize = sizeof(VplHeader) + paletteBytes + sectionBytes;
  if (bytes.size() < expectedSize) {
    throw std::runtime_error("Truncated VPL file: " + path.string());
  }

  const std::size_t sectionsOffset = sizeof(VplHeader) + paletteBytes;
  std::memcpy(file.sections_.data(), bytes.data() + sectionsOffset, sectionBytes);
  return file;
}

std::uint8_t VplFile::mapIndex(const std::size_t sectionIndex, const std::uint8_t colorIndex) const {
  if (sectionIndex >= sectionCount()) {
    throw std::out_of_range("VPL section index is out of range");
  }
  return sections_[sectionIndex][colorIndex];
}
