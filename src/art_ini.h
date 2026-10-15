#pragma once

#include "config/ini_document.h"

#include <filesystem>
#include <optional>
#include <string>

struct FoundationSize {
  int width = 1;
  int height = 1;
};

struct PixelOffset { int x = 0; int y = 0; };

// Position and depth are separate art.ini concepts. Retain depth settings for
// a future original Z-shape renderer; never add them to image coordinates.
struct BuildingAnimationPlacement {
  PixelOffset offset;
  int zAdjust = 0;
  int ySort = 0;
};

struct AnimationDefinition {
  std::string id;
  std::string image;
  std::optional<int> start;
  std::optional<int> end;
  std::optional<int> loopStart;
  std::optional<int> loopEnd;
  std::optional<int> rate;
  bool normalized = false;
  bool newTheater = false;
  bool shadow = false;
  int yDrawOffset = 0;
  int normalZAdjust = 0;
};

struct ArtDefinition {
  struct Layer { std::string source; std::string role; };
  std::string id;
  std::string image;
  std::optional<std::string> buildup;
  std::optional<std::string> bibShape;
  std::optional<std::string> idleAnim;
  std::optional<std::string> deployingAnim;
  std::optional<std::string> underDoorAnim;
  std::optional<std::string> roofDeployingAnim;
  std::optional<std::string> underRoofDoorAnim;
  std::optional<std::string> activeAnim;
  std::optional<std::string> activeAnimTwo;
  std::optional<std::string> activeAnimThree;
  std::optional<std::string> activeAnimFour;
  std::optional<std::string> specialAnim;
  std::optional<std::string> specialAnimTwo;
  std::optional<std::string> specialAnimThree;
  std::optional<std::string> specialAnimFour;
  std::optional<std::string> superAnim;
  std::optional<std::string> superAnimTwo;
  std::optional<std::string> superAnimThree;
  std::optional<std::string> superAnimFour;
  std::optional<std::string> productionAnim;
  std::optional<float> height;
  std::optional<float> occupyHeight;
  std::optional<FoundationSize> foundation;
  bool newTheater = false;
  bool remapable = false;
  bool terrainPalette = false;
  bool shadow = false;
  bool flat = false;
  bool activeAnimPowered = true;
  std::vector<Layer> layers;
  bool layeredFactory = false;
  bool productionSplit = false;
  bool deferOverUnit = false;
  int animationFrameLimit = 0;
};

class ArtIni {
public:
  static ArtIni load(const std::filesystem::path& path);
  static ArtIni fromDocument(ra2::IniDocument document, ra2::IniDocument replica = {});
  [[nodiscard]] int animationFrameLimit(std::string_view id) const;
  [[nodiscard]] std::string field(std::string_view id, std::string_view key) const;

  [[nodiscard]] ArtDefinition building(std::string id) const;
  [[nodiscard]] AnimationDefinition animation(std::string id) const;
  [[nodiscard]] BuildingAnimationPlacement animationPlacement(std::string_view buildingId, std::string_view slot) const;

private:
  ra2::IniDocument document_;
  ra2::IniDocument replica_;
};
