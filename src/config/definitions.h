#pragma once
#include "ini_document.h"
#include "map_grid.h"
#include "simulation/game_clock.h"
#include <array>
#include <map>
#include <optional>

namespace ra2 {
enum class TypeCategory { Vehicle, Building, Infantry, Aircraft };
struct VisualDef {
  std::string folder, body, turret, barrel, cameo, faction;
};
struct WeaponDef {
  std::string id, projectile, warhead;
  std::string muzzleAnimation;
  int damage = 0, rof = 1, burst = 1, speed = 40;
  int rangeMilliCells = 0;
  bool supported = false;
};
struct ProjectileDef {
  std::string id, image;
  bool arcing = false, invisible = false;
  bool subjectToCliffs = false, subjectToElevation = false, subjectToWalls = false;
};
struct ResourceDef { std::string id; int value = 0, image = 1; };
struct EffectAnimationDef {
  std::string image;
  int start = 0, end = -1, frameCount = 0, frameDelay = 1;
  bool normalized = false, translucent = false;
};
struct HarvesterDef {
  int capacity = 0, harvestFrames = 15, unloadFrames = 3, chronoReturnFrames = 16;
  bool teleportReturn = false;
  std::vector<std::string> docks;
};
struct RefineryDef { std::vector<TileCoord> docks; std::string freeUnit; };
struct WarheadDef {
  std::string id;
  std::vector<std::string> animations;
  std::array<int, 11> versesPercent{100,100,100,100,100,100,100,100,100,100,100};
  int spreadMilliCells = 0, percentAtMax = 100;
};
struct FactoryExitDef {
  // Cell offsets from the northwest Foundation start, including the internal spawn cell.
  std::vector<TileCoord> path;
  int prepareFrames = 0, clearFrames = 0;
};
struct UnitTypeDef {
  std::string id, image, primary, deploysInto, undeploysInto, factory;
  TypeCategory category = TypeCategory::Vehicle;
  int strength = 1, armor = 0, speed = 0, rot = 0, cost = 0, techLevel = -1, power = 0;
  int foundationWidth = 1, foundationHeight = 1;
  int productionFrames = 54;
  bool turret = false, naval = false, groundMobile = false;
  bool opportunityFire = false, isTilter = false;
  bool canPassiveAcquire = true;
  std::string speedType = "TRACK";
  std::array<int, 3> primaryFireFlh{0,0,0};
  std::vector<std::string> prerequisites, owners, unsupported;
  VisualDef visual;
  std::optional<FactoryExitDef> factoryExit;
  std::optional<HarvesterDef> harvester;
  std::optional<RefineryDef> refinery;
};
// Rule-derived timing plus explicit demo switches. Movement conversions live in compat/.
struct SimulationProfile {
  GameSpeed defaultSpeed{};
  int buildSpeedMillionths = 700000, buildupFrames = 54;
  int deployFrames = 90; // MCV choreography still requires an original-game trace.
  int productionFramesOverride = 0; // 0 = rules; nonzero is an explicit test/demo override.
  int pathRetryFrames = 60, blockedRetryFrames = 16;
  int startingCredits = 50000;
  int gravity = 6;
  bool sandbox = true;
};
class DefinitionRegistry {
public:
  static DefinitionRegistry load(const std::filesystem::path& settings);
  static DefinitionRegistry compile(IniDocument rules, IniDocument art, IniDocument replica = {});
  const UnitTypeDef& unit(std::string_view id) const;
  const WeaponDef& weapon(std::string_view id) const;
  const ProjectileDef& projectile(std::string_view id) const;
  const WarheadDef& warhead(std::string_view id) const;
  const ResourceDef& resource(std::string_view id) const;
  const auto& resources() const { return resources_; }
  const std::map<std::string, UnitTypeDef, std::less<>>& units() const { return units_; }
  const IniDocument& rules() const { return rules_; }
  const IniDocument& art() const { return art_; }
  const IniDocument& replica() const { return replica_; }
  const SimulationProfile& profile() const { return profile_; }
  const std::string& fingerprint() const { return fingerprint_; }
  int terrainSpeed(std::string_view land, std::string_view speedType) const;
  const EffectAnimationDef& effectAnimation(std::string_view id) const;
private:
  IniDocument rules_, art_, replica_;
  SimulationProfile profile_;
  std::string fingerprint_;
  std::map<std::string, UnitTypeDef, std::less<>> units_;
  std::map<std::string, WeaponDef, std::less<>> weapons_;
  std::map<std::string, ProjectileDef, std::less<>> projectiles_;
  std::map<std::string, WarheadDef, std::less<>> warheads_;
  std::map<std::string, ResourceDef, std::less<>> resources_;
  std::map<std::pair<std::string,std::string>, int> terrainSpeeds_;
  std::map<std::string,EffectAnimationDef,std::less<>> effectAnimations_;
};
}
