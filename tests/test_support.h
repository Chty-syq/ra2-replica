#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

inline void require(const bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

template <typename Exception = std::runtime_error, typename Function>
void requireRejected(Function function, const std::string& message) {
  try {
    function();
  } catch (const Exception&) {
    return;
  }
  throw std::runtime_error(message);
}

inline void put16(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint16_t value) {
  bytes.at(offset) = static_cast<std::uint8_t>(value);
  bytes.at(offset + 1) = static_cast<std::uint8_t>(value >> 8);
}

inline void put32(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint32_t value) {
  put16(bytes, offset, static_cast<std::uint16_t>(value));
  put16(bytes, offset + 2, static_cast<std::uint16_t>(value >> 16));
}

class BinaryFixture {
public:
  explicit BinaryFixture(const std::vector<std::uint8_t>& bytes) {
    static unsigned sequence = 0;
    path_ = std::filesystem::temp_directory_path() /
      ("ra2-test-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) +
       "-" + std::to_string(sequence++) + ".bin");
    std::ofstream file(path_, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(file), "Could not write test fixture");
  }
  ~BinaryFixture() {
    std::error_code error;
    std::filesystem::remove(path_, error);
  }
  BinaryFixture(const BinaryFixture&) = delete;
  BinaryFixture& operator=(const BinaryFixture&) = delete;
  const std::filesystem::path& path() const { return path_; }
private:
  std::filesystem::path path_;
};
