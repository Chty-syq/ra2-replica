#include "weapon_system.h"

#include "shp_ts.h"

#include <SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.0f;
constexpr float kLeptonsPerCell = 256.0f;
constexpr float kRangeEpsilonCells = 0.05f;

struct FiringCellCandidate {
  TileCoord cell{};
  float moveDistance = 0.0f;
  float targetDistance = 0.0f;
};

[[nodiscard]] float normalizeAngle(float radians) {
  radians = std::fmod(radians, kTwoPi);
  if (radians < 0.0f) {
    radians += kTwoPi;
  }
  return radians;
}

[[nodiscard]] float shortestAngleDelta(const float fromRadians, const float toRadians) {
  float delta = normalizeAngle(toRadians) - normalizeAngle(fromRadians);
  if (delta > kPi) {
    delta -= kTwoPi;
  } else if (delta < -kPi) {
    delta += kTwoPi;
  }
  return delta;
}

[[nodiscard]] float rotateToward(const float currentRadians,
                                 const float targetRadians,
                                 const float maxStepRadians) {
  const float delta = shortestAngleDelta(currentRadians, targetRadians);
  if (std::fabs(delta) <= maxStepRadians) {
    return normalizeAngle(targetRadians);
  }
  return normalizeAngle(currentRadians + (delta > 0.0f ? maxStepRadians : -maxStepRadians));
}

[[nodiscard]] float distanceBetween(const Vec2 lhs, const Vec2 rhs) {
  const float dx = lhs.x - rhs.x;
  const float dy = lhs.y - rhs.y;
  return std::sqrt(dx * dx + dy * dy);
}

[[nodiscard]] bool isSameTile(const TileCoord lhs, const TileCoord rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y;
}

[[nodiscard]] bool targetInWeaponRange(const RhinoUnitState& unit,
                                       const Vec2 targetLogical,
                                       const WeaponRuntimeParams& params) {
  return distanceBetween(unit.tilePosition, targetLogical) <=
         std::max(0.05f, params.rangeCells) + kRangeEpsilonCells;
}

[[nodiscard]] std::vector<FiringCellCandidate> firingCellCandidates(const RhinoUnitState& unit,
                                                                    const MapGrid& map,
                                                                    const Vec2 targetLogical,
                                                                    const WeaponRuntimeParams& params) {
  const float rangeCells = std::max(0.05f, params.rangeCells);
  const int minX = static_cast<int>(std::floor(targetLogical.x - rangeCells)) - 1;
  const int maxX = static_cast<int>(std::ceil(targetLogical.x + rangeCells)) + 1;
  const int minY = static_cast<int>(std::floor(targetLogical.y - rangeCells)) - 1;
  const int maxY = static_cast<int>(std::ceil(targetLogical.y + rangeCells)) + 1;

  std::vector<FiringCellCandidate> candidates;
  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      const TileCoord cell{x, y};
      if (!isSameTile(cell, unit.occupiedCell) && !map.isBuildable(cell)) {
        continue;
      }

      const Vec2 cellCenter{static_cast<float>(x), static_cast<float>(y)};
      const float targetDistance = distanceBetween(cellCenter, targetLogical);
      if (targetDistance > rangeCells + kRangeEpsilonCells) {
        continue;
      }

      candidates.push_back(FiringCellCandidate{
        cell,
        distanceBetween(unit.tilePosition, cellCenter),
        targetDistance
      });
    }
  }

  std::sort(candidates.begin(),
            candidates.end(),
            [](const FiringCellCandidate& lhs, const FiringCellCandidate& rhs) {
              if (std::fabs(lhs.moveDistance - rhs.moveDistance) > 0.001f) {
                return lhs.moveDistance < rhs.moveDistance;
              }
              return lhs.targetDistance < rhs.targetDistance;
            });
  return candidates;
}

[[nodiscard]] Vec2 directionFromAngle(const float radians) {
  return Vec2{std::cos(radians), std::sin(radians)};
}

[[nodiscard]] Vec2 isoToScreenFloat(const float tileX,
                                    const float tileY,
                                    const Vec2 origin,
                                    const float tileWidth,
                                    const float tileHeight) {
  return Vec2{
    origin.x + ((tileX - tileY) * tileWidth * 0.5f),
    origin.y + ((tileX + tileY) * tileHeight * 0.5f)
  };
}

