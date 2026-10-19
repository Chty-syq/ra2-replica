#pragma once
#include "ini_document.h"
#include "map_grid.h"
#include "simulation/game_clock.h"
#include "infantry_art.h"
#include <array>
#include <map>
#include <optional>

namespace ra2 {
enum class TypeCategory { Vehicle, Building, Infantry, Aircraft };
enum class VisualKind { None, Sprite, Voxel };
enum class SupportStatus { Ready, Partial, Unsupported };
struct VisualDef {
  std::string folder, body, turret, barrel, cameo, faction;
  VisualKind kind=VisualKind::None;
};
struct WeaponDef {
  std::string id, projectile, warhead;
  std::string muzzleAnimation;
  std::vector<std::string> muzzleAnimations;
  int damage = 0, rof = 1, burst = 1, speed = 40;
  int rangeMilliCells = 0, minimumRangeMilliCells=0;
  bool supported = false;
  bool neverUse=false, interactionOnly=false;
  bool repairs=false, charges=false, radiationBeam=false, areaFire=false, spawner=false, leap=false, suicide=false;
  bool plantsBomb=false,disarmsBomb=false,cellRangefinding=false,omniFire=false,decloakToFire=true,turboBoost=false;
  bool temporal=false,disguise=false,sapper=false,sonic=false;
  int ambientDamage=0;
  int radiationLevel=0;
  bool laser=false, houseColor=false, electricBolt=false, alternateBoltColor=false;
  int laserDuration=15, disguiseBlinkFrames=0;
  std::array<int,3> laserColor{255,0,0};
};
struct ProjectileDef {
  std::string id, image;
  bool arcing = false, invisible = false;
  bool subjectToCliffs = false, subjectToElevation = false, subjectToWalls = false;
  bool homing=false, antiGround=true, antiAir=false;
  int turnRate=0;
  bool inaccurate=false, flakScatter=false;
  std::string shrapnelWeapon;
  int shrapnelCount=0;
  bool waterBound=false,voxel=false,vertical=false;
  int acceleration=0;
};
struct ResourceDef { std::string id; int value = 0, image = 1; };
struct EffectAnimationDef {
  std::string image;
  int start = 0, end = -1, frameCount = 0, frameDelay = 1;
  bool normalized = false, translucent = false;
  bool groundLayer=false,unitPalette=false;
  int translucency=0,yDrawOffset=0;
  int loopStart=0, loopEnd=-1, loopCount=1;
  int extraFrames() const { return loopEnd>=loopStart?(loopEnd-loopStart+1)*(loopCount-1):0; }
  int frame(int elapsedFrames) const {
    const int loopLength=loopEnd-loopStart+1;
    if(elapsedFrames<loopStart-start)return start+elapsedFrames;
    if(loopLength>0 && elapsedFrames>=loopStart-start && elapsedFrames<loopEnd-start+1+extraFrames())
      return loopStart+(elapsedFrames-(loopStart-start))%loopLength;
    return start+elapsedFrames-extraFrames();
  }
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
  int proneDamagePercent=100, infantryDeath=0;
  bool damagesWalls=false, radiation=false,sonic=false,culling=false;
};
struct RadiationDef {
  int durationMultiple=1,applicationDelay=16,levelMax=500,levelDelay=90,lightDelay=90;
  int damageFactorMillionths=200000,lightFactorMillionths=100000,tintFactorMillionths=1000000;
  std::array<int,3> color{0,255,0};
  std::string warhead="RADSITE";
};
struct IvanBombDef {
  int damage=400,delay=450,flickerFrames=8;
  std::string warhead="IVANWH",weapon="@IVANBOMB";
};
struct MissileDef {
  int pauseFrames=0,tiltFrames=60,initialPitch=3441,finalPitch=8192,turnRate=819;
  int accelerationMilli=400,altitude=768,damage=200,bodyLength=256;
  bool lazyCurve=true;
  std::string type="V3ROCKET",impactWeapon="@V3IMPACT";
};
struct MissileLauncherDef {std::string type;int regenerateFrames=400;bool alternateBody=true;};
struct NavalSpawnDef {
  std::string type;
  int count=1,regenerateFrames=400,reloadFrames=150;
  bool missile=false,alternateBody=false;
};
struct LeapDef {
  std::string image;
  bool infantryOnly=true;
  bool naval=false;
  int scanRange=5000,suppressionThreshold=5;
};
struct FactoryExitDef {
  // Cell offsets from the northwest Foundation start, including the internal spawn cell.
  std::vector<TileCoord> path;
  int prepareFrames = 0, clearFrames = 0;
};
struct FlightDef {
  int speed=30, climb=20, crash=25, acceleration=10, turnRate=10, height=500;
};
struct AircraftDef {
  bool fixedWing=false,balloon=false;
  FlightDef movement;
  int ammo=-1,reloadFrames=270;
  std::vector<std::string> docks;
};
struct AirportDef {std::vector<std::array<int,3>> offsets;};
struct UnitTypeDef {
  std::string id, image, primary, secondary, deploysInto, undeploysInto, factory, deathWeapon,collateralWeapon,deathAnimation;
  TypeCategory category = TypeCategory::Vehicle;
  int strength = 1, armor = 0, speed = 0, rot = 0, cost = 0, techLevel = -1, power = 0;
  int foundationWidth = 1, foundationHeight = 1;
  int productionFrames = 54;
  bool waterBound=false,hover=false,underwater=false,cloakable=false,unitRepair=false;
  bool spawnedAircraft=false,typeImmune=false;
  int navalTargeting=0,landTargeting=0,sensorsRange=0,cloakingSpeed=1,flightLevel=0;
  bool turret = false, naval = false, groundMobile = false, powered=false, wall=false, overpowerable=false;
  std::string buildingTurret;
  int fireDelay=0;
  bool opportunityFire = false, isTilter = false;
  bool canPassiveAcquire = true;
  bool crusher=false;
  bool disguiseWhenStill=false, detectDisguise=false, canApproachTarget=true;
  int detectDisguiseRange=0, chargeTurrets=0;
  std::vector<std::string> disguises;
  bool engineer=false, capturable=false, repairable=true;
  bool immuneToRadiation=false,deployFire=false,missileSpawn=false;
  bool parasiteable=true,organic=false,explodes=false;
  bool ivan=false,bombable=true,selfHealing=false;
  bool c4=false,canC4=true,spy=false,spyable=false,canDisguise=false,teleporter=false,warpable=true;
  bool trainable=true;
  std::vector<std::string> veteranAbilities;
  std::string arcticBody;
  int bombSight=0;
  bool clickRepairable=true, sellable=true;
  bool occupier=false, garrisonable=false, gunner=false;
  int passengers=0, size=1, sizeLimit=0, maxOccupants=0, ifvMode=0;
  std::map<int,int> gunnerTurrets;
  std::vector<std::array<int,2>> garrisonMuzzles;
  int garrisonMuzzleHeight=0;
  int airRangeBonus=0, buildingHeight=0;
  std::string speedType = "TRACK";
  std::array<int, 3> primaryFireFlh{0,0,0};
  std::vector<std::array<int,3>> weaponFireFlh;
  std::vector<std::string> prerequisites, owners, unsupported;
  std::vector<std::string> requiredHouses, forbiddenHouses, weaponSlots, pending;
  VisualDef visual;
  std::optional<FactoryExitDef> factoryExit;
  std::optional<InfantryDef> infantry;
  std::optional<HarvesterDef> harvester;
  std::optional<RefineryDef> refinery;
  std::optional<FlightDef> flight;
  std::optional<AircraftDef> aircraft;
  std::optional<AirportDef> airport;
  std::optional<MissileLauncherDef> missileLauncher;
  std::optional<NavalSpawnDef> navalSpawn;
  // Sprite vehicles retain vehicle occupancy and commands; this is art only.
  std::optional<InfantryDef> spriteVehicle;
  std::optional<LeapDef> leap;
  SupportStatus support() const {
    if(!unsupported.empty() || (category!=TypeCategory::Building && visual.kind==VisualKind::None))return SupportStatus::Unsupported;
    return pending.empty()?SupportStatus::Ready:SupportStatus::Partial;
  }
  // A zero-drain defense (NALASR) keeps working during a base outage even
  // though the stock INI retains Powered=yes. Preserve both original fields.
  bool requiresPower() const {return powered && power<0;}
};
// Rule-derived timing plus explicit demo switches. Movement conversions live in compat/.
struct SimulationProfile {
  GameSpeed defaultSpeed{};
  int buildSpeedMillionths = 700000, buildupFrames = 54;
  int deployFrames = 90; // MCV choreography still requires an original-game trace.
  int productionFramesOverride = 0; // 0 = rules; nonzero is an explicit test/demo override.
  int pathRetryFrames = 60, blockedRetryFrames = 16;
  int startingCredits = 50000;
  int repairPercentMillionths=150000, refundPercentMillionths=500000;
  int repairFrames=14, repairStep=8;
  int selfHealCapMillionths=500000,defaultDeathDamagePercent=5;
  int conditionRedMillionths=250000,conditionYellowMillionths=500000;
  int gravity = 6;
  int ballisticScatter=1000;
  // Compatibility hit shape, independent of the pathfinding subcell. OpenRA
  // RA2 uses radius 128/1024 cell, equivalent to 32 RA2 leptons.
  int infantryHitRadiusLeptons=32;
  bool sandbox = true;
  int engineerEntryFrames=12;
  int unloadFrames=8;
  std::string playerHouse="BRITISH";
};
class DefinitionRegistry {
public:
  static DefinitionRegistry load(const std::filesystem::path& settings);
  static DefinitionRegistry compile(IniDocument rules, IniDocument art, IniDocument replica = {});
  const UnitTypeDef& unit(std::string_view id) const;
  const WeaponDef& weapon(std::string_view id) const;
  const WeaponDef* chargeWeapon(const UnitTypeDef& source,const UnitTypeDef& target) const;
  const RadiationDef& radiation() const {return radiation_;}
  const IvanBombDef& ivanBomb() const {return ivanBomb_;}
  const MissileDef& missile() const {return missile_;}
  const MissileDef& missileFor(std::string_view type) const {return canonicalId(type)==dreadMissile_.type?dreadMissile_:missile_;}
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
  const auto& effectAnimations() const {return effectAnimations_;}
  const UnitTypeDef* sidebarUnit(std::string_view cameo,std::string_view faction) const;
  bool allowedHouse(const UnitTypeDef&,std::string_view house) const;
  bool buildingTerrainAllowed(const UnitTypeDef&,std::string_view land) const;
  bool targetDomainAllowed(const UnitTypeDef& source,bool airborne,bool water,bool submerged) const;
  bool navalSecondary(const UnitTypeDef& source,const UnitTypeDef& target,bool water,bool submerged) const;
  bool factoryMatches(const UnitTypeDef& factory,const UnitTypeDef& unit) const;
  bool sidebarFactoryMatches(const UnitTypeDef& factory,const UnitTypeDef& unit,std::string_view faction) const;
  std::string playerHouse(std::uint32_t player) const;
  const WeaponDef* weaponForTarget(const UnitTypeDef&,bool airborne,bool deployed=false,int gunnerMode=0) const;
private:
  RadiationDef radiation_;
  IvanBombDef ivanBomb_;
  MissileDef missile_;
  MissileDef dreadMissile_;
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
