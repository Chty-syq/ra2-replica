#include "art_ini.h"
#include <utility>

ArtIni ArtIni::load(const std::filesystem::path& path) {
  const auto supplement = path.parent_path() / "replica.ini";
  return fromDocument(ra2::IniDocument::load(path), std::filesystem::exists(supplement) ? ra2::IniDocument::load(supplement) : ra2::IniDocument{});
}
ArtIni ArtIni::fromDocument(ra2::IniDocument document, ra2::IniDocument replica) {
  ArtIni result;
  result.document_ = std::move(document);
  result.replica_ = std::move(replica);
  return result;
}
ArtDefinition ArtIni::building(std::string id) const {
  ArtDefinition definition;
  definition.id = id;
  definition.image = document_.string(id, "Image", id);
  const auto profile = "Building." + id;
  const auto layerProfile = replica_.find(profile, "Layers") ? profile : "BuildingDefaults";
  auto layers = replica_.list(layerProfile, "Layers");
  if (layers.empty()) layers = {"Base", "Bib:UnderUnit", "Anim.IdleAnim", "Anim.ActiveAnim", "Anim.ActiveAnimTwo", "Anim.ActiveAnimThree", "Anim.ActiveAnimFour"};
  for (const auto& layer : layers) {
    const auto separator = layer.find(':');
    definition.layers.push_back({layer.substr(0, separator), separator == std::string::npos ? "NORMAL" : ra2::canonicalId(layer.substr(separator + 1))});
  }
  definition.layeredFactory = replica_.boolean(profile, "LayeredFactory");
  definition.productionSplit = replica_.boolean(profile, "ProductionSplit");
  definition.deferOverUnit = replica_.boolean(profile, "DeferOverUnit");
  definition.animationFrameLimit = replica_.integer(profile, "AnimationFrameLimit");
  if (document_.find(id, "buildup")) definition.buildup = document_.string(id, "buildup");
  if (document_.find(id, "bibShape")) definition.bibShape = document_.string(id, "bibShape");
  if (document_.find(id, "idleAnim")) definition.idleAnim = document_.string(id, "idleAnim");
  if (document_.find(id, "deployingAnim")) definition.deployingAnim = document_.string(id, "deployingAnim");
  if (document_.find(id, "underDoorAnim")) definition.underDoorAnim = document_.string(id, "underDoorAnim");
  if (document_.find(id, "roofDeployingAnim")) definition.roofDeployingAnim = document_.string(id, "roofDeployingAnim");
  if (document_.find(id, "underRoofDoorAnim")) definition.underRoofDoorAnim = document_.string(id, "underRoofDoorAnim");
  if (document_.find(id, "activeAnim")) definition.activeAnim = document_.string(id, "activeAnim");
  if (document_.find(id, "activeAnimTwo")) definition.activeAnimTwo = document_.string(id, "activeAnimTwo");
  if (document_.find(id, "activeAnimThree")) definition.activeAnimThree = document_.string(id, "activeAnimThree");
  if (document_.find(id, "activeAnimFour")) definition.activeAnimFour = document_.string(id, "activeAnimFour");
  if (document_.find(id, "specialAnim")) definition.specialAnim = document_.string(id, "specialAnim");
  if (document_.find(id, "specialAnimTwo")) definition.specialAnimTwo = document_.string(id, "specialAnimTwo");
  if (document_.find(id, "specialAnimThree")) definition.specialAnimThree = document_.string(id, "specialAnimThree");
  if (document_.find(id, "specialAnimFour")) definition.specialAnimFour = document_.string(id, "specialAnimFour");
  if (document_.find(id, "superAnim")) definition.superAnim = document_.string(id, "superAnim");
  if (document_.find(id, "superAnimTwo")) definition.superAnimTwo = document_.string(id, "superAnimTwo");
  if (document_.find(id, "superAnimThree")) definition.superAnimThree = document_.string(id, "superAnimThree");
  if (document_.find(id, "superAnimFour")) definition.superAnimFour = document_.string(id, "superAnimFour");
  if (document_.find(id, "productionAnim")) definition.productionAnim = document_.string(id, "productionAnim");
  if (document_.find(id, "height")) definition.height = static_cast<float>(document_.number(id, "height"));
  if (document_.find(id, "occupyHeight")) definition.occupyHeight = static_cast<float>(document_.number(id, "occupyHeight"));
  definition.newTheater = document_.boolean(id, "newTheater", false);
  definition.remapable = document_.boolean(id, "remapable", false);
  definition.terrainPalette = document_.boolean(id, "terrainPalette", false);
  definition.shadow = document_.boolean(id, "shadow", false);
  definition.flat = document_.boolean(id, "flat", false);
  definition.activeAnimPowered = document_.boolean(id, "activeAnimPowered", true);
  if (const auto value = document_.string(id, "Foundation"); !value.empty()) {
    const auto x = value.find_first_of("xX");
    if (x != std::string::npos) {
      const auto dimensions = ra2::IniDocument::parse("[F]\nW=" + value.substr(0, x) + "\nH=" + value.substr(x + 1));
      const auto width = dimensions.integer("F", "W"), height = dimensions.integer("F", "H");
      if (width < 1 || width > 32 || height < 1 || height > 32) document_.invalid(id, "Foundation", "dimensions outside 1..32");
      definition.foundation = FoundationSize{width, height};
    } else document_.invalid(id, "Foundation", "only rectangular width x height foundations are supported");
  }
  return definition;
}
int ArtIni::animationFrameLimit(std::string_view id) const { return replica_.integer("Animation." + std::string(id), "FrameLimit"); }
std::string ArtIni::field(std::string_view id, std::string_view key) const { return document_.string(id, key); }
AnimationDefinition ArtIni::animation(std::string id) const {
  AnimationDefinition definition;
  definition.id = id;
  definition.image = document_.string(id, "Image", id);
  if (document_.find(id, "start")) definition.start = document_.integer(id, "start");
  if (document_.find(id, "end")) definition.end = document_.integer(id, "end");
  if (document_.find(id, "loopStart")) definition.loopStart = document_.integer(id, "loopStart");
  if (document_.find(id, "loopEnd")) definition.loopEnd = document_.integer(id, "loopEnd");
  if (document_.find(id, "rate")) definition.rate = document_.integer(id, "rate");
  definition.normalized = document_.boolean(id, "Normalized");
  definition.newTheater = document_.boolean(id, "NewTheater");
  definition.shadow = document_.boolean(id, "Shadow");
  definition.yDrawOffset = document_.integer(id, "YDrawOffset");
  definition.normalZAdjust = document_.integer(id, "NormalZAdjust");
  return definition;
}

BuildingAnimationPlacement ArtIni::animationPlacement(std::string_view buildingId, std::string_view slot) const {
  const std::string key(slot);
  return {{document_.integer(buildingId, key + "X"), document_.integer(buildingId, key + "Y")},
          document_.integer(buildingId, key + "ZAdjust"), document_.integer(buildingId, key + "YSort")};
}