[[nodiscard]] GLuint uploadIndexedTexture(const int width,
                                          const int height,
                                          const std::vector<std::uint8_t>& indices) {
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, indices.data());
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  return texture;
}

[[nodiscard]] GLuint uploadRgbaTexture(const int width,
                                       const int height,
                                       const std::vector<std::uint8_t>& rgba) {
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  return texture;
}

[[nodiscard]] UiTexture uploadIndexedFrame(const DecodedFrame& frame) {
  return UiTexture{
    uploadIndexedTexture(frame.width, frame.height, frame.indices),
    0,
    static_cast<int>(frame.width),
    static_cast<int>(frame.height),
    {},
    UiTextureColorMode::IndexedPalette,
    UiTexturePaletteKind::Unit,
    false
  };
}

[[nodiscard]] UiTexture uploadRgbaFrame(const DecodedFrame& frame,
                                        std::vector<std::uint8_t> rgba) {
  return UiTexture{
    uploadRgbaTexture(frame.width, frame.height, rgba),
    0,
    static_cast<int>(frame.width),
    static_cast<int>(frame.height),
    std::move(rgba),
    UiTextureColorMode::Rgba,
    UiTexturePaletteKind::None,
    false
  };
}

[[nodiscard]] std::vector<UiTexture> loadRgbaShpFrames(const std::filesystem::path& path,
                                                       const Palette& palette) {
  if (!std::filesystem::exists(path)) {
    throw std::runtime_error("Missing weapon SHP resource: " + path.string());
  }

  const auto shp = ShpTsFile::load(path);
  std::vector<UiTexture> textures;
  textures.reserve(shp.frameCount());
  for (std::size_t frameIndex = 0; frameIndex < shp.frameCount(); ++frameIndex) {
    textures.push_back(uploadRgbaFrame(shp.decodeFrame(frameIndex),
                                       shp.decodeFrameRgba(frameIndex, palette)));
  }
  return textures;
}

[[nodiscard]] UiTexture loadIndexedShpFrame(const std::filesystem::path& path,
                                            const std::size_t frameIndex = 0) {
  if (!std::filesystem::exists(path)) {
    throw std::runtime_error("Missing weapon SHP resource: " + path.string());
  }

  const auto shp = ShpTsFile::load(path);
  if (frameIndex >= shp.frameCount()) {
    throw std::runtime_error("Weapon SHP frame index out of range: " + path.string());
  }
  return uploadIndexedFrame(shp.decodeFrame(frameIndex));
}

void destroyTexture(UiTexture& texture) {
  if (texture.texture != 0) {
    glDeleteTextures(1, &texture.texture);
    texture.texture = 0;
  }
  if (texture.logicalCoordTexture != 0) {
    glDeleteTextures(1, &texture.logicalCoordTexture);
    texture.logicalCoordTexture = 0;
  }
  texture.width = 0;
  texture.height = 0;
  texture.rgba.clear();
}

void destroyTextures(std::vector<UiTexture>& textures) {
  for (auto& texture : textures) {
    destroyTexture(texture);
  }
  textures.clear();
}

void drawTexturedQuad(Renderer2D& renderer,
                      const UiTexture& texture,
                      const Vec2 center,
                      const float width,
                      const float height,
                      const float rotationRadians,
                      const float depth01,
                      const float alpha = 1.0f) {
  if (texture.texture == 0 || width <= 0.0f || height <= 0.0f) {
    return;
  }

  const float halfWidth = width * 0.5f;
  const float halfHeight = height * 0.5f;
  const float cosAngle = std::cos(rotationRadians);
  const float sinAngle = std::sin(rotationRadians);
  const auto transform = [&](const float x, const float y) {
    return Vec2{
      center.x + x * cosAngle - y * sinAngle,
      center.y + x * sinAngle + y * cosAngle
    };
  };

  const Vec2 topLeft = transform(-halfWidth, -halfHeight);
  const Vec2 topRight = transform(halfWidth, -halfHeight);
  const Vec2 bottomRight = transform(halfWidth, halfHeight);
  const Vec2 bottomLeft = transform(-halfWidth, halfHeight);
  const RenderVertex vertices[] = {
    {topLeft.x, topLeft.y, depth01, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, alpha},
    {topRight.x, topRight.y, depth01, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, alpha},
    {bottomRight.x, bottomRight.y, depth01, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, alpha},
    {topLeft.x, topLeft.y, depth01, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, alpha},
    {bottomRight.x, bottomRight.y, depth01, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, alpha},
    {bottomLeft.x, bottomLeft.y, depth01, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, alpha}
  };
  renderer.draw(GL_TRIANGLES, texture, vertices, std::size(vertices));
}

