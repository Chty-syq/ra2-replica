#pragma once

#include <filesystem>
#include <optional>
#include <string>

struct FoundationSize {
  int width = 1;
  int height = 1;
};

struct AnimationDefinition {
  std::string id;
  std::string image;
  std::optional<int> start;
  std::optional<int> end;
  std::optional<int> loopStart;
  std::optional<int> loopEnd;
  std::optional<int> rate;
  bool newTheater = false;
  bool shadow = false;
};

struct ArtDefinition {
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
};

class ArtIni {
public:
  static ArtIni load(const std::filesystem::path& path);

  [[nodiscard]] ArtDefinition building(std::string id) const;
  [[nodiscard]] AnimationDefinition animation(std::string id) const;

private:
  std::filesystem::path path_;
};
