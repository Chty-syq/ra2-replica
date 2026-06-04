#pragma once

#include "demo_style.h"

#include <filesystem>
#include <string>
#include <unordered_map>

[[nodiscard]] HouseColorSet loadHouseColors(const std::filesystem::path& rulesIniPath);
[[nodiscard]] std::unordered_map<std::string, int> loadObjectStrengths(const std::filesystem::path& rulesIniPath);
