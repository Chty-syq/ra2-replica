#include "voxel_unit.h"

#include <algorithm>

namespace {
constexpr float kVehicleFrameToCellScale = 1.0f;
// Rhino 现在和建筑一样，统一按“占地中心点 -> 单一深度”参与主场景遮挡。
// 先把体素内部 3D 深度压平，这样排序语义会和当前 2.5D 建筑世界完全一致。
constexpr float kDirectDepthScale = 0.0f;
}  // namespace

void drawDirectVoxelUnitInstance(VplBoxRenderer& renderer,
                                 const Vec2& tilePosition,
                                 const Vec2 origin,
                                 const float tileWidth,
                                 const float tileHeight,
                                 const float footprintK,
                                 const int viewportWidth,
                                 const int viewportHeight,
                                 const VplBoxRendererState& renderState) {
  const auto groundAnchor = Vec2{
    origin.x + ((tilePosition.x - tilePosition.y) * tileWidth * 0.5f),
    origin.y + ((tilePosition.x + tilePosition.y) * tileHeight * 0.5f)
  };
  const float clampedFootprintK = std::max(0.01f, footprintK);

  auto effectiveState = renderState;
  effectiveState.scaleFactor *= kVehicleFrameToCellScale * clampedFootprintK;

  const float depthBase = depthFromLogicalCenter(tilePosition, tileWidth, tileHeight);
  const float shadowDepthBase =
    std::clamp(depthBase + effectiveState.shadowDepthBias01, 0.001f, 0.999f);

  renderer.renderShadowInWorld(effectiveState,
                               viewportWidth,
                               viewportHeight,
                               groundAnchor.x,
                               groundAnchor.y,
                               shadowDepthBase,
                               kDirectDepthScale);
  renderer.renderInWorld(effectiveState,
                         viewportWidth,
                         viewportHeight,
                         groundAnchor.x,
                         groundAnchor.y,
                         depthBase,
                         kDirectDepthScale);
}
