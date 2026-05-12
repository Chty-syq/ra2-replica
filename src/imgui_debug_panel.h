#pragma once

#include "demo_style.h"
#include "voxel_normals.h"

#include <SDL.h>

#include <cstddef>

// 调试面板里可选的窗口分辨率。
// 这里先提供一组固定档位，避免把窗口系统相关细节散落在主循环里。
struct DisplayResolutionOption {
  const char* label = "";
  int width = 1280;
  int height = 720;
};

inline constexpr DisplayResolutionOption kDisplayResolutionOptions[] = {
  {"1024 x 576", 1024, 576},
  {"1280 x 720", 1280, 720},
  {"1366 x 768", 1366, 768},
  {"1600 x 900", 1600, 900},
  {"1920 x 1080", 1920, 1080}
};

inline constexpr std::size_t kDefaultResolutionIndex = 1;

struct DisplayDebugState {
  std::size_t resolutionIndex = kDefaultResolutionIndex;
};

struct GameplayDebugState {
  float speedMultiplier = 1.0f;
};

// 下面这些状态继续服务于 Rhino 载具实验渲染。
struct RhinoPlacementDebugState {
  float footprintK = 1.0f;
  int direction = 0;
};

struct RhinoTransformDebugState {
  float scaleFactor = 1.0f;
  float worldRotateXDeg = 0.0f;
  float worldRotateYDeg = 0.0f;
  float worldRotateZDeg = 0.0f;
};

struct RhinoLightingDebugState {
  float extraLight = 0.0f;
  float lightDirX = 0.2013022f;
  float lightDirY = -0.9101138f;
  float lightDirZ = -0.3621709f;
  VoxelNormalTableSelection normalTableSelection = VoxelNormalTableSelection::AutoFromVxl;
};

struct RhinoShadowDebugState {
  float alpha = 0.28f;
  float gray = 0.18f;
  float depthBias01 = 0.0010f;
};

struct RhinoTurretDebugState {
  float rotationDegrees = 0.0f;
  float offsetPixels = 0.0f;
};

struct ImGuiDebugPanelState {
  bool visible = false;
  DemoVisualStyle style{};
  DisplayDebugState display{};
  GameplayDebugState gameplay{};
  RhinoPlacementDebugState rhinoPlacement{};
  RhinoTransformDebugState rhinoTransform{};
  RhinoLightingDebugState rhinoLighting{};
  RhinoShadowDebugState rhinoShadow{};
  RhinoTurretDebugState rhinoTurret{};
};

void initializeImGuiDebugPanel(SDL_Window* window, SDL_GLContext glContext);
void shutdownImGuiDebugPanel();
void processImGuiDebugPanelEvent(const SDL_Event& event);
void beginImGuiDebugPanelFrame();
[[nodiscard]] bool drawImGuiDebugPanel(ImGuiDebugPanelState& state, const HouseColorSet& houseColors);
void renderImGuiDebugPanel();
[[nodiscard]] bool imguiDebugPanelWantsMouse();
[[nodiscard]] bool imguiDebugPanelWantsKeyboard();
[[nodiscard]] bool imguiDebugPanelContainsPoint(const ImGuiDebugPanelState& state, float mouseX, float mouseY);

void resetRhinoDebugState(ImGuiDebugPanelState& state);
[[nodiscard]] bool rhinoDebugStateChanged(const ImGuiDebugPanelState& lhs, const ImGuiDebugPanelState& rhs);
