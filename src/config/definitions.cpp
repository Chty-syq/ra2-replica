#include "definitions.h"
#include "compat/rules_timing.h"
#include "foundation.h"
#include "shp_ts.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ra2 {
namespace {
std::string reference(const IniDocument& doc, std::string_view id, std::string_view key, std::string fallback = {}) {
  auto value = canonicalId(doc.string(id, key, std::move(fallback)));
  return value == "NONE" || value == "<NONE>" ? std::string{} : value;
}
int scaled(const IniDocument& doc, std::string_view id, std::string_view key, double scale, double fallback = 0) {
  const auto value = doc.number(id, key, fallback) * scale;
  if (value < -100000000 || value > 100000000) doc.invalid(id, key, "number outside supported range");
  return static_cast<int>(std::lround(value));
}
template<class Map> const auto& lookup(const Map& map, std::string_view id) {
  const auto it = map.find(canonicalId(id));
  if (it == map.end()) throw std::runtime_error("Unknown definition: " + std::string(id));
  return it->second;
}
}
DefinitionRegistry DefinitionRegistry::load(const std::filesystem::path& settings) {
  auto result=compile(IniDocument::load(settings / "rules.ini"), IniDocument::load(settings / "art.ini"),
                      IniDocument::load(settings / "replica.ini"));
  const auto effects=settings.parent_path()/"effects";
  if (std::filesystem::is_directory(effects)) for (const auto& entry:std::filesystem::recursive_directory_iterator(effects)) {
    if (!entry.is_regular_file() || canonicalId(entry.path().extension().string())!=".SHP") continue;
    const auto image=canonicalId(entry.path().stem().string());
    for (auto& [id,animation]:result.effectAnimations_) {
      (void)id;
      if (animation.image==image) animation.frameCount=static_cast<int>(ShpTsFile::load(entry.path()).frameCount());
    }
  }
  return result;
}
DefinitionRegistry DefinitionRegistry::compile(IniDocument rules, IniDocument art, IniDocument replica) {
  DefinitionRegistry result;
  result.rules_ = std::move(rules);
  result.art_ = std::move(art);
  result.replica_ = replica;
  const auto& r = result.rules_;
  const auto& a = result.art_;
  result.fingerprint_ = r.fingerprint() + ":" + a.fingerprint() + ":" + replica.fingerprint();
  auto& profile = result.profile_;
  profile.gravity=r.integer("General","Gravity",6);
  if (profile.gravity<1 || profile.gravity>256) r.invalid("General","Gravity","expected 1..256");
  for (const auto* land : {"Clear","Rough","Road","Water","Rock","Beach","Ice","Tiberium","Weeds","Railroad","Tunnel"})
    for (const auto* speed : {"Track","Wheel","Foot","Hover","Amphibious","Float","FloatBeach"}) {
      auto text=r.string(land,speed,"100%");
      const bool percent=text.ends_with('%'); if (percent) text.pop_back();
      const auto value=IniDocument::parse("[Speed]\nValue="+text).number("Speed","Value")*(percent?1:100);
      if (value<0 || value>100) r.invalid(land,speed,"expected speed multiplier 0..100%");
      result.terrainSpeeds_[{canonicalId(land),canonicalId(speed)}]=static_cast<int>(value);
    }
  for (const auto* oldKey : {"TicksPerSecond", "RulesFramesPerSecond", "SpeedMilliCellsPerSecond",
                             "TurnUnitsPerSecond", "BuildTicks", "DeployTicks", "ProductionTicksPerCredit",
                             "SpeedSubcellsNumerator", "SpeedSubcellsDenominator", "TurnUnitsNumerator",
                             "TurnUnitsDenominator", "BuildFrames", "ProductionFramesPerCredit"})
    if (replica.find("Simulation", oldKey))
      replica.invalid("Simulation", oldKey, "legacy time units: migrate to GameSpeed and per-game-frame fields");
  const auto integer = [&](std::string_view section, std::string_view field, int fallback, int minimum, int maximum) {
    const auto value = replica.integer(section, field, fallback);
    if (value < minimum || value > maximum) replica.invalid(section, field, "value outside supported range");
    return value;
  };
  profile.defaultSpeed.level = integer("GameSpeed", "Level", 6, 0, 6);
  const auto mode = canonicalId(replica.string("GameSpeed", "Mode", "Multiplayer"));
  if (mode != "MULTIPLAYER" && mode != "SINGLEPLAYER")
    replica.invalid("GameSpeed", "Mode", "expected Multiplayer or Singleplayer");
  profile.defaultSpeed.mode = mode == "MULTIPLAYER" ? GameSpeedMode::Multiplayer : GameSpeedMode::Singleplayer;
  const auto ratio = [&](const IniDocument& doc, std::string_view section, std::string_view key, double fallback) {
    const double value = doc.number(section,key,fallback);
    if (value < 0 || value > 100) doc.invalid(section,key,"ratio outside supported range 0..100");
    return static_cast<int>(std::llround(value * compat::kRatioScale));
  };
  profile.buildSpeedMillionths = ratio(r,"General","BuildSpeed",0.7);
  const int buildup = ratio(r,"General","BuildupTime",0.06);
  profile.buildupFrames = std::max(1, static_cast<int>(static_cast<std::int64_t>(buildup) * kGameFramesPerMinute / compat::kRatioScale));
  profile.deployFrames = integer("Simulation", "DeployFrames", 90, 1, 100000000);
  profile.productionFramesOverride = integer("Simulation","ProductionFramesOverride",0,0,100000000);
  profile.pathRetryFrames = integer("Simulation", "PathRetryFrames", 60, 1, 10000);
  profile.blockedRetryFrames = integer("Simulation", "BlockedRetryFrames", 16, 1, 10000);
  profile.startingCredits = integer("Simulation", "StartingCredits", 50000, 0, 100000000);
  profile.sandbox = replica.boolean("Simulation", "Sandbox", true);
  for (const auto& [index,entry] : r.section("Tiberiums")) {
    (void)index;
    const auto id = canonicalId(entry.text);
    ResourceDef resource{id,r.integer(id,"Value"),r.integer(id,"Image",1)};
    if (resource.value < 1 || resource.value > 100000) r.invalid(id,"Value","resource value outside 1..100000");
    result.resources_.insert_or_assign(id,std::move(resource));
  }
  constexpr std::pair<std::string_view, TypeCategory> catalogs[] = {
    {"VehicleTypes", TypeCategory::Vehicle}, {"BuildingTypes", TypeCategory::Building},
    {"InfantryTypes", TypeCategory::Infantry}, {"AircraftTypes", TypeCategory::Aircraft}};
  constexpr std::string_view armors[] = {"NONE", "FLAK", "PLATE", "LIGHT", "MEDIUM", "HEAVY", "WOOD", "STEEL", "CONCRETE", "SPECIAL_1", "SPECIAL_2"};
  for (const auto& [catalog, category] : catalogs) for (const auto& [index, entry] : r.section(catalog)) {
    (void)index;
    const auto id = canonicalId(entry.text);
    if (id.empty() || id == "NONE") continue;
    UnitTypeDef type;
    type.id = id;
    type.category = category;
    type.image = reference(r, id, "Image", id);
    type.strength = r.integer(id, "Strength", 1);
    type.speed = r.integer(id, "Speed");
    type.rot = r.integer(id, "ROT", 5);
    type.cost = r.integer(id, "Cost");
    type.productionFrames = compat::productionFrames(std::clamp(type.cost,0,10000000), profile.buildSpeedMillionths,
                                                      ratio(r,id,"BuildTimeMultiplier",1.0));
    type.techLevel = r.integer(id, "TechLevel", -1);
    type.power = r.integer(id, "Power");
    type.turret = r.boolean(id, "Turret");
    type.opportunityFire = r.boolean(id, "OpportunityFire");
    type.canPassiveAcquire = r.boolean(id,"CanPassiveAquire",true);
    type.isTilter = r.boolean(id, "IsTilter");
    type.speedType = canonicalId(r.string(id,"SpeedType","Track"));
    type.naval = r.boolean(id, "Naval");
    type.primary = reference(r, id, "Primary");
    type.deploysInto = reference(r, id, "DeploysInto");
    type.undeploysInto = reference(r, id, "UndeploysInto");
    type.factory = reference(r, id, "Factory");
    type.prerequisites = r.list(id, "Prerequisite");
    type.owners = r.list(id, "Owner");
    // Unloading-only art variants also set Harvester but omit Storage.
    if (r.boolean(id,"Harvester") && !r.find(id,"Storage")) type.unsupported.push_back("harvester without storage");
    if (r.boolean(id,"Harvester") && r.find(id,"Storage")) {
      HarvesterDef mining;
      mining.capacity = r.integer(id,"Storage");
      if (mining.capacity < 1 || mining.capacity > 10000) r.invalid(id,"Storage","harvester capacity outside 1..10000");
      mining.docks = r.list(id,"Dock");
      if (mining.docks.empty()) r.invalid(id,"Dock","harvester requires a compatible refinery");
      for (auto& dock : mining.docks) dock = canonicalId(dock);
      mining.teleportReturn = r.boolean(id,"Teleporter");
      const auto section = "Harvester." + id;
      mining.harvestFrames = integer(section,"HarvestFrames",15,1,3600);
      mining.unloadFrames = integer(section,"UnloadFrames",3,1,3600);
      mining.chronoReturnFrames = integer(section,"ChronoReturnFrames",std::clamp(r.integer("General","ChronoMinimumDelay",16),1,3600),1,3600);
      type.harvester = std::move(mining);
      if (category != TypeCategory::Vehicle) r.invalid(id,"Harvester","only vehicle harvesters are currently supported");
    }
    const auto flh = a.list(type.image, "PrimaryFireFLH");
    if (!flh.empty() && flh.size() != 3) a.invalid(type.image, "PrimaryFireFLH", "expected Forward,Lateral,Height");
    for (std::size_t i = 0; i < flh.size(); ++i) {
      const auto coordinate = IniDocument::parse("[FLH]\nValue=" + flh[i]);
      type.primaryFireFlh[i] = coordinate.integer("FLH", "Value");
      if (type.primaryFireFlh[i] < -100000 || type.primaryFireFlh[i] > 100000) a.invalid(type.image, "PrimaryFireFLH", "coordinate outside supported range");
    }
    for (auto& item : type.prerequisites) item = canonicalId(item);
    for (auto& item : type.owners) item = canonicalId(item);
    const auto armor = canonicalId(r.string(id, "Armor", "none"));
    const auto armorIt = std::find(std::begin(armors), std::end(armors), armor);
    if (armorIt == std::end(armors)) type.unsupported.push_back("Armor=" + armor);
    else type.armor = static_cast<int>(armorIt - std::begin(armors));
    const auto locomotor = reference(r,id,"Locomotor");
    type.groundMobile = category == TypeCategory::Vehicle && !type.naval &&
      (locomotor == "{4A582741-9839-11D1-B709-00A024DDAFD1}" ||
       (type.harvester && type.harvester->teleportReturn && locomotor == "{4A582747-9839-11D1-B709-00A024DDAFD1}"));
    if (category != TypeCategory::Building && !type.groundMobile) type.unsupported.push_back("locomotor/terrain capability");
    for (const auto* flag : {"Cloakable", "IsTilter", "Spawns", "ResourceGatherer"}) {
      // IsTilter affects presentation only. Other advanced capabilities must not be silently emulated as tanks.
      if (std::string_view(flag) == "IsTilter") continue;
      const auto value = reference(r, id, flag);
      if (!value.empty() && value != "NO" && value != "FALSE" && value != "0") type.unsupported.push_back(flag);
    }
    if (type.strength < 1 || type.strength > 10000000 || type.speed < 0 || type.speed > 1000 || type.rot < 0 || type.rot > 1000 || type.cost < 0 || type.cost > 10000000)
      type.unsupported.push_back("numeric limits");
    if (const auto foundation = a.string(type.image, "Foundation"); !foundation.empty()) {
      const auto x = foundation.find_first_of("xX");
      if (x != std::string::npos) {
        const auto dimensions = IniDocument::parse("[F]\nW=" + foundation.substr(0, x) + "\nH=" + foundation.substr(x + 1));
        type.foundationWidth = dimensions.integer("F", "W");
        type.foundationHeight = dimensions.integer("F", "H");
        if (type.foundationWidth < 1 || type.foundationWidth > 32 || type.foundationHeight < 1 || type.foundationHeight > 32)
          a.invalid(type.image, "Foundation", "dimensions outside 1..32");
      } else type.unsupported.push_back("custom foundation");
    }
    const auto visualId = "Visual." + id;
    if (category == TypeCategory::Building && r.boolean(id,"Refinery") && r.boolean(id,"DockUnload")) {
      RefineryDef refinery;
      refinery.freeUnit = reference(r,id,"FreeUnit");
      const int count = r.integer(id,"NumberOfDocks",1);
      if (count < 1 || count > 8) r.invalid(id,"NumberOfDocks","supported range is 1..8");
      const auto cells = foundationTiles({{},type.foundationWidth,type.foundationHeight});
      int edge = 0; for (auto cell : cells) edge = std::max(edge,cell.x);
      const auto section = "Refinery." + id;
      const auto offsets = replica.list(section,"DockCells");
      if (offsets.empty()) for (int i=0;i<count;++i) refinery.docks.push_back({edge+1,i});
      else for (const auto& value : offsets) {
        const auto separator = value.find(':');
        if (separator == std::string::npos) replica.invalid(section,"DockCells","expected x:y offsets");
        const auto parsed = IniDocument::parse("[Offset]\nX="+value.substr(0,separator)+"\nY="+value.substr(separator+1));
        const TileCoord cell{parsed.integer("Offset","X"),parsed.integer("Offset","Y")};
        if (cell.x < -64 || cell.x > 64 || cell.y < -64 || cell.y > 64 || std::find(cells.begin(),cells.end(),cell)!=cells.end() ||
            std::find(refinery.docks.begin(),refinery.docks.end(),cell)!=refinery.docks.end())
          replica.invalid(section,"DockCells","dock must be unique, outside footprint and within 64 cells");
        refinery.docks.push_back(cell);
      }
      if (refinery.docks.size()!=static_cast<std::size_t>(count)) replica.invalid(section,"DockCells","must match NumberOfDocks");
      type.refinery = std::move(refinery);
    }
    if (type.factory == "UNITTYPE" && !type.naval) {
      const auto profileId = "Factory." + id;
      FactoryExitDef exit;
      const auto cells = foundationTiles({{0,0}, type.foundationWidth, type.foundationHeight});
      const auto inside = [&](TileCoord cell) { return std::any_of(cells.begin(), cells.end(), [&](auto c) { return c.x == cell.x && c.y == cell.y; }); };
      if (replica.find(profileId, "ExitPath")) {
        for (const auto& value : replica.list(profileId, "ExitPath")) {
          const auto separator = value.find(':');
          if (separator == std::string::npos) replica.invalid(profileId, "ExitPath", "expected x:y offsets separated by commas");
          const auto offsets = IniDocument::parse("[Offset]\nX=" + value.substr(0, separator) + "\nY=" + value.substr(separator + 1));
          try { exit.path.push_back({offsets.integer("Offset", "X"), offsets.integer("Offset", "Y")}); }
          catch (const std::runtime_error&) { replica.invalid(profileId, "ExitPath", "offsets must be integers"); }
        }
      } else {
        int edge = 0;
        for (const auto cell : cells) if (cell.y == 0) edge = std::max(edge, cell.x);
        for (int x = 0; x <= edge + 1; ++x) exit.path.push_back({x,0});
      }
      if (exit.path.size() < 2 || exit.path.size() > 64) replica.invalid(profileId, "ExitPath", "expected 2..64 cells");
      bool leftBuilding = false;
      for (std::size_t i = 0; i < exit.path.size(); ++i) {
        const auto cell = exit.path[i];
        if (cell.x < -64 || cell.x > 64 || cell.y < -64 || cell.y > 64) replica.invalid(profileId, "ExitPath", "offset outside -64..64");
        if (i && std::abs(cell.x - exit.path[i-1].x) + std::abs(cell.y - exit.path[i-1].y) != 1)
          replica.invalid(profileId, "ExitPath", "successive cells must share an edge");
        for (std::size_t j = 0; j < i; ++j) if (exit.path[j].x == cell.x && exit.path[j].y == cell.y)
          replica.invalid(profileId, "ExitPath", "route cannot revisit a cell");
        if (inside(cell) && leftBuilding) replica.invalid(profileId, "ExitPath", "route cannot reenter the building");
        leftBuilding = leftBuilding || !inside(cell);
      }
      if (!inside(exit.path.front()) || inside(exit.path.back())) replica.invalid(profileId, "ExitPath", "route must begin inside and end outside the footprint");
      for (const auto* oldKey : {"PrepareTicks", "ClearTicks"})
        if (replica.find(profileId, oldKey)) replica.invalid(profileId, oldKey, "rename to PrepareFrames / ClearFrames");
      exit.prepareFrames = replica.integer(profileId, "PrepareFrames");
      exit.clearFrames = replica.integer(profileId, "ClearFrames");
      if (exit.prepareFrames < 0 || exit.prepareFrames > 3600 || exit.clearFrames < 0 || exit.clearFrames > 3600)
        replica.invalid(profileId, exit.prepareFrames < 0 || exit.prepareFrames > 3600 ? "PrepareFrames" : "ClearFrames", "duration outside 0..3600 game frames");
      type.factoryExit = std::move(exit);
    }
    type.visual = {replica.string(visualId, "Folder"), replica.string(visualId, "Body"), replica.string(visualId, "Turret"),
                   replica.string(visualId, "Barrel"), replica.string(visualId, "Cameo", a.string(type.image, "Cameo")),
                   replica.string(visualId, "Faction")};
    result.units_.insert_or_assign(id, std::move(type));
  }
  for (auto& [id, type] : result.units_) {
    if (type.harvester) for (const auto& dock : type.harvester->docks)
      if (!result.units_.contains(dock) || !result.units_.at(dock).refinery) r.invalid(id,"Dock","unknown or unsupported refinery " + dock);
    if (type.refinery && !type.refinery->freeUnit.empty() &&
        (!result.units_.contains(type.refinery->freeUnit) || !result.units_.at(type.refinery->freeUnit).harvester))
      r.invalid(id,"FreeUnit","expected a supported harvester type");
    for (const auto* target : {&type.deploysInto, &type.undeploysInto}) if (!target->empty() && !result.units_.contains(*target))
      type.unsupported.push_back("missing transform target " + *target);
    if (type.primary.empty()) continue;
    if (!r.contains(type.primary)) { type.unsupported.push_back("missing Primary=" + type.primary); continue; }
    if (!result.weapons_.contains(type.primary)) {
      const auto& weaponId = type.primary;
      WeaponDef weapon;
      weapon.id = weaponId;
      weapon.damage = r.integer(weaponId, "Damage");
      weapon.rof = r.integer(weaponId, "ROF", 1);
      weapon.burst = r.integer(weaponId, "Burst", 1);
      weapon.rangeMilliCells = scaled(r, weaponId, "Range", 1000);
      weapon.speed = r.integer(weaponId, "Speed", 40);
      weapon.muzzleAnimation = reference(r,weaponId,"Anim");
      weapon.projectile = reference(r, weaponId, "Projectile");
      weapon.warhead = reference(r, weaponId, "Warhead");
      if (r.contains(weapon.projectile)) result.projectiles_.try_emplace(weapon.projectile,
        ProjectileDef{weapon.projectile, r.string(weapon.projectile, "Image"), r.boolean(weapon.projectile, "Arcing"),
                      r.boolean(weapon.projectile,"Inviso") && !r.boolean(weapon.projectile,"AA"),
                      r.boolean(weapon.projectile,"SubjectToCliffs"),r.boolean(weapon.projectile,"SubjectToElevation"),
                      r.boolean(weapon.projectile,"SubjectToWalls")});
      if (r.contains(weapon.warhead) && !result.warheads_.contains(weapon.warhead)) {
        WarheadDef warhead;
        warhead.id = weapon.warhead;
        warhead.animations = r.list(warhead.id,"AnimList");
        for (auto& name : warhead.animations) name=canonicalId(name);
        warhead.spreadMilliCells = scaled(r, warhead.id, "CellSpread", 1000);
        warhead.percentAtMax = scaled(r, warhead.id, "PercentAtMax", 100, 1);
        const auto verses = r.list(warhead.id, "Verses");
        if (!verses.empty() && verses.size() != warhead.versesPercent.size()) r.invalid(warhead.id, "Verses", "expected 11 armor multipliers");
        for (std::size_t i = 0; i < verses.size(); ++i) {
          auto value = verses[i];
          const bool percent = value.ends_with('%');
          if (percent) value.pop_back();
          const auto parsed = IniDocument::parse("[V]\nN=" + value);
          warhead.versesPercent[i] = scaled(parsed, "V", "N", percent ? 1 : 100);
        }
        result.warheads_.emplace(warhead.id, warhead);
      }
      weapon.supported = result.projectiles_.contains(weapon.projectile) && result.warheads_.contains(weapon.warhead) &&
        (result.projectiles_.at(weapon.projectile).arcing || result.projectiles_.at(weapon.projectile).invisible) && weapon.damage > 0 && weapon.damage <= 100000 &&
        weapon.rof > 0 && weapon.rof <= 100000 && weapon.burst == 1 && weapon.speed > 0 && weapon.speed <= 10000 &&
        weapon.rangeMilliCells > 0 && weapon.rangeMilliCells <= 100000;
      for (const auto* special : {"IsLaser", "IsRailgun", "IsElectricBolt", "IsSonic", "Spawner", "IsRadBeam", "FireOnce"})
        if (r.boolean(weaponId, special)) weapon.supported = false;
      result.weapons_.emplace(weapon.id, std::move(weapon));
    }
    if (!result.weapons_.at(type.primary).supported) type.unsupported.push_back("weapon capability " + type.primary);
  }
  const auto animation=[&](const std::string& id) {
    if (id.empty() || result.effectAnimations_.contains(id)) return;
    EffectAnimationDef value;
    value.image=reference(a,id,"Image",id);
    value.start=a.integer(id,"Start",0); value.end=a.integer(id,"End",-1);
    const int rate=a.integer(id,"Rate",-1);
    if (value.start<0 || value.end<-1 || (value.end>=0 && value.end<value.start) || rate<-1 || rate>900)
      a.invalid(id,"Rate/Start/End","invalid animation timing");
    value.frameDelay=rate==-1?1:compat::animationDelay(rate);
    value.normalized=a.boolean(id,"Normalized"); value.translucent=a.boolean(id,"Translucent");
    result.effectAnimations_.emplace(id,std::move(value));
  };
  for (const auto& [id,weapon]:result.weapons_) { (void)id; animation(weapon.muzzleAnimation); }
  for (const auto& [id,warhead]:result.warheads_) { (void)id; for (const auto& name:warhead.animations) animation(name); }
  return result;
}
const UnitTypeDef& DefinitionRegistry::unit(std::string_view id) const { return lookup(units_, id); }
const WeaponDef& DefinitionRegistry::weapon(std::string_view id) const { return lookup(weapons_, id); }
const ProjectileDef& DefinitionRegistry::projectile(std::string_view id) const { return lookup(projectiles_, id); }
const ResourceDef& DefinitionRegistry::resource(std::string_view id) const { return lookup(resources_, id); }
const WarheadDef& DefinitionRegistry::warhead(std::string_view id) const { return lookup(warheads_, id); }
int DefinitionRegistry::terrainSpeed(std::string_view land, std::string_view speedType) const {
  const auto it=terrainSpeeds_.find({canonicalId(land),canonicalId(speedType)});
  return it==terrainSpeeds_.end()?100:it->second;
}
const EffectAnimationDef& DefinitionRegistry::effectAnimation(std::string_view id) const { return lookup(effectAnimations_,id); }
}
