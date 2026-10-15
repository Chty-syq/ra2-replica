#include "rhino_unit.h"

#include "pathfinding.h"
#include "shp_ts.h"
#include "voxel_unit.h"
#include "vpl_box_renderer.h"

#include <SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

namespace {
constexpr float kHealthBarWidthRatio = 0.62f;
constexpr float kHealthBarOffsetY = 30.0f;
constexpr float kMoveSpeedTilesPerSecond = 2.4f;
constexpr float kTurnSpeedRadiansPerSecond = 5.2f;
constexpr float kMoveTurnThresholdRadians = 0.22f;
constexpr float kPi = 3.14159265358979323846f;

[[nodiscard]] UiTexture uploadTexture(const int width,
                                      const int height,
                                      const std::vector<std::uint8_t>& rgba) {
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  return UiTexture{texture, 0, width, height, rgba};
}

// Takes ownership of the uploaded source, including when no crop is needed.
[[nodiscard]] UiTexture cropOpaqueBounds(UiTexture source) {
  if (source.texture == 0 || source.width <= 0 || source.height <= 0 || source.rgba.empty()) {
    return source;
  }

  int minX = source.width;
  int minY = source.height;
  int maxX = -1;
  int maxY = -1;
  for (int y = 0; y < source.height; ++y) {
    for (int x = 0; x < source.width; ++x) {
      const std::size_t pixelIndex = static_cast<std::size_t>((y * source.width + x) * 4 + 3);
      if (pixelIndex >= source.rgba.size() || source.rgba[pixelIndex] == 0) {
        continue;
      }
      minX = std::min(minX, x);
      minY = std::min(minY, y);
      maxX = std::max(maxX, x);
      maxY = std::max(maxY, y);
    }
  }

  if (maxX < minX || maxY < minY) {
    return source;
  }

  const int croppedWidth = maxX - minX + 1;
  const int croppedHeight = maxY - minY + 1;
  if (croppedWidth == source.width && croppedHeight == source.height) {
    return source;
  }

  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(croppedWidth * croppedHeight * 4), 0);
  for (int y = 0; y < croppedHeight; ++y) {
    for (int x = 0; x < croppedWidth; ++x) {
      const std::size_t srcIndex = static_cast<std::size_t>(((minY + y) * source.width + (minX + x)) * 4);
      const std::size_t dstIndex = static_cast<std::size_t>((y * croppedWidth + x) * 4);
      rgba[dstIndex + 0] = source.rgba[srcIndex + 0];
      rgba[dstIndex + 1] = source.rgba[srcIndex + 1];
      rgba[dstIndex + 2] = source.rgba[srcIndex + 2];
      rgba[dstIndex + 3] = source.rgba[srcIndex + 3];
    }
  }

  try {
    auto cropped = uploadTexture(croppedWidth, croppedHeight, rgba);
    glDeleteTextures(1, &source.texture);
    return cropped;
  } catch (...) {
    glDeleteTextures(1, &source.texture);
    throw;
  }
}

[[nodiscard]] UiTexture loadSingleShpFrame(const std::filesystem::path& path,
                                           const Palette& palette,
                                           const std::size_t frameIndex = 0) {
  const auto shp = ShpTsFile::load(path);
  const auto frame = shp.decodeFrame(frameIndex);
  return uploadTexture(frame.width, frame.height, shp.decodeFrameRgba(frameIndex, palette));
}

