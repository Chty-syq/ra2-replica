#include "rules_colors.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {
std::string trim(std::string value) {
  value.erase(value.begin(),
              std::find_if(value.begin(), value.end(), [](const unsigned char ch) {
                return !std::isspace(ch);
              }));
  value.erase(std::find_if(value.rbegin(), value.rend(), [](const unsigned char ch) {
                return !std::isspace(ch);
              }).base(),
              value.end());
  return value;
}

bool iequals(const std::string& lhs, const std::string& rhs) {
  return lhs.size() == rhs.size() &&
         std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](const unsigned char a, const unsigned char b) {
           return std::tolower(a) == std::tolower(b);
         });
}

std::optional<std::array<int, 3>> parseHsvTriplet(const std::string& value) {
  std::array<int, 3> hsv{};
  std::stringstream stream(value);
  std::string token;

  for (int i = 0; i < 3; ++i) {
    if (!std::getline(stream, token, ',')) {
      return std::nullopt;
    }

    try {
      hsv[static_cast<std::size_t>(i)] = std::clamp(std::stoi(trim(token)), 0, 255);
    } catch (...) {
      return std::nullopt;
    }
  }

  return hsv;
}

HsvColor toHsvColor(const std::array<int, 3>& hsv) {
  return HsvColor{
    static_cast<std::uint8_t>(hsv[0]),
    static_cast<std::uint8_t>(hsv[1]),
    static_cast<std::uint8_t>(hsv[2])
  };
}

Rgba quantizeToRgb565(const Rgba color) {
  const auto quantize5 = [](const std::uint8_t channel) {
    const int value5 = channel / 8;
    return static_cast<std::uint8_t>((value5 * 255) / 31);
  };
  const auto quantize6 = [](const std::uint8_t channel) {
    const int value6 = channel / 4;
    return static_cast<std::uint8_t>((value6 * 255) / 63);
  };

  return Rgba{
    quantize5(color.r),
    quantize6(color.g),
    quantize5(color.b),
    color.a
  };
}

Rgba hsvToRgb565(const std::array<int, 3>& hsv) {
  const float hue = (static_cast<float>(hsv[0]) / 255.0f) * 360.0f;
  const float saturation = static_cast<float>(hsv[1]) / 255.0f;
  const float value = static_cast<float>(hsv[2]) / 255.0f;

  const float chroma = value * saturation;
  const float hueSection = std::fmod(hue / 60.0f, 6.0f);
  const float x = chroma * (1.0f - std::fabs(std::fmod(hueSection, 2.0f) - 1.0f));
  const float match = value - chroma;

  float red = 0.0f;
  float green = 0.0f;
  float blue = 0.0f;

  if (hueSection >= 0.0f && hueSection < 1.0f) {
    red = chroma;
    green = x;
  } else if (hueSection < 2.0f) {
    red = x;
    green = chroma;
  } else if (hueSection < 3.0f) {
    green = chroma;
    blue = x;
  } else if (hueSection < 4.0f) {
    green = x;
    blue = chroma;
  } else if (hueSection < 5.0f) {
    red = x;
    blue = chroma;
  } else {
    red = chroma;
    blue = x;
  }

  const auto toByte = [&](const float channel) {
    return static_cast<std::uint8_t>(std::clamp(std::lround((channel + match) * 255.0f), 0L, 255L));
  };

  return quantizeToRgb565(Rgba{toByte(red), toByte(green), toByte(blue), 255});
}
}  // namespace

HouseColorSet loadHouseColors(const std::filesystem::path& rulesIniPath) {
  std::ifstream input(rulesIniPath);
  if (!input) {
    throw std::runtime_error("Failed to open rules.ini: " + rulesIniPath.string());
  }

  bool inColorsSection = false;
  std::string line;
  HouseColorSet result;

  while (std::getline(input, line)) {
    const auto comment = line.find(';');
    if (comment != std::string::npos) {
      line = line.substr(0, comment);
    }
    line = trim(line);
    if (line.empty()) {
      continue;
    }

    if (line.front() == '[' && line.back() == ']') {
      const auto section = line.substr(1, line.size() - 2);
      if (inColorsSection) {
        break;
      }
      inColorsSection = iequals(section, "Colors");
      continue;
    }

    if (!inColorsSection) {
      continue;
    }

    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }

    const auto key = trim(line.substr(0, eq));
    const auto value = trim(line.substr(eq + 1));
    const auto hsv = parseHsvTriplet(value);
    if (!hsv.has_value()) {
      continue;
    }

    result.colors.push_back(HouseColorEntry{key, toHsvColor(*hsv), hsvToRgb565(*hsv)});
    if (iequals(key, "DarkBlue")) {
      result.defaultIndex = result.colors.size() - 1;
    }
  }

  if (result.colors.empty()) {
    result.colors.push_back(HouseColorEntry{
      "DarkBlue",
      HsvColor{153, 214, 212},
      Rgba{68, 126, 255, 255}
    });
    result.defaultIndex = 0;
  }

  return result;
}

std::unordered_map<std::string, int> loadObjectStrengths(const std::filesystem::path& rulesIniPath) {
  std::ifstream input(rulesIniPath);
  if (!input) {
    throw std::runtime_error("Failed to open rules.ini: " + rulesIniPath.string());
  }

  std::unordered_map<std::string, int> strengths;
  std::string currentSection;
  std::string line;
  while (std::getline(input, line)) {
    const auto comment = line.find(';');
    if (comment != std::string::npos) {
      line = line.substr(0, comment);
    }
    line = trim(line);
    if (line.empty()) {
      continue;
    }

    if (line.front() == '[' && line.back() == ']') {
      currentSection = trim(line.substr(1, line.size() - 2));
      continue;
    }

    if (currentSection.empty()) {
      continue;
    }

    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }

    const auto key = trim(line.substr(0, eq));
    const auto value = trim(line.substr(eq + 1));
    if (!iequals(key, "Strength")) {
      continue;
    }

    try {
      const int strength = std::stoi(value);
      if (strength > 0) {
        strengths[currentSection] = strength;
      }
    } catch (...) {
    }
  }

  return strengths;
}