[[nodiscard]] std::uint32_t cooldownTicksForWeapon(const WeaponRuntimeParams& params) {
  const float cooldownSeconds =
    std::max(0.0f, params.rofFrames) / std::max(1.0f, params.rofFramesPerSecond);
  return static_cast<std::uint32_t>(std::round(cooldownSeconds * 1000.0f));
}

void spawnRhino120mmShot(RhinoUnitState& unit,
                         std::vector<ProjectileInstance>& projectiles,
                         std::vector<CombatEffectInstance>& effects,
                         const std::uint32_t nowTicks,
                         const WeaponRuntimeParams& params) {
  const auto direction = directionFromAngle(unit.turretHeadingRadians);
  const Vec2 lateralDirection{-direction.y, direction.x};
  const Vec2 muzzleLogical{
    unit.tilePosition.x +
      direction.x * (params.fireForwardLeptons / kLeptonsPerCell) +
      lateralDirection.x * (params.fireLateralLeptons / kLeptonsPerCell),
    unit.tilePosition.y +
      direction.y * (params.fireForwardLeptons / kLeptonsPerCell) +
      lateralDirection.y * (params.fireLateralLeptons / kLeptonsPerCell)
  };
  const Vec2 targetLogical = unit.groundFireTarget;
  const float travelDistance = std::max(0.05f, distanceBetween(muzzleLogical, targetLogical));
  const float horizontalDistanceLeptons = travelDistance * kLeptonsPerCell;
  const float arcingSpeed = std::max(1.0f, params.arcingSpeedLeptonsPerFrame);
  const float gravity = std::max(0.1f, params.gravityLeptonsPerFrameSquared);
  const float minDurationRulesFrames = std::max(1.0f, params.minDurationRulesFrames);
  const float projectileRulesFramesPerSecond = std::max(1.0f, params.projectileRulesFramesPerSecond);
  const float fireHeightLeptons = std::max(0.0f, params.fireHeightLeptons);
  const float durationRulesFrames =
    std::max(minDurationRulesFrames, horizontalDistanceLeptons / arcingSpeed);
  const float durationSeconds = durationRulesFrames / projectileRulesFramesPerSecond;
  const float initialVerticalVelocity =
    (-fireHeightLeptons + 0.5f * gravity * durationRulesFrames * durationRulesFrames) /
    durationRulesFrames;

  effects.push_back(CombatEffectInstance{
    CombatEffectKind::Muzzle,
    muzzleLogical,
    fireHeightLeptons,
    0.0f,
    0.035f,
    0.14f,
    0
  });
  projectiles.push_back(ProjectileInstance{
    muzzleLogical,
    targetLogical,
    fireHeightLeptons,
    initialVerticalVelocity,
    durationRulesFrames,
    gravity,
    projectileRulesFramesPerSecond,
    0.0f,
    durationSeconds
  });
  unit.nextGroundFireTicks = nowTicks + cooldownTicksForWeapon(params);
}

[[nodiscard]] float leptonHeightToPixels(const float heightLeptons, const float tileHeight) {
  return heightLeptons * tileHeight / kLeptonsPerCell;
}

[[nodiscard]] std::size_t effectFrameIndex(const CombatEffectInstance& effect,
                                           const std::size_t frameCount) {
  if (frameCount == 0) {
    return 0;
  }
  const auto frameIndex =
    static_cast<std::size_t>(effect.elapsedSeconds / std::max(0.001f, effect.frameDurationSeconds));
  return std::min(frameIndex, frameCount - 1);
}
}  // namespace

WeaponVisualAssets loadWeaponVisualAssets(const std::filesystem::path& effectsRoot,
                                          const Palette& animationPalette) {
  WeaponVisualAssets assets;
  assets.shell120mm = loadIndexedShpFrame(effectsRoot / "projectiles" / "cannon" / "120mm.shp");
  assets.gunfireFrames = loadRgbaShpFrames(effectsRoot / "muzzle" / "gunfire.shp", animationPalette);
  assets.explosionFrames = loadRgbaShpFrames(effectsRoot / "explosions" / "s_clsn16.shp", animationPalette);
  assets.explosionLargeFrames = loadRgbaShpFrames(effectsRoot / "explosions" / "s_clsn22.shp", animationPalette);
  return assets;
}