void destroyTexture(UiTexture& texture) {
  if (texture.texture != 0) {
    glDeleteTextures(1, &texture.texture);
    texture.texture = 0;
  }
  texture.width = 0;
  texture.height = 0;
  texture.rgba.clear();
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

[[nodiscard]] float normalizeAngle(const float radians) {
  float value = std::fmod(radians, 2.0f * kPi);
  if (value <= -kPi) {
    value += 2.0f * kPi;
  } else if (value > kPi) {
    value -= 2.0f * kPi;
  }
  return value;
}

[[nodiscard]] float rotateToward(const float current, const float target, const float maxStep) {
  const float delta = normalizeAngle(target - current);
  if (std::fabs(delta) <= maxStep) {
    return normalizeAngle(target);
  }
  return normalizeAngle(current + std::copysign(maxStep, delta));
}

[[nodiscard]] bool isSameCoord(const TileCoord lhs, const TileCoord rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y;
}

[[nodiscard]] bool planRhinoMoveFromCurrentCell(RhinoUnitState& unit,
                                                const MapGrid& map,
                                                const TileCoord targetCell) {
  auto path = findTilePath(map, unit.occupiedCell, targetCell);
  if (!path) return false;
  unit.moveTarget = path->target;
  unit.path = std::move(path->cells);
  unit.steeringTarget = unit.occupiedCell;
  unit.steeringDirectionIndex = 0;
  return true;
}

void applyPendingMoveAfterWaypoint(RhinoUnitState& unit, MapGrid& map) {
  if (!unit.hasPendingMove) {
    if (unit.path.empty()) {
      unit.finishPathBeforePendingMove = false;
      if (unit.hasQueuedGroundFire) {
        unit.groundFireActive = true;
        unit.groundFireTarget = unit.queuedGroundFireTarget;
        unit.hasQueuedGroundFire = false;
        unit.nextGroundFireTicks = 0;
      }
    }
    unit.steeringTarget = unit.occupiedCell;
    return;
  }

  if (unit.finishPathBeforePendingMove && !unit.path.empty()) {
    unit.steeringTarget = unit.occupiedCell;
    return;
  }

  const TileCoord pendingTarget = unit.pendingMoveTarget;
  unit.hasPendingMove = false;
  unit.finishPathBeforePendingMove = false;
  if (!planRhinoMoveFromCurrentCell(unit, map, pendingTarget)) {
    unit.hasQueuedGroundFire = false;
  }
}

[[nodiscard]] int quantizeDirectionIndex(const float radians) {
  const float wrapped = normalizeAngle(radians);
  const float normalized = (wrapped < 0.0f ? wrapped + 2.0f * kPi : wrapped) / (2.0f * kPi);
  // 当前朝向仍然保留 32 向量化，这样车体在转向过程中会连续经过中间朝向，
  // 视觉上是“丝滑插值”的，而不是突然跳 8 个定向。
  return static_cast<int>(std::lround(normalized * 32.0f)) % 32;
}

[[nodiscard]] int desiredDirectionIndexForMovement(const Vec2 delta) {
  const int stepX = (delta.x > 0.001f) ? 1 : ((delta.x < -0.001f) ? -1 : 0);
  const int stepY = (delta.y > 0.001f) ? 1 : ((delta.y < -0.001f) ? -1 : 0);

  // 这里只保留 8 方向目标：
  // 右 0，右下 4，下 8，左下 12，左 16，左上 20，上 24，右上 28。
  // 这样移动目标不会落到 32 向里的中间角度，斜向观感会稳定很多；
  // 但当前朝向仍然通过 32 向量化来表现连续转向过程。
  if (stepX > 0 && stepY == 0) return 0;
  if (stepX > 0 && stepY > 0) return 4;
  if (stepX == 0 && stepY > 0) return 8;
  if (stepX < 0 && stepY > 0) return 12;
  if (stepX < 0 && stepY == 0) return 16;
  if (stepX < 0 && stepY < 0) return 20;
  if (stepX == 0 && stepY < 0) return 24;
  if (stepX > 0 && stepY < 0) return 28;
  return 0;
}

[[nodiscard]] float directionIndexToHeadingRadians(const int directionIndex) {
  const int normalizedIndex = ((directionIndex % 32) + 32) % 32;
  return static_cast<float>(normalizedIndex) * 2.0f * kPi / 32.0f;
}

[[nodiscard]] Vec2 headingDirection(const float radians) {
  return Vec2{std::cos(radians), std::sin(radians)};
}

void drawTexturedQuad(Renderer2D& renderer,
                      const UiTexture& texture,
                      const Vec2 topLeft,
                      const Vec2 topRight,
                      const Vec2 bottomRight,
                      const Vec2 bottomLeft,
                      const float depth01,
                      const float alpha = 1.0f) {
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

}  // namespace

RhinoUnitUiAssets loadRhinoUnitUiAssets(const std::filesystem::path& overlayRoot,
                                        const Palette& overlayPalette) {
  RhinoUnitUiAssets assets;
  assets.healthBorder = loadSingleShpFrame(overlayRoot / "pipbrd.shp", overlayPalette);
  // 已验证：pips.shp 的 16~18 帧（1-based）对应绿 / 黄 / 红血条元素。
  assets.healthPipGreen = cropOpaqueBounds(loadSingleShpFrame(overlayRoot / "pips.shp", overlayPalette, 15));
  assets.healthPipYellow = cropOpaqueBounds(loadSingleShpFrame(overlayRoot / "pips.shp", overlayPalette, 16));
  assets.healthPipRed = cropOpaqueBounds(loadSingleShpFrame(overlayRoot / "pips.shp", overlayPalette, 17));
  return assets;
}

void destroyRhinoUnitUiAssets(RhinoUnitUiAssets& assets) {
  destroyTexture(assets.healthBorder);
  destroyTexture(assets.healthPipGreen);
  destroyTexture(assets.healthPipYellow);
  destroyTexture(assets.healthPipRed);
}

void initializeRhinoUnit(RhinoUnitState& unit, MapGrid& map, const TileCoord startCell) {
  unit.tilePosition = Vec2{static_cast<float>(startCell.x), static_cast<float>(startCell.y)};
  unit.occupiedCell = startCell;
  unit.headingRadians = 0.0f;
  unit.turretHeadingRadians = unit.headingRadians;
  unit.selected = false;
  unit.path.clear();
  unit.moveTarget = startCell;
  unit.steeringTarget = startCell;
  unit.steeringDirectionIndex = 0;
  unit.hasPendingMove = false;
  unit.finishPathBeforePendingMove = false;
  unit.pendingMoveTarget = startCell;
  unit.waypointVisibleUntilTicks = 0;
  unit.groundFireActive = false;
  unit.groundFireTarget = unit.tilePosition;
  unit.hasQueuedGroundFire = false;
  unit.queuedGroundFireTarget = unit.tilePosition;
  unit.nextGroundFireTicks = 0;
  map.setOccupied(std::vector<TileCoord>{startCell}, true);
}

bool hitTestRhinoUnit(const RhinoUnitState& unit,
                      const float mouseX,
                      const float mouseY,
                      const Vec2 origin,
                      const float tileWidth,
                      const float tileHeight) {
  const auto anchor = rhinoGroundAnchor(unit, origin, tileWidth, tileHeight);
  const float dx = (mouseX - anchor.x) / (tileWidth * 0.58f);
  const float dy = (mouseY - anchor.y) / (tileHeight * 0.92f);
  return dx * dx + dy * dy <= 1.0f;
}

void updateRhinoUnit(RhinoUnitState& unit, MapGrid& map, const float deltaSeconds) {
  if (deltaSeconds <= 0.0f || unit.path.empty()) {
    return;
  }

  const TileCoord targetCell = unit.path.front();
  const Vec2 toTarget{
    static_cast<float>(targetCell.x) - unit.tilePosition.x,
    static_cast<float>(targetCell.y) - unit.tilePosition.y
  };
  const float distance =
    std::sqrt(toTarget.x * toTarget.x + toTarget.y * toTarget.y);
  if (distance <= 0.001f) {
    map.setOccupied(std::vector<TileCoord>{unit.occupiedCell}, false);
    unit.occupiedCell = targetCell;
    map.setOccupied(std::vector<TileCoord>{unit.occupiedCell}, true);
    unit.tilePosition = Vec2{static_cast<float>(targetCell.x), static_cast<float>(targetCell.y)};
    unit.path.erase(unit.path.begin());
    applyPendingMoveAfterWaypoint(unit, map);
    return;
  }

  if (!isSameCoord(unit.steeringTarget, targetCell)) {
    unit.steeringTarget = targetCell;
    unit.steeringDirectionIndex = desiredDirectionIndexForMovement(toTarget);
  }

  const int desiredDirectionIndex = unit.steeringDirectionIndex;
  const float desiredHeading = directionIndexToHeadingRadians(desiredDirectionIndex);
  unit.headingRadians = rotateToward(unit.headingRadians,
                                     desiredHeading,
                                     kTurnSpeedRadiansPerSecond * deltaSeconds);

  if (std::fabs(normalizeAngle(desiredHeading - unit.headingRadians)) > kMoveTurnThresholdRadians) {
    return;
  }

  const Vec2 forward = headingDirection(unit.headingRadians);
  const float alongForward =
    toTarget.x * forward.x + toTarget.y * forward.y;
  if (alongForward <= 0.0f) {
    return;
  }

  const float maxStep = kMoveSpeedTilesPerSecond * deltaSeconds;
  const float step = std::min(maxStep, alongForward);
  unit.tilePosition.x += forward.x * step;
  unit.tilePosition.y += forward.y * step;

  const Vec2 remainingToTarget{
    static_cast<float>(targetCell.x) - unit.tilePosition.x,
    static_cast<float>(targetCell.y) - unit.tilePosition.y
  };
  const float remainingDistance =
    std::sqrt(remainingToTarget.x * remainingToTarget.x +
              remainingToTarget.y * remainingToTarget.y);
  if (remainingDistance <= 0.08f || step + 0.001f >= distance) {
    map.setOccupied(std::vector<TileCoord>{unit.occupiedCell}, false);
    unit.occupiedCell = targetCell;
    map.setOccupied(std::vector<TileCoord>{unit.occupiedCell}, true);
    unit.tilePosition = Vec2{static_cast<float>(targetCell.x), static_cast<float>(targetCell.y)};
    unit.path.erase(unit.path.begin());
    applyPendingMoveAfterWaypoint(unit, map);
  }
}

bool issueRhinoMoveCommand(RhinoUnitState& unit,
                           const MapGrid& map,
                           const TileCoord targetCell,
                           const std::uint32_t nowTicks,
                           const bool cancelGroundFire) {
  if (cancelGroundFire) {
    unit.groundFireActive = false;
    unit.hasQueuedGroundFire = false;
    unit.nextGroundFireTicks = 0;
  }

  if (!unit.path.empty()) {
    unit.hasPendingMove = true;
    unit.pendingMoveTarget = targetCell;
    unit.waypointVisibleUntilTicks = nowTicks;
    return true;
  }

  unit.hasPendingMove = false;
  unit.finishPathBeforePendingMove = false;
  if (!planRhinoMoveFromCurrentCell(unit, map, targetCell)) {
    return false;
  }
  unit.waypointVisibleUntilTicks = nowTicks;
  return true;
}

int rhinoDirectionIndex(const RhinoUnitState& unit) {
  return quantizeDirectionIndex(unit.headingRadians);
}

void setRhinoDirectionIndex(RhinoUnitState& unit, const int directionIndex) {
  unit.headingRadians = directionIndexToHeadingRadians(directionIndex);
  unit.turretHeadingRadians = unit.headingRadians;
}

bool rotateRhinoTowardDirectionIndex(RhinoUnitState& unit,
                                     const int directionIndex,
                                     const float deltaSeconds) {
  const float desiredHeading = directionIndexToHeadingRadians(directionIndex);
  unit.headingRadians = rotateToward(unit.headingRadians,
                                     desiredHeading,
                                     kTurnSpeedRadiansPerSecond * std::max(0.0f, deltaSeconds));
  return std::fabs(normalizeAngle(desiredHeading - unit.headingRadians)) <= 0.01f;
}

void drawRhinoHealthBar(Renderer2D& renderer,
                        const RhinoUnitUiAssets& assets,
                        const RhinoUnitState& unit,
                        const Vec2 origin,
                        const float tileWidth,
                        const float tileHeight) {
  if (!unit.selected || unit.maxHp <= 0 || assets.healthBorder.texture == 0) {
    return;
  }

  // 这里采用“pipbrd 边框 + pips 16/17/18 帧拼接血格”的方式。
  // 使用资源本身的绿 / 黄 / 红血格，不再靠 tint 伪造颜色。
  const auto anchor = rhinoGroundAnchor(unit, origin, tileWidth, tileHeight);
  const float visualScale = std::max(1.0f, (tileWidth * kHealthBarWidthRatio) / assets.healthBorder.width);
  const float outerWidth = assets.healthBorder.width * visualScale;
  const float outerHeight = assets.healthBorder.height * visualScale;
  const Vec2 barTopLeft{
    anchor.x - outerWidth * 0.5f,
    anchor.y - std::max(kHealthBarOffsetY, tileHeight * 1.05f)
  };
  const Vec2 barTopRight{barTopLeft.x + outerWidth, barTopLeft.y};
  const Vec2 barBottomRight{barTopLeft.x + outerWidth, barTopLeft.y + outerHeight};
  const Vec2 barBottomLeft{barTopLeft.x, barTopLeft.y + outerHeight};
  drawTexturedQuad(renderer,
                   assets.healthBorder,
                   barTopLeft,
                   barTopRight,
                   barBottomRight,
                   barBottomLeft,
                   0.001f);

  const float innerPaddingX = std::max(1.0f, std::round(visualScale));
  const float innerPaddingY = std::max(1.0f, std::round(visualScale));
  const float innerX = barTopLeft.x + innerPaddingX;
  const float innerY = barTopLeft.y + innerPaddingY;
  const float innerWidth = std::max(1.0f, outerWidth - innerPaddingX * 2.0f);
  const float innerHeight = std::max(1.0f, outerHeight - innerPaddingY * 2.0f);
  const float ratio = std::clamp(static_cast<float>(unit.hp) / static_cast<float>(unit.maxHp), 0.0f, 1.0f);
  const UiTexture* pipTexture = &assets.healthPipGreen;
  if (ratio <= 0.25f && assets.healthPipRed.texture != 0) {
    pipTexture = &assets.healthPipRed;
  } else if (ratio <= 0.5f && assets.healthPipYellow.texture != 0) {
    pipTexture = &assets.healthPipYellow;
  }
  if (pipTexture->texture == 0) {
    return;
  }

  const float pipWidth = std::max(1.0f, pipTexture->width * visualScale);
  const float pipHeight = std::max(1.0f, pipTexture->height * visualScale);
  const std::size_t pipCount =
    pipTexture->width > 0 ? static_cast<std::size_t>(std::max(1, static_cast<int>((innerWidth + 0.5f) / pipWidth))) : 1;
  std::size_t litPips = static_cast<std::size_t>(std::ceil(ratio * static_cast<float>(pipCount)));
  if (unit.hp > 0) {
    litPips = std::max<std::size_t>(1, litPips);
  }
  litPips = std::min(litPips, pipCount);

  for (std::size_t pipIndex = 0; pipIndex < litPips; ++pipIndex) {
    const float x = innerX + static_cast<float>(pipIndex) * pipWidth;
    const float y = innerY + std::max(0.0f, (innerHeight - pipHeight) * 0.5f);
    drawTexturedQuad(renderer,
                     *pipTexture,
                     Vec2{x, y},
                     Vec2{x + pipWidth, y},
                     Vec2{x + pipWidth, y + pipHeight},
                     Vec2{x, y + pipHeight},
                     0.001f);
  }
}

Vec2 rhinoGroundAnchor(const RhinoUnitState& unit,
                       const Vec2 origin,
                       const float tileWidth,
                       const float tileHeight) {
  return isoToScreenFloat(unit.tilePosition.x, unit.tilePosition.y, origin, tileWidth, tileHeight);
}

void drawRhinoUnit(Renderer2D& renderer,
                   VplBoxRenderer& rhinoRenderer,
                   const RhinoUnitUiAssets& uiAssets,
                   const RhinoUnitState& unit,
                   const Vec2 origin,
                   const float tileWidth,
                   const float tileHeight,
                   const float footprintK,
                   const int viewportWidth,
                   const int viewportHeight,
                   const std::uint32_t nowTicks,
                   const VplBoxRendererState& renderState) {
  (void)nowTicks;
  const Vec2 elevatedOrigin{origin.x,origin.y-unit.heightLeptons*tileHeight/256.0f};
  drawDirectVoxelUnitInstance(rhinoRenderer,
                              unit.tilePosition,
                              elevatedOrigin,
                              tileWidth,
                              tileHeight,
                              footprintK,
                              viewportWidth,
                              viewportHeight,
                              renderState);
  renderer.beginUiPass();
  drawRhinoHealthBar(renderer, uiAssets, unit, elevatedOrigin, tileWidth, tileHeight);
}
