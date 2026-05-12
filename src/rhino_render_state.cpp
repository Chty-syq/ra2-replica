#include "rhino_render_state.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {
struct Matrix4f {
  std::array<float, 16> values{};

  [[nodiscard]] float& at(const std::size_t row, const std::size_t column) {
    return values[row * 4 + column];
  }

  [[nodiscard]] static Matrix4f identity() {
    Matrix4f matrix{};
    matrix.at(0, 0) = 1.0f;
    matrix.at(1, 1) = 1.0f;
    matrix.at(2, 2) = 1.0f;
    matrix.at(3, 3) = 1.0f;
    return matrix;
  }
};

[[nodiscard]] Matrix4f multiply(const Matrix4f& left, const Matrix4f& right) {
  Matrix4f result{};
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t column = 0; column < 4; ++column) {
      float sum = 0.0f;
      for (std::size_t k = 0; k < 4; ++k) {
        sum += left.values[row * 4 + k] * right.values[k * 4 + column];
      }
      result.values[row * 4 + column] = sum;
    }
  }
  return result;
}

[[nodiscard]] Matrix4f makeRotationXMatrix(const float radians) {
  auto matrix = Matrix4f::identity();
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  matrix.at(1, 1) = c;
  matrix.at(1, 2) = s;
  matrix.at(2, 1) = -s;
  matrix.at(2, 2) = c;
  return matrix;
}

[[nodiscard]] Matrix4f makeRotationYMatrix(const float radians) {
  auto matrix = Matrix4f::identity();
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  matrix.at(0, 0) = c;
  matrix.at(0, 2) = -s;
  matrix.at(2, 0) = s;
  matrix.at(2, 2) = c;
  return matrix;
}

[[nodiscard]] Matrix4f makeRotationZMatrix(const float radians) {
  auto matrix = Matrix4f::identity();
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  matrix.at(0, 0) = c;
  matrix.at(0, 1) = s;
  matrix.at(1, 0) = -s;
  matrix.at(1, 1) = c;
  return matrix;
}

[[nodiscard]] float degToRad(const float degrees) {
  return degrees * 3.14159265358979323846f / 180.0f;
}

[[nodiscard]] std::array<float, 3> normalizeDirection(const std::array<float, 3>& direction) {
  const float length =
    std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
  if (length <= 1e-6f) {
    return {0.0f, 0.0f, 1.0f};
  }

  return {direction[0] / length, direction[1] / length, direction[2] / length};
}
}  // namespace

VplBoxRendererState buildRhinoTankRenderState(const HouseColorSet& houseColors,
                                              const ImGuiDebugPanelState& debugPanelState) {
  return buildRhinoTankRenderState(houseColors, debugPanelState, debugPanelState.rhinoPlacement.direction);
}

VplBoxRendererState buildRhinoTankRenderState(const HouseColorSet& houseColors,
                                              const ImGuiDebugPanelState& debugPanelState,
                                              const int directionIndex) {
  VplBoxRendererState renderState;

  const float directionStep = -2.0f * 3.14159265358979323846f / 32.0f;
  const float directionAngle = directionStep * static_cast<float>(directionIndex);
  const auto world = multiply(
    makeRotationZMatrix(directionAngle + degToRad(debugPanelState.rhinoTransform.worldRotateZDeg)),
    multiply(makeRotationYMatrix(degToRad(debugPanelState.rhinoTransform.worldRotateYDeg)),
             makeRotationXMatrix(degToRad(debugPanelState.rhinoTransform.worldRotateXDeg))));

  renderState.worldTransform = world.values;
  renderState.lightDirection = normalizeDirection({
    debugPanelState.rhinoLighting.lightDirX,
    debugPanelState.rhinoLighting.lightDirY,
    debugPanelState.rhinoLighting.lightDirZ
  });
  renderState.bodyRotationDegrees = 0.0f;
  renderState.scaleFactor = debugPanelState.rhinoTransform.scaleFactor;
  renderState.turretRotationDegrees = debugPanelState.rhinoTurret.rotationDegrees;
  renderState.turretOffsetPixels = debugPanelState.rhinoTurret.offsetPixels;
  renderState.extraLight = debugPanelState.rhinoLighting.extraLight;
  renderState.normalTableSelection = debugPanelState.rhinoLighting.normalTableSelection;
  renderState.shadowAlpha = std::clamp(debugPanelState.rhinoShadow.alpha, 0.0f, 1.0f);
  renderState.shadowGray = std::clamp(debugPanelState.rhinoShadow.gray, 0.0f, 1.0f);
  renderState.shadowDepthBias01 = std::max(0.0f, debugPanelState.rhinoShadow.depthBias01);
  renderState.remapColor = activeHouseColorValue(debugPanelState.style, houseColors);
  return renderState;
}