void destroyWeaponVisualAssets(WeaponVisualAssets& assets) {
  destroyTexture(assets.shell120mm);
  destroyTextures(assets.gunfireFrames);
  destroyTextures(assets.explosionFrames);
  destroyTextures(assets.explosionLargeFrames);
}

bool issueRhinoGroundFireCommand(RhinoUnitState& unit,
                                 const MapGrid& map,
                                 const Vec2 targetLogical,
                                 const std::uint32_t nowTicks,
                                 const WeaponRuntimeParams& params) {
  if (unit.kind != VehicleUnitKind::Rhino) {
    return false;
  }

  if (!targetInWeaponRange(unit, targetLogical, params)) {
    unit.groundFireActive = false;
    unit.hasQueuedGroundFire = true;
    unit.queuedGroundFireTarget = targetLogical;
    unit.nextGroundFireTicks = 0;

    for (const auto& candidate : firingCellCandidates(unit, map, targetLogical, params)) {
      if (issueRhinoMoveCommand(unit, map, candidate.cell, nowTicks, false)) {
        return true;
      }
    }

    unit.hasQueuedGroundFire = false;
    return false;
  }

  const bool wasActive = unit.groundFireActive;
  unit.groundFireActive = true;
  unit.groundFireTarget = targetLogical;
  unit.hasQueuedGroundFire = false;
  unit.path.clear();
  unit.hasPendingMove = false;
  unit.finishPathBeforePendingMove = false;
  unit.moveTarget = unit.occupiedCell;
  unit.steeringTarget = unit.occupiedCell;
  if (!wasActive) {
    unit.nextGroundFireTicks = nowTicks;
  }
  return true;
}

void stopRhinoGroundFire(RhinoUnitState& unit) {
  unit.groundFireActive = false;
  unit.hasQueuedGroundFire = false;
  unit.nextGroundFireTicks = 0;
}

void updateRhinoWeapon(RhinoUnitState& unit,
                       std::vector<ProjectileInstance>& projectiles,
                       std::vector<CombatEffectInstance>& effects,
                       const float deltaSeconds,
                       const std::uint32_t nowTicks,
                       const WeaponRuntimeParams& params) {
  const float safeDeltaSeconds = std::max(0.0f, deltaSeconds);
  const float turretTurnSpeed = std::max(0.1f, params.turretTurnSpeedRadiansPerSecond);
  if (!unit.groundFireActive || unit.kind != VehicleUnitKind::Rhino) {
    unit.turretHeadingRadians = rotateToward(unit.turretHeadingRadians,
                                             unit.headingRadians,
                                             turretTurnSpeed * safeDeltaSeconds);
    return;
  }

  const float dx = unit.groundFireTarget.x - unit.tilePosition.x;
  const float dy = unit.groundFireTarget.y - unit.tilePosition.y;
  const float targetDistance = std::sqrt(dx * dx + dy * dy);
  if (targetDistance <= 0.05f) {
    return;
  }
  if (targetDistance > std::max(0.05f, params.rangeCells) + kRangeEpsilonCells) {
    return;
  }

  const float targetHeading = normalizeAngle(std::atan2(dy, dx));
  unit.turretHeadingRadians = rotateToward(unit.turretHeadingRadians,
                                           targetHeading,
                                           turretTurnSpeed * safeDeltaSeconds);
  const float fireTurnTolerance = std::max(0.0f, params.fireTurnToleranceRadians);
  if (std::fabs(shortestAngleDelta(unit.turretHeadingRadians, targetHeading)) > fireTurnTolerance) {
    return;
  }

  if (nowTicks >= unit.nextGroundFireTicks) {
    spawnRhino120mmShot(unit, projectiles, effects, nowTicks, params);
  }
}

