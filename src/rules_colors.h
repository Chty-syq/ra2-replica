#pragma once

#include "demo_style.h"

#include <filesystem>

[[nodiscard]] HouseColorSet loadHouseColors(const std::filesystem::path& rulesIniPath);
