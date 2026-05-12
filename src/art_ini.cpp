#include "art_ini.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {
std::string trim(std::string value) {
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) { return !std::isspace(ch); }));
  value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), value.end());
  return value;
}

bool iequals(const std::string& lhs, const std::string& rhs) {
  return lhs.size() == rhs.size() &&
         std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](unsigned char a, unsigned char b) {
           return std::tolower(a) == std::tolower(b);
         });
}

bool parseBool(const std::string& value) {
  return iequals(value, "yes") || iequals(value, "true") || value == "1";
}

std::optional<FoundationSize> parseFoundation(const std::string& value) {
  const auto x = value.find('x');
  if (x == std::string::npos) {
    return std::nullopt;
  }

  try {
    return FoundationSize{
      std::stoi(value.substr(0, x)),
      std::stoi(value.substr(x + 1))
    };
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<int> parseInt(const std::string& value) {
  try {
    return std::stoi(value);
  } catch (...) {
    return std::nullopt;
  }
}
}

ArtIni ArtIni::load(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Failed to open art.ini: " + path.string());
  }

  ArtIni ini;
  ini.path_ = path;
  return ini;
}

ArtDefinition ArtIni::building(std::string id) const {
  std::ifstream input(path_);
  if (!input) {
    throw std::runtime_error("Failed to open art.ini: " + path_.string());
  }

  ArtDefinition definition;
  definition.id = id;
  definition.image = id;

  bool inSection = false;
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
      const auto section = line.substr(1, line.size() - 2);
      if (inSection) {
        break;
      }
      inSection = iequals(section, id);
      continue;
    }

    if (!inSection) {
      continue;
    }

    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }

    const auto key = trim(line.substr(0, eq));
    const auto value = trim(line.substr(eq + 1));

    if (iequals(key, "Image")) {
      definition.image = value;
    } else if (iequals(key, "Foundation")) {
      definition.foundation = parseFoundation(value);
    } else if (iequals(key, "IdleAnim")) {
      definition.idleAnim = value;
    } else if (iequals(key, "ActiveAnim")) {
      definition.activeAnim = value;
    } else if (iequals(key, "ActiveAnimTwo")) {
      definition.activeAnimTwo = value;
    } else if (iequals(key, "ActiveAnimThree")) {
      definition.activeAnimThree = value;
    } else if (iequals(key, "ActiveAnimFour")) {
      definition.activeAnimFour = value;
    } else if (iequals(key, "SpecialAnim")) {
      definition.specialAnim = value;
    } else if (iequals(key, "SpecialAnimTwo")) {
      definition.specialAnimTwo = value;
    } else if (iequals(key, "SpecialAnimThree")) {
      definition.specialAnimThree = value;
    } else if (iequals(key, "SpecialAnimFour")) {
      definition.specialAnimFour = value;
    } else if (iequals(key, "SuperAnim")) {
      definition.superAnim = value;
    } else if (iequals(key, "SuperAnimTwo")) {
      definition.superAnimTwo = value;
    } else if (iequals(key, "SuperAnimThree")) {
      definition.superAnimThree = value;
    } else if (iequals(key, "SuperAnimFour")) {
      definition.superAnimFour = value;
    } else if (iequals(key, "ProductionAnim")) {
      definition.productionAnim = value;
    } else if (iequals(key, "Height")) {
      definition.height = std::stof(value);
    } else if (iequals(key, "OccupyHeight")) {
      definition.occupyHeight = std::stof(value);
    } else if (iequals(key, "Buildup")) {
      definition.buildup = value;
    } else if (iequals(key, "BibShape")) {
      definition.bibShape = value;
    } else if (iequals(key, "NewTheater")) {
      definition.newTheater = parseBool(value);
    } else if (iequals(key, "Remapable")) {
      definition.remapable = parseBool(value);
    } else if (iequals(key, "TerrainPalette")) {
      definition.terrainPalette = parseBool(value);
    } else if (iequals(key, "Shadow")) {
      definition.shadow = parseBool(value);
    } else if (iequals(key, "ActiveAnimPowered")) {
      definition.activeAnimPowered = parseBool(value);
    }
  }

  return definition;
}

AnimationDefinition ArtIni::animation(std::string id) const {
  std::ifstream input(path_);
  if (!input) {
    throw std::runtime_error("Failed to open art.ini: " + path_.string());
  }

  AnimationDefinition definition;
  definition.id = id;
  definition.image = id;

  bool inSection = false;
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
      const auto section = line.substr(1, line.size() - 2);
      if (inSection) {
        break;
      }
      inSection = iequals(section, id);
      continue;
    }

    if (!inSection) {
      continue;
    }

    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }

    const auto key = trim(line.substr(0, eq));
    const auto value = trim(line.substr(eq + 1));

    if (iequals(key, "Image")) {
      definition.image = value;
    } else if (iequals(key, "Start")) {
      definition.start = parseInt(value);
    } else if (iequals(key, "End")) {
      definition.end = parseInt(value);
    } else if (iequals(key, "LoopStart")) {
      definition.loopStart = parseInt(value);
    } else if (iequals(key, "LoopEnd")) {
      definition.loopEnd = parseInt(value);
    } else if (iequals(key, "Rate")) {
      definition.rate = parseInt(value);
    } else if (iequals(key, "NewTheater")) {
      definition.newTheater = parseBool(value);
    } else if (iequals(key, "Shadow")) {
      definition.shadow = parseBool(value);
    }
  }

  return definition;
}