void updateWeaponProjectilesAndEffects(std::vector<ProjectileInstance>& projectiles,
                                       std::vector<CombatEffectInstance>& effects,
                                       const float deltaSeconds) {
  const float safeDeltaSeconds = std::max(0.0f, deltaSeconds);

  std::vector<ProjectileInstance> liveProjectiles;
  liveProjectiles.reserve(projectiles.size());
  for (auto projectile : projectiles) {
    projectile.elapsedSeconds += safeDeltaSeconds;
    if (projectile.elapsedSeconds >= projectile.durationSeconds) {
      const int explosionVariant =
        (static_cast<int>(std::floor(projectile.targetLogical.x * 13.0f +
                                     projectile.targetLogical.y * 17.0f)) & 1);
      effects.push_back(CombatEffectInstance{
        CombatEffectKind::Explosion,
        projectile.targetLogical,
        0.0f,
        0.0f,
        0.045f,
        0.36f,
        explosionVariant
      });
      continue;
    }
    liveProjectiles.push_back(projectile);
  }
  projectiles = std::move(liveProjectiles);

  for (auto& effect : effects) {
    effect.elapsedSeconds += safeDeltaSeconds;
  }
  effects.erase(std::remove_if(effects.begin(),
                               effects.end(),
                               [](const CombatEffectInstance& effect) {
                                 return effect.elapsedSeconds >= effect.lifetimeSeconds;
                               }),
                effects.end());
}

void drawWeaponProjectilesAndEffects(Renderer2D& renderer,
                                     const WeaponVisualAssets& assets,
                                     const std::vector<ProjectileInstance>& projectiles,
                                     const std::vector<CombatEffectInstance>& effects,
                                     const Vec2 origin,
                                     const float tileWidth,
                                     const float tileHeight) {
  for (const auto& projectile : projectiles) {
    const float elapsedRulesFrames =
      std::clamp(projectile.elapsedSeconds * std::max(1.0f, projectile.rulesFramesPerSecond),
                 0.0f,
                 std::max(0.001f, projectile.durationRulesFrames));
    const float progress =
      std::clamp(elapsedRulesFrames / std::max(0.001f, projectile.durationRulesFrames), 0.0f, 1.0f);
    const Vec2 logical{
      projectile.startLogical.x + (projectile.targetLogical.x - projectile.startLogical.x) * progress,
      projectile.startLogical.y + (projectile.targetLogical.y - projectile.startLogical.y) * progress
    };
    const float heightLeptons = std::max(
      0.0f,
      projectile.startHeightLeptons +
        projectile.initialVerticalVelocityLeptonsPerFrame * elapsedRulesFrames -
        0.5f * projectile.gravityLeptonsPerFrameSquared * elapsedRulesFrames * elapsedRulesFrames);
    Vec2 screen = isoToScreenFloat(logical.x, logical.y, origin, tileWidth, tileHeight);
    screen.y -= leptonHeightToPixels(heightLeptons, tileHeight);

    const auto startScreen =
      isoToScreenFloat(projectile.startLogical.x, projectile.startLogical.y, origin, tileWidth, tileHeight);
    const auto targetScreen =
      isoToScreenFloat(projectile.targetLogical.x, projectile.targetLogical.y, origin, tileWidth, tileHeight);
    const float screenAngle = std::atan2(targetScreen.y - startScreen.y, targetScreen.x - startScreen.x);
    const float width = std::max(6.0f, static_cast<float>(assets.shell120mm.width));
    const float height = std::max(3.0f, static_cast<float>(assets.shell120mm.height));
    const float depth = std::clamp(depthFromLogicalCenter(logical, tileWidth, tileHeight) - 0.00045f,
                                   0.001f,
                                   0.999f);
    drawTexturedQuad(renderer, assets.shell120mm, screen, width, height, screenAngle, depth);
  }

  for (const auto& effect : effects) {
    const auto& frames =
      effect.kind == CombatEffectKind::Muzzle
        ? assets.gunfireFrames
        : ((effect.variant == 1 && !assets.explosionLargeFrames.empty())
             ? assets.explosionLargeFrames
             : assets.explosionFrames);
    if (frames.empty()) {
      continue;
    }

    const UiTexture& frame = frames[effectFrameIndex(effect, frames.size())];
    Vec2 screen =
      isoToScreenFloat(effect.logicalPosition.x, effect.logicalPosition.y, origin, tileWidth, tileHeight);
    screen.y -= leptonHeightToPixels(effect.heightLeptons, tileHeight);
    const float depth =
      std::clamp(depthFromLogicalCenter(effect.logicalPosition, tileWidth, tileHeight) - 0.0003f,
                 0.001f,
                 0.999f);
    drawTexturedQuad(renderer,
                     frame,
                     screen,
                     static_cast<float>(frame.width),
                     static_cast<float>(frame.height),
                     0.0f,
                     depth);
  }
}
