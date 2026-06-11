#pragma once

#include "iso_world.h"
#include "palette.h"
#include "renderer2d.h"
#include "rhino_unit.h"
#include "ui_texture.h"

#include <cstdint>
#include <filesystem>
#include <vector>

struct WeaponVisualAssets {
  UiTexture shell120mm;
  std::vector<UiTexture> gunfireFrames;
  std::vector<UiTexture> explosionFrames;
  std::vector<UiTexture> explosionLargeFrames;
};

struct WeaponRuntimeParams {
  float rangeCells = 5.75f;
  float rofFrames = 65.0f;
  float rofFramesPerSecond = 15.0f;
  float turretTurnSpeedRadiansPerSecond = 5.8f;
  float fireTurnToleranceRadians = 0.08f;
  float fireForwardLeptons = 150.0f;
  float fireLateralLeptons = 0.0f;
  float fireHeightLeptons = 100.0f;
  float arcingSpeedLeptonsPerFrame = 50.0f;
  float gravityLeptonsPerFrameSquared = 1.0f;
  float projectileRulesFramesPerSecond = 30.0f;
  float minDurationRulesFrames = 4.0f;
};

enum class CombatEffectKind {
  Muzzle,
  Explosion
};

struct ProjectileInstance {
  Vec2 startLogical{};
  Vec2 targetLogical{};
  float startHeightLeptons = 0.0f;
  float initialVerticalVelocityLeptonsPerFrame = 0.0f;
  float durationRulesFrames = 1.0f;
  float gravityLeptonsPerFrameSquared = 1.0f;
  float rulesFramesPerSecond = 30.0f;
  float elapsedSeconds = 0.0f;
  float durationSeconds = 0.25f;
};

struct CombatEffectInstance {
  CombatEffectKind kind = CombatEffectKind::Muzzle;
  Vec2 logicalPosition{};
  float heightLeptons = 0.0f;
  float elapsedSeconds = 0.0f;
  float frameDurationSeconds = 0.04f;
  float lifetimeSeconds = 0.16f;
  int variant = 0;
};

[[nodiscard]] WeaponVisualAssets loadWeaponVisualAssets(const std::filesystem::path& effectsRoot,
                                                        const Palette& animationPalette);
void destroyWeaponVisualAssets(WeaponVisualAssets& assets);

[[nodiscard]] bool issueRhinoGroundFireCommand(RhinoUnitState& unit,
                                               const MapGrid& map,
                                               Vec2 targetLogical,
                                               std::uint32_t nowTicks,
                                               const WeaponRuntimeParams& params);
void stopRhinoGroundFire(RhinoUnitState& unit);

void updateRhinoWeapon(RhinoUnitState& unit,
                       std::vector<ProjectileInstance>& projectiles,
                       std::vector<CombatEffectInstance>& effects,
                       float deltaSeconds,
                       std::uint32_t nowTicks,
                       const WeaponRuntimeParams& params);

void updateWeaponProjectilesAndEffects(std::vector<ProjectileInstance>& projectiles,
                                       std::vector<CombatEffectInstance>& effects,
                                       float deltaSeconds);

void drawWeaponProjectilesAndEffects(Renderer2D& renderer,
                                     const WeaponVisualAssets& assets,
                                     const std::vector<ProjectileInstance>& projectiles,
                                     const std::vector<CombatEffectInstance>& effects,
                                     Vec2 origin,
                                     float tileWidth,
                                     float tileHeight);
