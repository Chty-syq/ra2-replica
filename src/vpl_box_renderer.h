#pragma once

#include "palette.h"
#include "ui_texture.h"
#include "voxel_normals.h"
#include "vpl_file.h"

#include <SDL.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

// 这组状态刻意贴近参考项目 vpl_renderer 的运行时控制项。
// 参考资料：
// - https://modenc.renegadeprojects.com/Voxel
// - https://modenc.renegadeprojects.com/HVA
// - https://modenc.renegadeprojects.com/VPL
// - https://modenc.renegadeprojects.com/Normals
struct VplBoxRendererState {
  std::array<float, 16> worldTransform{
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
  };
  std::array<float, 3> lightDirection{0.2013022f, -0.9101138f, -0.3621709f};
  float bodyRotationDegrees = 0.0f;
  float scaleFactor = 1.0f;
  float turretRotationDegrees = 0.0f;
  float turretOffsetPixels = 0.0f;
  float extraLight = 0.2f;
  float shadowAlpha = 0.28f;
  float shadowGray = 0.18f;
  float shadowDepthBias01 = 0.0010f;
  VoxelNormalTableSelection normalTableSelection = VoxelNormalTableSelection::AutoFromVxl;
  Rgba remapColor{252, 0, 0, 255};
  Rgba backgroundColor{0, 0, 255, 255};
};

// 共享的 VXL 体素立方体渲染器。
// 主项目和 experiment 共用它，避免出现“两边 Rhino 看起来不一样”的情况。
class VplBoxRenderer {
public:
  VplBoxRenderer() = default;
  ~VplBoxRenderer();

  VplBoxRenderer(const VplBoxRenderer&) = delete;
  VplBoxRenderer& operator=(const VplBoxRenderer&) = delete;

  void initialize(SDL_Window* window);
  void destroy();
  void loadRhinoAssets(const std::filesystem::path& voxelRoot, const VplFile& vpl);
  void loadVehicleAssets(const std::filesystem::path& voxelRoot,
                         const VplFile& vpl,
                         const std::string& bodyStem,
                         const std::string& turretStem = {},
                         const std::string& barrelStem = {});
  void setPalette(const Palette& palette);

  void renderToScreen(const VplBoxRendererState& state, int drawableWidth, int drawableHeight);
  [[nodiscard]] UiTexture renderToTexture(const VplBoxRendererState& state, int width, int height);

  // 体素阴影单独作为一个 pass：
  // - 只参与深度测试
  // - 不写深度
  // - 当前固定为垂直向下投影
  void renderShadowInWorld(const VplBoxRendererState& state,
                           int viewportWidth,
                           int viewportHeight,
                           float screenCenterX,
                           float screenCenterY,
                           float depthBase01,
                           float depthScale01);

  // 直接把 Rhino 画进主场景，而不是先烘成一张 2D 贴图再回贴。
  void renderInWorld(const VplBoxRendererState& state,
                     int viewportWidth,
                     int viewportHeight,
                     float screenCenterX,
                     float screenCenterY,
                     float depthBase01,
                     float depthScale01);

  [[nodiscard]] bool isInitialized() const noexcept { return initialized_; }
  [[nodiscard]] std::string loadedAssetSummary() const;

private:
  struct Impl;
  Impl* impl_ = nullptr;
  bool initialized_ = false;
};
