#pragma once

#include "demo_style.h"
#include "config/ini_document.h"

#include <filesystem>
#include <string>
#include <unordered_map>

[[nodiscard]] HouseColorSet loadHouseColors(const std::filesystem::path& rulesIniPath);
[[nodiscard]] HouseColorSet loadHouseColors(const ra2::IniDocument& rules);
[[nodiscard]] std::unordered_map<std::string, int> loadObjectStrengths(const std::filesystem::path& rulesIniPath);
