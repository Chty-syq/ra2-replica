#include "definitions.h"
#include "compat/rules_timing.h"
#include "foundation.h"
#include "shp_ts.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <span>
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
WarheadDef readWarhead(const IniDocument& r,std::string id) {
  WarheadDef warhead;
  warhead.id = std::move(id);
  auto prone=r.string(warhead.id,"ProneDamage","100%");
  const bool percent=prone.ends_with('%'); if(percent) prone.pop_back();
  warhead.proneDamagePercent=scaled(IniDocument::parse("[P]\nValue="+prone),"P","Value",percent?1:100);
  warhead.infantryDeath=r.integer(warhead.id,"InfDeath");
  warhead.damagesWalls=r.boolean(warhead.id,"Wall");
  warhead.radiation=r.boolean(warhead.id,"Radiation");
  warhead.sonic=r.boolean(warhead.id,"Sonic");warhead.culling=r.boolean(warhead.id,"Culling");
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
  return warhead;
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
  for(auto& [id,type]:result.units_) {
    if(type.visual.kind==VisualKind::Voxel)for(const auto* stem:{&type.visual.body,&type.visual.turret,&type.visual.barrel}) {
      if(stem->empty())continue;
      for(const auto* extension:{".vxl",".hva"})if(!std::filesystem::is_regular_file(settings.parent_path()/"vehicles"/type.visual.folder/(*stem+extension)))
        type.unsupported.push_back("missing runtime asset "+*stem+extension);
    }
    for(int i=1;i<type.chargeTurrets;++i)for(const auto* extension:{".vxl",".hva"})
      if(!std::filesystem::is_regular_file(settings.parent_path()/"vehicles"/type.visual.folder/(type.visual.turret+std::to_string(i)+extension)))
        type.unsupported.push_back("missing charge turret asset");
    if((type.missileLauncher && type.missileLauncher->alternateBody) || (type.navalSpawn && type.navalSpawn->alternateBody))for(const auto* extension:{".vxl",".hva"})
      if(!std::filesystem::is_regular_file(settings.parent_path()/"vehicles"/type.visual.folder/(type.visual.body+"wo"+extension)))type.unsupported.push_back("missing empty launcher asset");
    if((!type.infantry && !type.spriteVehicle) || !type.unsupported.empty())continue;
    const auto file=settings.parent_path()/(type.infantry?"infantry":"vehicles")/type.visual.folder/type.visual.body;
    const auto shp=ShpTsFile::load(file);
    auto& art=type.infantry?*type.infantry:*type.spriteVehicle;
    art.bodyFrames=static_cast<int>(shp.frameCount()/(result.rules().boolean(id,"NoShadow")?1:2));
    if(type.leap && !type.leap->naval) {
      const auto jump=ShpTsFile::load(file.parent_path()/type.leap->image);
      if(jump.frameCount()!=64)throw std::runtime_error(id+": expected 32 leap facings and shadows");
    }
    if(!type.arcticBody.empty()) {
      const auto arctic=ShpTsFile::load(file.parent_path()/type.arcticBody);
      if(arctic.frameCount()!=shp.frameCount())throw std::runtime_error(id+": Arctic art frame layout differs");
    }
    for(const auto& [name,seq]:art.sequences) {
      // TRST's original FireUp/FireProne extend beyond its body frames. Native
      // Suicide never plays these poses; all reachable sequences remain checked.
      if((name=="FIREUP" || name=="FIREPRONE") && result.weapons_.contains(type.primary) && result.weapon(type.primary).suicide)continue;
      if(seq.count>0 && seq.start+(seq.fixedFacing>=0?seq.fixedFacing:7)*seq.stride+seq.count>art.bodyFrames)
        throw std::runtime_error(id+": SHP sequence outside body frames: "+name);
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
  auto& rad=result.radiation_;auto& missile=result.missile_;
  const auto bounded=[&](std::string_view section,std::string_view field,int fallback,int lo,int hi) {
    const int value=r.integer(section,field,fallback);if(value<lo || value>hi)r.invalid(section,field,"outside supported numeric range");return value;
  };
  auto& bomb=result.ivanBomb_;
  bomb.damage=bounded("CombatDamage","IvanDamage",400,1,100000);
  bomb.delay=bounded("CombatDamage","IvanTimedDelay",450,1,100000);
  bomb.flickerFrames=bounded("CombatDamage","IvanIconFlickerRate",8,1,10000);
  bomb.warhead=reference(r,"CombatDamage","IvanWarhead","IVANWH");
  rad.durationMultiple=bounded("Radiation","RadDurationMultiple",1,1,100);
  rad.applicationDelay=bounded("Radiation","RadApplicationDelay",16,1,3600);
  rad.levelMax=bounded("Radiation","RadLevelMax",500,1,10000);
  rad.levelDelay=bounded("Radiation","RadLevelDelay",90,1,3600);rad.lightDelay=bounded("Radiation","RadLightDelay",90,1,3600);
  rad.damageFactorMillionths=scaled(r,"Radiation","RadLevelFactor",1000000,.2);
  rad.lightFactorMillionths=scaled(r,"Radiation","RadLightFactor",1000000,.1);rad.tintFactorMillionths=scaled(r,"Radiation","RadTintFactor",1000000,1);
  if(rad.damageFactorMillionths<0 || rad.damageFactorMillionths>10000000 || rad.lightFactorMillionths<0 || rad.lightFactorMillionths>10000000 || rad.tintFactorMillionths<0 || rad.tintFactorMillionths>10000000)
    r.invalid("Radiation","RadLevelFactor/RadLightFactor/RadTintFactor","expected 0..10");
  rad.warhead=reference(r,"Radiation","RadSiteWarhead","RADSITE");
  const auto color=r.list("Radiation","RadColor");if(!color.empty()) {
    if(color.size()!=3)r.invalid("Radiation","RadColor","expected R,G,B");
    for(int i=0;i<3;++i){rad.color[i]=IniDocument::parse("[C]\nN="+color[i]).integer("C","N");if(rad.color[i]<0 || rad.color[i]>255)r.invalid("Radiation","RadColor","expected 0..255");}
  }
  missile.pauseFrames=bounded("General","V3RocketPauseFrames",0,0,3600);missile.tiltFrames=bounded("General","V3RocketTiltFrames",60,1,3600);
  missile.initialPitch=scaled(r,"General","V3RocketPitchInitial",16384,.21);missile.finalPitch=scaled(r,"General","V3RocketPitchFinal",16384,.5);
  missile.turnRate=scaled(r,"General","V3RocketTurnRate",16384,.05);missile.accelerationMilli=scaled(r,"General","V3RocketAcceleration",1000,.4);
  missile.altitude=bounded("General","V3RocketAltitude",768,1,8192);missile.damage=bounded("General","V3RocketDamage",200,1,100000);
  missile.bodyLength=bounded("General","V3RocketBodyLength",256,1,2048);missile.lazyCurve=r.boolean("General","V3RocketLazyCurve",true);
  missile.type=reference(r,"General","V3RocketType","V3ROCKET");
  if(missile.initialPitch<0 || missile.initialPitch>16384 || missile.finalPitch<1 || missile.finalPitch>16384 || missile.turnRate<1 || missile.turnRate>16384 || missile.accelerationMilli<1 || missile.accelerationMilli>100000)
    r.invalid("General","V3RocketPitch/TurnRate/Acceleration","invalid missile motion parameters");
  bounded("General","ChronoMinimumDelay",16,1,36000);bounded("General","ChronoDistanceFactor",48,1,100000);
  bounded("General","ChronoRangeMinimum",0,0,1000000);bounded("General","SpyPowerBlackout",1000,1,360000);
  if(r.number("CombatDamage","C4Delay",.03)<=0 || r.number("CombatDamage","C4Delay",.03)>60)r.invalid("CombatDamage","C4Delay","expected positive duration up to 60 minutes");
  if(r.number("General","SpyMoneyStealPercent",.5)<0 || r.number("General","SpyMoneyStealPercent",.5)>1)r.invalid("General","SpyMoneyStealPercent","expected 0..1");
  profile.gravity=r.integer("AudioVisual","Gravity",6);
  profile.ballisticScatter=scaled(r,"CombatDamage","BallisticScatter",1000,1);
  if(profile.ballisticScatter<0 || profile.ballisticScatter>10000)r.invalid("CombatDamage","BallisticScatter","expected 0..10 cells");
  if (profile.gravity<1 || profile.gravity>256) r.invalid("AudioVisual","Gravity","expected 1..256");
  auto& dread=result.dreadMissile_;
  dread.type=reference(r,"General","DMislType","DMISL");dread.impactWeapon="@DMISLIMPACT";
  dread.pauseFrames=bounded("General","DMislPauseFrames",20,0,3600);dread.tiltFrames=bounded("General","DMislTiltFrames",60,1,3600);
  dread.initialPitch=scaled(r,"General","DMislPitchInitial",16384,0);dread.finalPitch=scaled(r,"General","DMislPitchFinal",16384,.5);
  dread.turnRate=scaled(r,"General","DMislTurnRate",16384,.08);dread.accelerationMilli=scaled(r,"General","DMislAcceleration",1000,.8);
  dread.altitude=bounded("General","DMislAltitude",768,1,8192);dread.damage=bounded("General","DMislDamage",300,1,100000);
  dread.bodyLength=bounded("General","DMislBodyLength",128,1,2048);dread.lazyCurve=r.boolean("General","DMislLazyCurve",false);
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
    auto source=doc.string(section,key);
    const bool percent=source.ends_with('%');if(percent)source.pop_back();
    const double value = percent?IniDocument::parse("[Ratio]\nValue="+source).number("Ratio","Value")/100:doc.number(section,key,fallback);
    if (value < 0 || value > 100) doc.invalid(section,key,"ratio outside supported range 0..100");
    return static_cast<int>(std::llround(value * compat::kRatioScale));
  };
  profile.buildSpeedMillionths = ratio(r,"General","BuildSpeed",0.7);
  const int buildup = ratio(r,"General","BuildupTime",0.06);
  profile.buildupFrames = std::max(1, static_cast<int>(static_cast<std::int64_t>(buildup) * kGameFramesPerMinute / compat::kRatioScale));
  profile.repairPercentMillionths=ratio(r,"General","RepairPercent",.15);
  profile.refundPercentMillionths=ratio(r,"General","RefundPercent",.5);
  if(profile.repairPercentMillionths>compat::kRatioScale || profile.refundPercentMillionths>compat::kRatioScale)
    r.invalid("General","RepairPercent/RefundPercent","expected 0..1 or 0..100%");
  profile.repairFrames=std::max(1,int(std::int64_t(ratio(r,"General","RepairRate",.016))*kGameFramesPerMinute/compat::kRatioScale));
  profile.repairStep=r.integer("General","RepairStep",8);
  profile.conditionRedMillionths=ratio(r,"AudioVisual","ConditionRed",.25);
  profile.conditionYellowMillionths=ratio(r,"AudioVisual","ConditionYellow",.5);
  profile.selfHealCapMillionths=profile.conditionYellowMillionths;
  if(profile.conditionYellowMillionths>compat::kRatioScale || profile.conditionRedMillionths>profile.conditionYellowMillionths)
    r.invalid("AudioVisual","ConditionRed/ConditionYellow","expected 0 <= red <= yellow <= 100%");
  profile.defaultDeathDamagePercent=integer("Simulation","DefaultDeathDamagePercent",5,0,1000);
  if(profile.repairStep<1 || profile.repairStep>10000000)r.invalid("General","RepairStep","expected 1..10000000");
  profile.deployFrames = integer("Simulation", "DeployFrames", 90, 1, 100000000);
  profile.productionFramesOverride = integer("Simulation","ProductionFramesOverride",0,0,100000000);
  profile.pathRetryFrames = integer("Simulation", "PathRetryFrames", 60, 1, 10000);
  profile.blockedRetryFrames = integer("Simulation", "BlockedRetryFrames", 16, 1, 10000);
  profile.startingCredits = integer("Simulation", "StartingCredits", 50000, 0, 100000000);
  profile.sandbox = replica.boolean("Simulation", "Sandbox", true);
  profile.engineerEntryFrames=integer("Simulation","EngineerEntryFrames",12,1,120);
  profile.unloadFrames=integer("Simulation","UnloadFrames",8,1,120);
  profile.infantryHitRadiusLeptons=integer("Simulation","InfantryHitRadiusLeptons",32,0,128);
  profile.playerHouse=canonicalId(replica.string("Simulation","PlayerHouse","British"));
  if(replica.find("Simulation","PlayerHouse") && !r.contains(profile.playerHouse))replica.invalid("Simulation","PlayerHouse","unknown country");
  for(const auto& [player,value]:replica.section("Players")) {
    if(!r.contains(canonicalId(value.text)))replica.invalid("Players",player,"unknown country");
  }
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
    if(category==TypeCategory::Aircraft)type.flightLevel=bounded(id,"FlightLevel",bounded("General","FlightLevel",1500,1,8192),1,8192);
    type.cost = r.integer(id, "Cost");
    type.productionFrames = compat::productionFrames(std::clamp(type.cost,0,10000000), profile.buildSpeedMillionths,
                                                      ratio(r,id,"BuildTimeMultiplier",1.0));
    type.techLevel = r.integer(id, "TechLevel", -1);
    type.power = r.integer(id, "Power");
    type.powered=r.boolean(id,"Powered");type.wall=r.boolean(id,"Wall");
    type.overpowerable=category==TypeCategory::Building && r.boolean(id,"Overpowerable");
    if(category==TypeCategory::Building && r.boolean(id,"TurretAnimIsVoxel"))type.buildingTurret=reference(r,id,"TurretAnim");
    if(category==TypeCategory::Building && a.boolean(type.image,"IsAnimDelayedFire"))type.fireDelay=a.integer(type.image,"DelayedFireDelay");
    if(type.fireDelay<0 || type.fireDelay>900)a.invalid(type.image,"DelayedFireDelay","expected 0..900 game frames");
    type.turret = r.boolean(id, "Turret");
    type.opportunityFire = r.boolean(id, "OpportunityFire");
    type.canPassiveAcquire = r.boolean(id,"CanPassiveAquire",true);
    type.isTilter = r.boolean(id, "IsTilter");
    type.speedType = canonicalId(r.string(id,"SpeedType",category==TypeCategory::Infantry?"Foot":"Track"));
    type.naval = r.boolean(id, "Naval");
    type.waterBound=r.boolean(id,"WaterBound");type.unitRepair=r.boolean(id,"UnitRepair");
    type.hover=category==TypeCategory::Vehicle && reference(r,id,"Locomotor")=="{4A582742-9839-11D1-B709-00A024DDAFD1}";
    type.typeImmune=r.boolean(id,"TypeImmune");type.spawnedAircraft=(id=="ASW" || id=="HORNET") && r.boolean(id,"Spawned");
    type.underwater=r.boolean(id,"Underwater");type.cloakable=(id=="SUB" || id=="DLPH" || id=="SQD") && r.boolean(id,"Cloakable");
    type.navalTargeting=bounded(id,"NavalTargeting",0,0,6);type.landTargeting=bounded(id,"LandTargeting",0,0,1);
    type.sensorsRange=r.boolean(id,"Sensors")?scaled(r,id,"SensorsSight",1000,r.number(id,"Sight",4)):0;
    if(type.sensorsRange<0 || type.sensorsRange>100000)r.invalid(id,"SensorsSight","expected 0..100 cells");
    type.cloakingSpeed=bounded(id,"CloakingSpeed",1,1,100);
    type.airRangeBonus=scaled(r,id,"AirRangeBonus",1000);
    type.buildingHeight=category==TypeCategory::Building?scaled(a,type.image,"Height",128):0;
    const int weaponCount=r.integer(id,"WeaponCount");
    if(weaponCount<0 || weaponCount>32)r.invalid(id,"WeaponCount","expected 0..32");
    for(int slot=1;slot<=weaponCount;++slot)type.weaponSlots.push_back(reference(r,id,"Weapon"+std::to_string(slot)));
    type.primary = reference(r, id, "Primary",type.weaponSlots.empty()?"":type.weaponSlots[0]);
    // DeathWeapon is conditional: merely declaring one (FV) does not arm it.
    if(category!=TypeCategory::Building) {
      type.deathWeapon=reference(r,id,"DeathWeapon");
      type.explodes=r.boolean(id,"Explodes");
    }
    type.secondary = reference(r,id,"Secondary");
    type.c4=r.boolean(id,"C4");type.canC4=r.boolean(id,"CanC4",true);
    type.spy=r.boolean(id,"Agent") && r.boolean(id,"Infiltrate");type.spyable=r.boolean(id,"Spyable");
    type.canDisguise=category==TypeCategory::Infantry && r.boolean(id,"CanDisguise");type.warpable=r.boolean(id,"Warpable",true);
    type.teleporter=category==TypeCategory::Infantry && r.boolean(id,"Teleporter");
    type.trainable=r.boolean(id,"Trainable",true);type.veteranAbilities=r.list(id,"VeteranAbilities");
    for(auto& ability:type.veteranAbilities)ability=canonicalId(ability);
    if(r.boolean(id,"AlternateArcticArt"))type.arcticBody=replica.string("Visual."+id,"ArcticBody");
    type.ivan=r.boolean(id,"Ivan");type.bombable=r.boolean(id,"Bombable",true) && !type.wall;
    type.bombSight=scaled(r,id,"BombSight",1000);
    if(type.bombSight<0 || type.bombSight>100000)r.invalid(id,"BombSight","expected 0..100 cells");
    type.selfHealing=r.boolean(id,"SelfHealing") && category==TypeCategory::Vehicle;
    const auto explosions=r.list(id,"Explosion");
    if(!explosions.empty())type.deathAnimation=canonicalId(explosions.front());
    type.engineer=r.boolean(id,"Engineer");
    type.immuneToRadiation=r.boolean(id,"ImmuneToRadiation");
    // A deployed area emitter is distinct from GI's targeted deployed gun.
    type.deployFire=r.boolean(id,"DeployFire") && r.boolean(type.secondary,"AreaFire") && r.integer(type.secondary,"RadLevel")>0;
    type.missileSpawn=r.boolean(id,"MissileSpawn") && category==TypeCategory::Aircraft && (id==missile.type || id==result.dreadMissile_.type);
    if(const auto spawn=reference(r,id,"Spawns");!spawn.empty() && spawn==missile.type && r.boolean(spawn,"MissileSpawn") && r.integer(id,"SpawnsNumber",1)==1 && !type.naval) {
      type.missileLauncher=MissileLauncherDef{spawn,bounded(id,"SpawnRegenRate",400,1,36000),r.boolean(id,"NoSpawnAlt")};
    }
    if(type.naval && (id=="DEST" || id=="CARRIER" || id=="DRED")) {
      NavalSpawnDef spawns;spawns.type=reference(r,id,"Spawns");spawns.count=bounded(id,"SpawnsNumber",1,1,8);
      spawns.regenerateFrames=bounded(id,"SpawnRegenRate",400,1,36000);spawns.reloadFrames=bounded(id,"SpawnReloadRate",150,0,36000);
      spawns.missile=r.boolean(spawns.type,"MissileSpawn");spawns.alternateBody=r.boolean(id,"NoSpawnAlt");type.navalSpawn=std::move(spawns);
    }
    type.occupier=r.boolean(id,"Occupier");type.garrisonable=r.boolean(id,"CanBeOccupied");type.gunner=r.boolean(id,"Gunner");
    type.passengers=r.integer(id,"Passengers");type.size=r.integer(id,"Size",1);type.sizeLimit=r.integer(id,"SizeLimit",1);
    type.maxOccupants=r.integer(id,"MaxNumberOccupants");type.ifvMode=r.integer(id,"IFVMode");
    if(type.passengers<0 || type.passengers>100 || type.size<1 || type.size>100 || type.sizeLimit<1 || type.sizeLimit>100 ||
        type.maxOccupants<0 || type.maxOccupants>100 || type.ifvMode<0 || type.ifvMode>31)r.invalid(id,"Passengers/Size/IFVMode","value outside supported range");
    if(type.garrisonable) {
      type.garrisonMuzzleHeight=scaled(a,type.image,"OccupyHeight",128,1);
      for(int slot=0;slot<type.maxOccupants;++slot) {
        const auto field="MuzzleFlash"+std::to_string(slot);const auto offset=a.list(type.image,field);if(offset.empty())continue;
        if(offset.size()!=2)a.invalid(type.image,field,"expected X,Y");
        std::array<int,2> point{};for(int axis=0;axis<2;++axis)point[axis]=IniDocument::parse("[P]\nN="+offset[axis]).integer("P","N");
        type.garrisonMuzzles.push_back(point);
      }
    }
    if(type.gunner)for(const auto prefix:{"Normal","Repair","MachineGun","Flak","Pistol","Sniper","Shock","Explode","BrainBlast","RadCannon","Chrono","TerroristExplode","Cow"}) {
      const auto slot=r.integer(id,std::string(prefix)+"TurretWeapon",-1),turret=r.integer(id,std::string(prefix)+"TurretIndex",-1);
      if(slot>=weaponCount || turret>14 || slot<-1 || turret<-1)r.invalid(id,"TurretWeapon/TurretIndex","mode or turret outside supported range");
      if(slot>=0 && turret>=0)type.gunnerTurrets[slot]=turret;
    }
    type.parasiteable=r.boolean(id,"Parasiteable",true);type.organic=r.boolean(id,"Organic");
    if((id=="DOG" || id=="ADOG" || id=="DRON" || id=="SQD") && r.boolean(type.primary,"LimboLaunch") && r.boolean(reference(r,type.primary,"Warhead"),"Parasite")) {
      LeapDef leap;leap.infantryOnly=category==TypeCategory::Infantry;leap.naval=id=="SQD";
      auto image=reference(r,reference(r,type.primary,"Projectile"),"Image");
      std::transform(image.begin(),image.end(),image.begin(),[](unsigned char c){return std::tolower(c);});leap.image=leap.naval?std::string{}:image+".shp";
      leap.scanRange=leap.naval?scaled(r,id,"GuardRange",1000,5):scaled(r,type.secondary,"Range",1000,5);
      leap.suppressionThreshold=r.integer(id,"SuppressionThreshold",5);
      if(leap.scanRange<1 || leap.scanRange>100000 || leap.suppressionThreshold<0 || leap.suppressionThreshold>100000)r.invalid(id,"SuppressionThreshold","invalid leap parameters");
      type.leap=std::move(leap);
    }
    type.capturable=r.boolean(id,"Capturable");type.repairable=r.boolean(id,"Repairable",true);
    type.clickRepairable=type.repairable && r.boolean(id,"ClickRepairable",true);
    type.sellable=category==TypeCategory::Building && !r.boolean(id,"Unsellable") && !type.garrisonable;
    type.requiredHouses=r.list(id,"RequiredHouses");type.forbiddenHouses=r.list(id,"ForbiddenHouses");
    for(auto* list:{&type.requiredHouses,&type.forbiddenHouses})for(auto& house:*list)house=canonicalId(house);
    type.crusher = r.boolean(id,"Crusher");
    type.disguiseWhenStill=r.boolean(id,"DisguiseWhenStill") && r.boolean(id,"CanDisguise") && category==TypeCategory::Vehicle;
    type.canApproachTarget=r.boolean(id,"CanApproachTarget",true);
    type.detectDisguise=r.boolean(id,"DetectDisguise");
    type.detectDisguiseRange=scaled(r,id,"DetectDisguiseRange",1000,r.number(id,"Sight",5));
    if(type.disguiseWhenStill) {
      type.disguises=r.list("General","DefaultMirageDisguises");
      for(auto& disguise:type.disguises)disguise=canonicalId(disguise);
      if(type.disguises.empty())type.unsupported.push_back("missing terrain disguises");
    }
    if(r.boolean(id,"IsChargeTurret")) {
      type.chargeTurrets=r.integer(id,"TurretCount");
      if(type.chargeTurrets<1 || type.chargeTurrets>16)r.invalid(id,"TurretCount","charge turret count outside 1..16");
    }
    type.deploysInto = reference(r, id, "DeploysInto");
    type.undeploysInto = reference(r, id, "UndeploysInto");
    type.factory = reference(r, id, "Factory");
    if(category==TypeCategory::Building) {
      if(!reference(r,id,"SuperWeapon").empty())type.pending.push_back("superweapon charging/activation");
      if(r.boolean(id,"SpySat") || r.boolean(id,"GapGenerator"))type.pending.push_back("fog of war/map visibility");
      if(r.boolean(id,"SensorArray"))type.pending.push_back("psychic detection overlay");
      if(reference(r,id,"Secondary")=="PRISMSUPPORT")type.pending.push_back("prism support network");
    }
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
    for(int slot=1;slot<=weaponCount;++slot) {
      const auto field="Weapon"+std::to_string(slot)+"FLH";const auto values=a.list(type.image,field);
      auto point=type.primaryFireFlh;
      if(!values.empty() && values.size()!=3)a.invalid(type.image,field,"expected Forward,Lateral,Height");
      for(std::size_t i=0;i<values.size();++i) {
        point[i]=IniDocument::parse("[P]\nN="+values[i]).integer("P","N");
        if(point[i]<-100000 || point[i]>100000)a.invalid(type.image,field,"coordinate outside supported range");
      }
      type.weaponFireFlh.push_back(point);
    }
    if(flh.empty() && !type.weaponFireFlh.empty())type.primaryFireFlh=type.weaponFireFlh.front();
    for (auto& item : type.prerequisites) item = canonicalId(item);
    for (auto& item : type.owners) item = canonicalId(item);
    const auto armor = canonicalId(r.string(id, "Armor", "none"));
    const auto armorIt = std::find(std::begin(armors), std::end(armors), armor);
    if (armorIt == std::end(armors)) type.unsupported.push_back("Armor=" + armor);
    else type.armor = static_cast<int>(armorIt - std::begin(armors));
    const auto locomotor = reference(r,id,"Locomotor");
    type.groundMobile = category == TypeCategory::Vehicle &&
      (locomotor == "{4A582741-9839-11D1-B709-00A024DDAFD1}" ||
       (type.naval && locomotor=="{2BEA74E1-7CCA-11D3-BE14-00104B62A16C}") || type.hover ||
       (type.harvester && type.harvester->teleportReturn && locomotor == "{4A582747-9839-11D1-B709-00A024DDAFD1}"));
    if((id=="ORCA" || id=="BEAG") && locomotor=="{4A582746-9839-11D1-B709-00A024DDAFD1}" && r.boolean(id,"AirportBound")) {
      AircraftDef aircraft;aircraft.fixedWing=true;aircraft.ammo=bounded(id,"Ammo",1,1,100);
      aircraft.reloadFrames=std::clamp(int(r.number("General","ReloadRate",.3)*900+.5),1,36000);
      aircraft.movement={type.speed,20,40,10,type.rot,type.flightLevel};
      aircraft.docks=r.list(id,"Dock");for(auto& dock:aircraft.docks)dock=canonicalId(dock);
      type.aircraft=std::move(aircraft);
    }
    if((id=="SHAD" || id=="ZEP") && locomotor=="{92612C46-F71F-11D1-AC9F-006008055BB5}") {
      AircraftDef aircraft;aircraft.balloon=r.boolean(id,"BalloonHover");
      aircraft.movement={bounded(id,"JumpjetSpeed",30,1,255),bounded(id,"JumpjetClimb",20,1,256),bounded(id,"JumpjetCrash",25,1,256),
        bounded(id,"JumpjetAccel",10,1,255),bounded(id,"JumpjetTurnRate",10,1,127),bounded(id,"JumpjetHeight",500,1,8192)};
      type.aircraft=std::move(aircraft);
    }
    if(category==TypeCategory::Building && r.boolean(id,"Helipad") && r.boolean(id,"UnitReload")) {
      AirportDef airport;const int count=bounded(id,"NumberOfDocks",1,1,8);
      for(int slot=0;slot<count;++slot) {
        const auto field="DockingOffset"+std::to_string(slot);const auto values=a.list(type.image,field);
        if(values.size()!=3)a.invalid(type.image,field,"expected X,Y,Z dock offset in leptons");
        std::array<int,3> offset{};for(int axis=0;axis<3;++axis) {
          offset[axis]=IniDocument::parse("[P]\nN="+values[axis]).integer("P","N");
          if(std::abs(offset[axis])>8192)a.invalid(type.image,field,"dock outside supported range");
        }
        airport.offsets.push_back(offset);
      }
      type.airport=std::move(airport);type.factoryExit=FactoryExitDef{};
    }
    if(category==TypeCategory::Infantry) {
      type.infantry=readInfantryArt(a,type.image);
      auto& foot=*type.infantry;
      foot.fearless=r.boolean(id,"Fearless") || (type.leap && type.leap->infantryOnly); foot.deployer=r.boolean(id,"Deployer");
      foot.crushable=r.boolean(id,"Crushable",true); foot.deployedCrushable=r.boolean(id,"DeployedCrushable");
      type.groundMobile=!type.naval && (locomotor=="{4A582744-9839-11D1-B709-00A024DDAFD1}" || (type.teleporter && locomotor=="{4A582747-9839-11D1-B709-00A024DDAFD1}"));
      if(r.boolean(id,"JumpJet") && locomotor=="{92612C46-F71F-11D1-AC9F-006008055BB5}") {
        FlightDef flight;
        const auto value=[&](std::string_view field,int fallback,int maximum) {
          const int n=r.integer(id,field,fallback);if(n<1 || n>maximum)r.invalid(id,field,"flight parameter outside supported range");return n;
        };
        flight.speed=value("JumpjetSpeed",30,255);flight.climb=value("JumpjetClimb",20,256);flight.crash=value("JumpjetCrash",25,256);
        flight.acceleration=value("JumpjetAccel",10,255);flight.turnRate=value("JumpjetTurnRate",10,127);flight.height=value("JumpjetHeight",500,4096);
        type.flight=flight;
        if(!r.boolean(id,"BalloonHover"))type.unsupported.push_back("landing jumpjet locomotion");
        for(const auto* sequence:{"FLY","HOVER","FIREFLY","AIRDEATHSTART","AIRDEATHFALLING","AIRDEATHFINISH"})
          if(!foot.sequences.contains(sequence))type.unsupported.push_back(std::string("missing flight sequence ")+sequence);
      }
      if(!replica.find("Visual."+id,"Body") || !foot.sequences.contains("READY"))
        type.unsupported.push_back("infantry visuals/capability not enabled");
    }
    if (category != TypeCategory::Building && !type.groundMobile && !type.flight && !type.missileSpawn && !type.spawnedAircraft && !type.aircraft) type.unsupported.push_back("locomotor/terrain capability");
    for (const auto* flag : {"Cloakable", "IsTilter", "Spawns", "ResourceGatherer"}) {
      // IsTilter affects presentation only. Other advanced capabilities must not be silently emulated as tanks.
      if (std::string_view(flag) == "IsTilter") continue;
      if(std::string_view(flag)=="Cloakable" && type.cloakable)continue;
      if(std::string_view(flag)=="Spawns" && (type.missileLauncher || type.navalSpawn))continue;
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
    if(type.factory=="UNITTYPE" && type.naval)type.factoryExit=FactoryExitDef{};
    if(type.factory=="INFANTRYTYPE") {
      FactoryExitDef exit;
      // The original barracks flags select exits relative to the foundation origin.
      exit.path.push_back(r.boolean(id,"NODBarracks")?TileCoord{2,2}:TileCoord{1,2});
      type.factoryExit=std::move(exit);
    }
    type.visual = {replica.string(visualId, "Folder"), replica.string(visualId, "Body"), replica.string(visualId, "Turret"),
                   replica.string(visualId, "Barrel"), replica.string(visualId, "Cameo", a.string(type.image, "Cameo")),
                   replica.string(visualId, "Faction")};
    // Original vehicle rules decide whether turret/barrel voxels exist in the
    // rendered model. A leftover asset name must not add the unused RTNK
    // turret onto MGTK (Turret=no, frontal weapon, complete rtnk body).
    if(category==TypeCategory::Vehicle && !type.turret) {
      type.visual.turret.clear();
      type.visual.barrel.clear();
    }
    type.visual.kind=type.visual.body.empty()?VisualKind::None:
      (canonicalId(std::filesystem::path(type.visual.body).extension().string())==".SHP"?VisualKind::Sprite:VisualKind::Voxel);
    if(category!=TypeCategory::Building) {
      if(r.boolean(id,"Infiltrate") && !type.spy)type.unsupported.push_back("capability Infiltrate");
      if(type.spy)type.pending.push_back("radar shroud / superweapon reset (systems pending)");
      if(r.boolean(id,"DisguiseWhenStill") && !type.disguiseWhenStill)type.unsupported.push_back("capability DisguiseWhenStill");
      if(category==TypeCategory::Vehicle && type.visual.kind==VisualKind::Sprite) {
        if((type.leap && !type.leap->infantryOnly) || id=="DLPH") {
          InfantryDef body;const int walk=a.integer(type.image,"WalkFrames",6),fire=a.integer(type.image,"FiringFrames",4);
          if(walk<1 || walk>100 || fire<1 || fire>100)a.invalid(type.image,"WalkFrames/FiringFrames","expected 1..100");
          body.sequences["WALK"]={0,walk,walk,-1,id=="SQD"?r.integer(id,"WalkRate",2):id=="DLPH"?r.integer(id,"WalkRate",4):3};
          body.sequences["READY"]={walk*8,1,1};
          body.sequences["FIREUP"]={walk*8+8,fire,fire};
          if(id=="DLPH")body.sequences["FIREUP"].clockwise=true;
          type.spriteVehicle=std::move(body);
        } else type.unsupported.push_back("sprite vehicle renderer");
      }
      if(type.passengers>0 && !type.groundMobile && !type.aircraft)type.pending.push_back("transport locomotion");
      if(r.boolean(id,"Trainable",true))type.pending.push_back("veterancy / elite weapons");
      if(r.boolean(id,"SelfHealing") && !type.selfHealing)type.pending.push_back("self healing");
    }
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
    std::vector<std::string> armaments;
    if(!type.primary.empty()) armaments.push_back(type.primary);
    if(!type.secondary.empty()) armaments.push_back(type.secondary);
    if(!type.deathWeapon.empty())armaments.push_back(type.deathWeapon);
    for(const auto& slot:type.weaponSlots)if(!slot.empty())armaments.push_back(slot);
    std::sort(armaments.begin(),armaments.end());armaments.erase(std::unique(armaments.begin(),armaments.end()),armaments.end());
    for(const auto& weaponId:armaments) {
    if (!r.contains(weaponId)) { type.unsupported.push_back("missing weapon=" + weaponId); continue; }
    if (!result.weapons_.contains(weaponId)) {
      WeaponDef weapon;
      weapon.id = weaponId;
      weapon.neverUse=r.boolean(weaponId,"NeverUse");
      weapon.disarmsBomb=r.boolean(reference(r,weaponId,"Warhead"),"BombDisarm");
      weapon.plantsBomb=r.boolean(reference(r,weaponId,"Warhead"),"IvanBomb");
      weapon.cellRangefinding=r.boolean(weaponId,"CellRangefinding");
      weapon.turboBoost=r.boolean(weaponId,"TurboBoost");weapon.omniFire=r.boolean(weaponId,"OmniFire");weapon.decloakToFire=r.boolean(weaponId,"DecloakToFire",true);
      weapon.sonic=r.boolean(weaponId,"IsSonic");weapon.ambientDamage=r.integer(weaponId,"AmbientDamage");
      weapon.temporal=r.boolean(reference(r,weaponId,"Warhead"),"Temporal");
      weapon.disguise=r.boolean(reference(r,weaponId,"Warhead"),"MakesDisguise");
      weapon.sapper=r.boolean(weaponId,"SabotageCursor");
      weapon.interactionOnly=weapon.neverUse;
      weapon.repairs=r.integer(weaponId,"Damage")<0 && canonicalId(r.string(weaponId,"Warhead"))=="MECHANICAL";
      weapon.damage = r.integer(weaponId, "Damage");
      weapon.rof = r.integer(weaponId, "ROF", 1);
      weapon.burst = r.integer(weaponId, "Burst", 1);
      weapon.rangeMilliCells = scaled(r, weaponId, "Range", 1000);
      if(weaponId=="ASWLAUNCHER" && weapon.rangeMilliCells==-2000)weapon.rangeMilliCells=100000;
      weapon.minimumRangeMilliCells=scaled(r,weaponId,"MinimumRange",1000);
      if(weapon.minimumRangeMilliCells<0 || weapon.minimumRangeMilliCells>100000)
        r.invalid(weaponId,"MinimumRange","expected 0..100 cells");
      weapon.speed = r.integer(weaponId, "Speed", 40);
      weapon.muzzleAnimations=r.list(weaponId,"Anim");
      for(auto& name:weapon.muzzleAnimations) name=canonicalId(name);
      if(!weapon.muzzleAnimations.empty()) weapon.muzzleAnimation=weapon.muzzleAnimations.front();
      weapon.projectile = reference(r, weaponId, "Projectile");
      weapon.warhead = reference(r, weaponId, "Warhead");
      weapon.radiationBeam=r.boolean(weaponId,"IsRadBeam");weapon.areaFire=r.boolean(weaponId,"AreaFire");weapon.spawner=r.boolean(weaponId,"Spawner");
      weapon.suicide=r.boolean(weaponId,"Suicide");
      weapon.radiationLevel=r.integer(weaponId,"RadLevel");if(weapon.radiationLevel<0 || weapon.radiationLevel>10000)r.invalid(weaponId,"RadLevel","expected 0..10000");
      weapon.charges=r.boolean(weapon.warhead,"ElectricAssault");
      weapon.leap=r.boolean(weaponId,"LimboLaunch") && r.boolean(weapon.warhead,"Parasite") && (weaponId=="BADTEETH" || weaponId=="GOODTEETH" || weaponId=="DRONEJUMP" || weaponId=="SQUIDGRAB");
      if (r.contains(weapon.projectile)) result.projectiles_.try_emplace(weapon.projectile,
        ProjectileDef{weapon.projectile, r.string(weapon.projectile, "Image"), r.boolean(weapon.projectile, "Arcing"),
                      r.boolean(weapon.projectile,"Inviso"),
                      r.boolean(weapon.projectile,"SubjectToCliffs"),r.boolean(weapon.projectile,"SubjectToElevation"),
                      r.boolean(weapon.projectile,"SubjectToWalls"),r.integer(weapon.projectile,"ROT")>0,
                      r.boolean(weapon.projectile,"AG",true),r.boolean(weapon.projectile,"AA"),r.integer(weapon.projectile,"ROT"),
                      r.boolean(weapon.projectile,"Inaccurate"),r.boolean(weapon.projectile,"FlakScatter"),
                      reference(r,weapon.projectile,"ShrapnelWeapon"),r.integer(weapon.projectile,"ShrapnelCount")});
      if(auto found=result.projectiles_.find(weapon.projectile);found!=result.projectiles_.end()) {
        found->second.waterBound=replica.boolean("Projectile."+weapon.projectile,"WaterBound");
        found->second.voxel=a.boolean(canonicalId(found->second.image),"Voxel");
        found->second.vertical=r.boolean(weapon.projectile,"Vertical");found->second.acceleration=r.integer(weapon.projectile,"Acceleration");
      }
      if(r.contains(weapon.warhead) && !result.warheads_.contains(weapon.warhead))
        result.warheads_.emplace(weapon.warhead,readWarhead(r,weapon.warhead));
      weapon.supported = !weapon.interactionOnly && result.projectiles_.contains(weapon.projectile) && result.warheads_.contains(weapon.warhead) &&
        (result.projectiles_.at(weapon.projectile).antiGround || result.projectiles_.at(weapon.projectile).antiAir) &&
        (result.projectiles_.at(weapon.projectile).arcing || result.projectiles_.at(weapon.projectile).invisible || result.projectiles_.at(weapon.projectile).homing || result.projectiles_.at(weapon.projectile).vertical) &&
        (weapon.damage>0 || weapon.repairs) && weapon.damage>=-100000 && weapon.damage<=100000 &&
        weapon.rof > 0 && weapon.rof <= 100000 && weapon.burst>=1 && weapon.burst<=8 && weapon.speed > 0 && weapon.speed <= 10000 &&
        weapon.rangeMilliCells > 0 && weapon.rangeMilliCells <= 100000;
      weapon.laser=r.boolean(weaponId,"IsLaser");weapon.houseColor=r.boolean(weaponId,"IsHouseColor");
      weapon.electricBolt=r.boolean(weaponId,"IsElectricBolt");weapon.alternateBoltColor=r.boolean(weaponId,"IsAlternateColor");
      weapon.laserDuration=r.integer(weaponId,"LaserDuration",15);
      weapon.disguiseBlinkFrames=r.integer(weaponId,"DisguiseFakeBlinkTime");
      const auto color=r.list(weaponId,"LaserInnerColor");
      if(!color.empty()) {
        if(color.size()!=3)r.invalid(weaponId,"LaserInnerColor","expected R,G,B");
        for(int i=0;i<3;++i){weapon.laserColor[i]=IniDocument::parse("[C]\nN="+color[i]).integer("C","N");if(weapon.laserColor[i]<0 || weapon.laserColor[i]>255)r.invalid(weaponId,"LaserInnerColor","expected 0..255");}
      }
      if(weapon.laserDuration<1 || weapon.laserDuration>900 || weapon.disguiseBlinkFrames<0 || weapon.disguiseBlinkFrames>900)
        r.invalid(weaponId,"LaserDuration/DisguiseFakeBlinkTime","duration outside supported range");
      if(weapon.laser && (!result.projectiles_.contains(weapon.projectile) || !result.projectiles_.at(weapon.projectile).invisible))weapon.supported=false;
      if(r.boolean(weaponId,"LimboLaunch") && !weapon.leap)weapon.supported=false;
      if((weapon.suicide || weapon.plantsBomb || weapon.disarmsBomb) && (!result.projectiles_.contains(weapon.projectile) || !result.projectiles_.at(weapon.projectile).invisible || weapon.burst!=1 || weapon.repairs || weapon.leap || weapon.spawner))weapon.supported=false;
      if(weapon.sapper && result.projectiles_.contains(weapon.projectile) && result.projectiles_.at(weapon.projectile).invisible && weapon.damage>0 && weapon.rof>0 && weapon.rangeMilliCells>0)weapon.supported=true;
      if(weapon.disguise && weapon.rangeMilliCells==-2000 && weapon.damage>0 && result.projectiles_.contains(weapon.projectile) && result.warheads_.contains(weapon.warhead))weapon.supported=true;
      if(r.boolean(weaponId,"FireOnce") && !weapon.plantsBomb && !weapon.disarmsBomb && !weapon.disguise)weapon.supported=false;
      if(weaponId=="HORNETBOMB" && result.projectiles_.contains(weapon.projectile) && result.warheads_.contains(weapon.warhead))weapon.supported=true;
      if(weapon.sonic && weapon.rangeMilliCells>0 && result.warheads_.contains(weapon.warhead))weapon.supported=true;
      if(weapon.spawner && (weaponId=="HORNETLAUNCHER" || weaponId=="ASWLAUNCHER" || weaponId=="DREDLAUNCHER"))weapon.supported=true;
      for (const auto* special : {"IsRailgun"})
        if (r.boolean(weaponId, special)) weapon.supported = false;
      result.weapons_.emplace(weapon.id, std::move(weapon));
    }
    const auto& weapon=result.weapons_.at(weaponId);
    if(!weapon.supported && !((type.engineer || type.leap) && weapon.interactionOnly)) {
      if(type.gunner)type.pending.push_back("passenger weapon "+weaponId);
      else if(weaponId==type.primary || weaponId==type.deathWeapon || type.infantry || !type.weaponSlots.empty())type.unsupported.push_back("weapon capability " + weaponId);
      else type.pending.push_back("secondary weapon "+weaponId);
    }
    }
  }
  if(r.contains(bomb.warhead)) {
  result.warheads_.try_emplace(bomb.warhead,readWarhead(r,bomb.warhead));
  ProjectileDef blast;blast.id="@EXPLOSION";blast.invisible=true;blast.antiAir=true;result.projectiles_.emplace(blast.id,blast);
  WeaponDef payload;payload.id=bomb.weapon;payload.projectile=blast.id;payload.warhead=bomb.warhead;payload.damage=bomb.damage;payload.supported=true;
  result.weapons_.emplace(payload.id,payload);
  for(auto& [id,type]:result.units_)if(type.explodes && type.deathWeapon.empty()) {
    WeaponDef death=payload;death.id="@DEATH."+id;
    if(type.ivan && result.weapons_.contains(type.primary))death.damage=result.weapon(type.primary).damage;
    else {
      const auto fallback=reference(r,"CombatDamage","DeathWeapon","DEFAULTDEATHWEAPON");
      death.warhead=reference(r,fallback,"Warhead","DEATHWH");
      death.damage=int(std::int64_t(type.strength)*profile.defaultDeathDamagePercent/100);
      if(!r.contains(death.warhead))r.invalid(fallback,"Warhead","missing death warhead");
      result.warheads_.try_emplace(death.warhead,readWarhead(r,death.warhead));
    }
    type.collateralWeapon=death.id;result.weapons_.emplace(death.id,std::move(death));
  }
  } else if(std::any_of(result.weapons_.begin(),result.weapons_.end(),[](const auto& item){return item.second.plantsBomb;}))r.invalid("CombatDamage","IvanWarhead","missing warhead");
  if(r.contains(rad.warhead))result.warheads_.try_emplace(rad.warhead,readWarhead(r,rad.warhead));
  if(r.contains("V3WH")) {
    result.warheads_.try_emplace("V3WH",readWarhead(r,"V3WH"));
    ProjectileDef projectile;projectile.id="@V3IMPACT";projectile.invisible=true;result.projectiles_.emplace(projectile.id,projectile);
    WeaponDef impact;impact.id=missile.impactWeapon;impact.projectile=projectile.id;impact.warhead="V3WH";impact.damage=missile.damage;impact.rangeMilliCells=18000;impact.supported=true;
    result.weapons_.emplace(impact.id,impact);
  }
  if(r.contains("DMISLWH")) {
    const auto wh=reference(r,"CombatDamage","DMislWarhead","DMISLWH");result.warheads_.try_emplace(wh,readWarhead(r,wh));
    WeaponDef payload;payload.id=dread.impactWeapon;payload.projectile="@V3IMPACT";payload.warhead=wh;payload.damage=dread.damage;payload.supported=true;
    result.weapons_.emplace(payload.id,std::move(payload));
  }
  if(result.weapons_.contains("SONICZAP")) {
    auto ambient=result.weapon("SONICZAP");ambient.id="@SONICAMBIENT";ambient.damage=ambient.ambientDamage;ambient.projectile="@V3IMPACT";
    result.weapons_.emplace(ambient.id,std::move(ambient));
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
    value.unitPalette=a.boolean(id,"AltPalette");
    value.groundLayer=canonicalId(a.string(id,"Layer"))=="GROUND" || canonicalId(a.string(id,"Layer"))=="SURFACE";
    value.translucency=a.integer(id,"Translucency",value.translucent?50:0);value.yDrawOffset=a.integer(id,"YDrawOffset",0);
    if(value.translucency<0 || value.translucency>100 || std::abs(value.yDrawOffset)>1024)a.invalid(id,"Translucency/YDrawOffset","invalid effect appearance");
    value.loopStart=a.integer(id,"LoopStart",value.start);value.loopEnd=a.integer(id,"LoopEnd",-1);value.loopCount=a.integer(id,"LoopCount",1);
    if(value.loopStart<value.start || value.loopStart>65535 || value.loopEnd<-1 || value.loopEnd>65535 ||
        (value.loopEnd>=0 && (value.loopEnd<value.loopStart || (value.end>=0 && value.loopEnd>value.end))) || value.loopCount<1 || value.loopCount>1000)
      a.invalid(id,"LoopStart/LoopEnd/LoopCount","invalid animation loop");
    result.effectAnimations_.emplace(id,std::move(value));
  };
  for (const auto& [id,weapon]:result.weapons_) { (void)id; for(const auto& name:weapon.muzzleAnimations) animation(name); }
  for (auto& [id,warhead]:result.warheads_) {
    const auto section="WarheadVisual."+id;
    if(replica.find(section,"AnimList")) {
      warhead.animations=replica.list(section,"AnimList");
      for(auto& name:warhead.animations)name=canonicalId(name);
    }
    for (const auto& name:warhead.animations) animation(name);
  }
  for(const auto* key:{"WarpIn","WarpOut","WarpAway"})animation(reference(r,"AudioVisual",key,key));
  animation(reference(r,"AudioVisual","Wake","WAKE1"));
  for(const auto& name:r.list("CombatDamage","SplashList"))animation(canonicalId(name));
  animation("BBBLELRG");animation("DURASMOKE");animation("SQDWAKE");animation("SQDG");
  for(const auto* direction:{"N","NE","E","SE","S","SW","W","NW"})animation(std::string("SQDG_")+direction);
  animation("ELECTRO");
  animation("NUKEDIE");animation("V3TAKOFF");animation("V3TRAIL");
  animation(reference(r,"AudioVisual","InfantryExplode","S_BANG34"));
  for(const auto& [id,type]:result.units_)if(type.explodes && !type.deathAnimation.empty()){(void)id;animation(type.deathAnimation);}
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

namespace ra2 {
bool DefinitionRegistry::allowedHouse(const UnitTypeDef& type,std::string_view house) const {
  const auto id=canonicalId(house);
  return (type.requiredHouses.empty() || std::find(type.requiredHouses.begin(),type.requiredHouses.end(),id)!=type.requiredHouses.end()) &&
    std::find(type.forbiddenHouses.begin(),type.forbiddenHouses.end(),id)==type.forbiddenHouses.end();
}
const WeaponDef* DefinitionRegistry::weaponForTarget(const UnitTypeDef& type,bool airborne,bool deployed,int gunnerMode) const {
  const auto matches=[&](std::string_view id)->const WeaponDef* {
    if(id.empty())return nullptr;
    const auto& w=weapon(id);if(!w.supported || w.charges)return nullptr;
    const auto& p=projectile(w.projectile);return (airborne?p.antiAir:p.antiGround)?&w:nullptr;
  };
  if(type.gunner)return gunnerMode>=0 && gunnerMode<static_cast<int>(type.weaponSlots.size())?matches(type.weaponSlots[gunnerMode]):nullptr;
  if(deployed)return matches(type.secondary);
  if(const auto* primary=matches(type.primary))return primary;
  return matches(type.secondary);
}
const WeaponDef* DefinitionRegistry::chargeWeapon(const UnitTypeDef& source,const UnitTypeDef& target) const {
  if(!source.infantry || !target.overpowerable || source.secondary.empty())return nullptr;
  const auto& w=weapon(source.secondary);
  return w.supported && w.charges?&w:nullptr;
}
std::string DefinitionRegistry::playerHouse(std::uint32_t player) const {
  return canonicalId(replica_.string("Players",std::to_string(player),player==0?profile_.playerHouse:"Russians"));
}
bool DefinitionRegistry::buildingTerrainAllowed(const UnitTypeDef& type,std::string_view land) const {
  const auto id=canonicalId(land);
  return type.waterBound?id=="WATER":rules_.boolean(id,"Buildable",id!="WATER" && id!="BEACH" && id!="ROCK");
}
bool DefinitionRegistry::targetDomainAllowed(const UnitTypeDef& source,bool airborne,bool water,bool submerged) const {
  if(airborne)return true;
  return !(source.landTargeting==1 && !water) && !(source.navalTargeting==6 && water) &&
    !(source.navalTargeting==0 && submerged) && !(source.navalTargeting==2 && !submerged);
}
bool DefinitionRegistry::navalSecondary(const UnitTypeDef& source,const UnitTypeDef& target,bool water,bool submerged) const {
  return (source.navalTargeting==1 && submerged) || (source.navalTargeting==3 && target.organic) ||
    (source.navalTargeting==4 && water && !target.organic && !target.hover && target.speedType!="AMPHIBIOUS");
}
bool DefinitionRegistry::factoryMatches(const UnitTypeDef& factory,const UnitTypeDef& unit) const {
  if(factory.naval!=unit.naval || !((factory.factory=="INFANTRYTYPE" && unit.category==TypeCategory::Infantry) ||
      (factory.factory=="UNITTYPE" && unit.category==TypeCategory::Vehicle) ||
      (factory.airport && unit.aircraft && unit.aircraft->fixedWing && std::find(unit.aircraft->docks.begin(),unit.aircraft->docks.end(),factory.id)!=unit.aircraft->docks.end())))return false;
  if(!unit.visual.faction.empty()) {
    const int side=rules_.integer(factory.id,"AIBasePlanningSide",-1);
    if((side==0 && canonicalId(unit.visual.faction)!="ALLIED") || (side==1 && canonicalId(unit.visual.faction)!="SOVIET"))return false;
  }
  return factory.owners.empty() || unit.owners.empty() || std::any_of(factory.owners.begin(),factory.owners.end(),[&](const auto& house) {
    return allowedHouse(unit,house) && std::find(unit.owners.begin(),unit.owners.end(),house)!=unit.owners.end();
  });
}
bool DefinitionRegistry::sidebarFactoryMatches(const UnitTypeDef& factory,const UnitTypeDef& unit,std::string_view faction) const {
  const auto side=canonicalId(faction);
  const int index=side=="ALLIED"?0:side=="SOVIET"?1:-1;
  // Owner lists countries permitted to own a structure; the stock factories
  // include both sides there. Use its explicit side for the mixed sidebar.
  return index>=0 && rules_.integer(factory.id,"AIBasePlanningSide",-1)==index && factoryMatches(factory,unit);
}
const UnitTypeDef* DefinitionRegistry::sidebarUnit(std::string_view cameo,std::string_view faction) const {
  const auto icon=canonicalId(cameo),side=canonicalId(faction);
  const UnitTypeDef* fallback=nullptr;
  constexpr std::string_view allied[]{"BRITISH","FRENCH","GERMANS","AMERICANS","ALLIANCE"};
  constexpr std::string_view soviet[]{"RUSSIANS","CONFEDERATION","AFRICANS","ARABS"};
  for(const auto& [id,type]:units_) {
    (void)id;if(canonicalId(type.visual.cameo)!=icon)continue;
    if(!type.visual.faction.empty()) {if(canonicalId(type.visual.faction)==side)return &type;continue;}
    const auto matches=[&](auto countries) {
      for(auto country:countries)if(allowedHouse(type,country) && std::find(type.owners.begin(),type.owners.end(),country)!=type.owners.end())return true;
      return false;
    };
    // Campaign actors reuse combat cameos (PRES/PENTGEN use SHKICON).
    // An explicit runtime mapping takes precedence over those art fallbacks.
    if(!fallback && (side=="ALLIED"?matches(std::span{allied}):matches(std::span{soviet})))fallback=&type;
  }
  return fallback;
}
}
