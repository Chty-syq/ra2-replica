#include "imgui_debug_panel.h"

#include "gl_loader.h"

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iterator>
#include <stdexcept>

namespace {
constexpr float kPanelX = 16.0f;
constexpr float kPanelY = 16.0f;
constexpr float kPanelWidth = 392.0f;
constexpr float kPanelHeight = 640.0f;
constexpr float kChineseFontSize = 18.0f;
constexpr float kColorSwatchSize = 24.0f;
constexpr float kColorGridPaddingY = 8.0f;

bool gDebugPanelInitialized = false;

[[nodiscard]] const char* theaterLabel(const TheaterStyle theater) {
  switch (theater) {
    case TheaterStyle::Temperate: return u8"温带";
    case TheaterStyle::Snow: return u8"雪地";
    case TheaterStyle::Urban: return u8"城市";
  }
  return u8"温带";
}

[[nodiscard]] const char* factionLabel(const BuildFaction faction) {
  switch (faction) {
    case BuildFaction::Allied: return u8"盟军";
    case BuildFaction::Soviet: return u8"苏军";
  }
  return u8"盟军";
}

[[nodiscard]] ImVec4 toImGuiColor(const Rgba color) {
  return ImVec4(color.r / 255.0f,
                color.g / 255.0f,
                color.b / 255.0f,
                color.a / 255.0f);
}

[[nodiscard]] bool pointInPanelBounds(const float mouseX, const float mouseY) {
  return mouseX >= kPanelX &&
         mouseX < kPanelX + kPanelWidth &&
         mouseY >= kPanelY &&
         mouseY < kPanelY + kPanelHeight;
}

void applyDebugPanelStyle() {
  ImGuiStyle& style = ImGui::GetStyle();
  ImGui::StyleColorsDark();

  style.WindowRounding = 6.0f;
  style.FrameRounding = 4.0f;
  style.GrabRounding = 4.0f;
  style.FrameBorderSize = 1.0f;
  style.WindowBorderSize = 1.0f;

  style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.11f, 0.15f, 0.95f);
  style.Colors[ImGuiCol_TitleBg] = ImVec4(0.14f, 0.20f, 0.28f, 1.00f);
  style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.28f, 0.38f, 1.00f);
  style.Colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.18f, 0.23f, 1.00f);
  style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.24f, 0.30f, 1.00f);
  style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.30f, 0.38f, 1.00f);
  style.Colors[ImGuiCol_Button] = ImVec4(0.18f, 0.24f, 0.30f, 1.00f);
  style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.30f, 0.38f, 1.00f);
  style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.26f, 0.36f, 0.46f, 1.00f);
  style.Colors[ImGuiCol_Header] = ImVec4(0.18f, 0.24f, 0.30f, 1.00f);
  style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.30f, 0.38f, 1.00f);
  style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.36f, 0.46f, 1.00f);
  style.Colors[ImGuiCol_Separator] = ImVec4(0.27f, 0.35f, 0.44f, 0.90f);
  style.Colors[ImGuiCol_CheckMark] = ImVec4(0.80f, 0.89f, 1.00f, 1.00f);
}

void installChineseFont(ImGuiIO& io) {
  const ImWchar* glyphRanges = io.Fonts->GetGlyphRangesChineseFull();
  const std::filesystem::path candidates[] = {
    "C:\\Windows\\Fonts\\msyh.ttc",
    "C:\\Windows\\Fonts\\msyhl.ttc",
    "C:\\Windows\\Fonts\\simhei.ttf",
    "C:\\Windows\\Fonts\\simsun.ttc"
  };

  for (const auto& path : candidates) {
    if (!std::filesystem::exists(path)) {
      continue;
    }

    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;
    config.PixelSnapH = false;
    config.RasterizerMultiply = 1.0f;
    if (io.Fonts->AddFontFromFileTTF(path.string().c_str(), kChineseFontSize, &config, glyphRanges) != nullptr) {
      return;
    }
  }

  io.Fonts->AddFontDefault();
}

[[nodiscard]] bool beginSection(const char* title) {
  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.16f, 0.22f, 0.30f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.20f, 0.28f, 0.38f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.24f, 0.34f, 0.46f, 1.00f));
  const bool open = ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen);
  ImGui::PopStyleColor(3);
  return open;
}

struct HouseColorGridMetrics {
  int columns = 1;
  int rows = 0;
  float childHeight = 0.0f;
};

[[nodiscard]] HouseColorGridMetrics computeHouseColorGridMetrics(const float availableWidth,
                                                                 const std::size_t colorCount) {
  const ImGuiStyle& style = ImGui::GetStyle();
  const float slotWidth = kColorSwatchSize + style.ItemSpacing.x;
  const int columns = std::max(1, static_cast<int>((availableWidth + style.ItemSpacing.x) / slotWidth));
  const int rows = colorCount == 0
                     ? 0
                     : static_cast<int>((colorCount + static_cast<std::size_t>(columns) - 1) /
                                        static_cast<std::size_t>(columns));
  const float contentHeight =
    rows > 0 ? rows * kColorSwatchSize + (rows - 1) * style.ItemSpacing.y : kColorSwatchSize;
  return HouseColorGridMetrics{columns, rows, contentHeight + kColorGridPaddingY * 2.0f};
}

[[nodiscard]] bool drawHouseColorSwatches(std::size_t& selectedIndex, const HouseColorSet& houseColors) {
  if (houseColors.colors.empty()) {
    ImGui::TextDisabled("%s", u8"无可用国家色");
    return false;
  }

  const auto metrics =
    computeHouseColorGridMetrics(ImGui::GetContentRegionAvail().x, houseColors.colors.size());

  bool changed = false;
  for (std::size_t index = 0; index < houseColors.colors.size(); ++index) {
    const auto& colorEntry = houseColors.colors[index];
    const bool selected = selectedIndex == index;

    ImGui::PushID(static_cast<int>(index));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.24f, 0.30f, 0.38f, 1.00f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

    const bool clicked = ImGui::ColorButton("##HouseColor",
                                            toImGuiColor(colorEntry.color),
                                            ImGuiColorEditFlags_NoTooltip,
                                            ImVec2(kColorSwatchSize, kColorSwatchSize));
    if (clicked) {
      selectedIndex = index;
      changed = true;
    }

    if (selected) {
      ImDrawList* drawList = ImGui::GetWindowDrawList();
      const ImVec2 min = ImGui::GetItemRectMin();
      const ImVec2 max = ImGui::GetItemRectMax();
      drawList->AddRect(ImVec2(min.x - 2.0f, min.y - 2.0f),
                        ImVec2(max.x + 2.0f, max.y + 2.0f),
                        IM_COL32(240, 250, 255, 255),
                        4.0f,
                        0,
                        2.0f);
    }

    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextUnformatted(colorEntry.name.c_str());
      ImGui::EndTooltip();
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PopID();

    if ((index + 1) % metrics.columns != 0 && index + 1 < houseColors.colors.size()) {
      ImGui::SameLine();
    }
  }

  return changed;
}

[[nodiscard]] bool resolutionChanged(const ImGuiDebugPanelState& lhs,
                                     const ImGuiDebugPanelState& rhs) {
  return lhs.display.resolutionIndex != rhs.display.resolutionIndex;
}

[[nodiscard]] bool gameplayStateChanged(const ImGuiDebugPanelState& lhs,
                                        const ImGuiDebugPanelState& rhs) {
  return lhs.gameplay.speedMultiplier != rhs.gameplay.speedMultiplier;
}

[[nodiscard]] bool weaponStateChanged(const ImGuiDebugPanelState& lhs,
                                      const ImGuiDebugPanelState& rhs) {
  return lhs.weapon.rangeCells != rhs.weapon.rangeCells ||
         lhs.weapon.rofFrames != rhs.weapon.rofFrames ||
         lhs.weapon.rofFramesPerSecond != rhs.weapon.rofFramesPerSecond ||
         lhs.weapon.turretTurnSpeedRadiansPerSecond != rhs.weapon.turretTurnSpeedRadiansPerSecond ||
         lhs.weapon.fireTurnToleranceRadians != rhs.weapon.fireTurnToleranceRadians ||
         lhs.weapon.fireForwardLeptons != rhs.weapon.fireForwardLeptons ||
         lhs.weapon.fireLateralLeptons != rhs.weapon.fireLateralLeptons ||
         lhs.weapon.fireHeightLeptons != rhs.weapon.fireHeightLeptons ||
         lhs.weapon.arcingSpeedLeptonsPerFrame != rhs.weapon.arcingSpeedLeptonsPerFrame ||
         lhs.weapon.gravityLeptonsPerFrameSquared != rhs.weapon.gravityLeptonsPerFrameSquared ||
         lhs.weapon.projectileRulesFramesPerSecond != rhs.weapon.projectileRulesFramesPerSecond ||
         lhs.weapon.minDurationRulesFrames != rhs.weapon.minDurationRulesFrames;
}
}  // namespace

void initializeImGuiDebugPanel(SDL_Window* window, SDL_GLContext glContext) {
  if (gDebugPanelInitialized) {
    return;
  }

  ensureOpenGlLoaded();

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = nullptr;

  installChineseFont(io);
  applyDebugPanelStyle();

  if (!ImGui_ImplSDL2_InitForOpenGL(window, glContext)) {
    throw std::runtime_error("ImGui SDL2 backend initialization failed");
  }
  if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    throw std::runtime_error("ImGui OpenGL3 backend initialization failed");
  }

  gDebugPanelInitialized = true;
}

void shutdownImGuiDebugPanel() {
  if (!gDebugPanelInitialized) {
    return;
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  gDebugPanelInitialized = false;
}

void processImGuiDebugPanelEvent(const SDL_Event& event) {
  if (!gDebugPanelInitialized) {
    return;
  }

  ImGui_ImplSDL2_ProcessEvent(&event);
}

void beginImGuiDebugPanelFrame() {
  if (!gDebugPanelInitialized) {
    return;
  }

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();
}

void resetRhinoDebugState(ImGuiDebugPanelState& state) {
  state.rhinoPlacement = RhinoPlacementDebugState{};
  state.rhinoTransform = RhinoTransformDebugState{};
  state.rhinoLighting = RhinoLightingDebugState{};
  state.rhinoShadow = RhinoShadowDebugState{};
  state.rhinoTurret = RhinoTurretDebugState{};
}

bool rhinoDebugStateChanged(const ImGuiDebugPanelState& lhs, const ImGuiDebugPanelState& rhs) {
  return lhs.rhinoPlacement.footprintK != rhs.rhinoPlacement.footprintK ||
         lhs.rhinoPlacement.direction != rhs.rhinoPlacement.direction ||
         lhs.rhinoTransform.scaleFactor != rhs.rhinoTransform.scaleFactor ||
         lhs.rhinoTransform.worldRotateXDeg != rhs.rhinoTransform.worldRotateXDeg ||
         lhs.rhinoTransform.worldRotateYDeg != rhs.rhinoTransform.worldRotateYDeg ||
         lhs.rhinoTransform.worldRotateZDeg != rhs.rhinoTransform.worldRotateZDeg ||
         lhs.rhinoLighting.extraLight != rhs.rhinoLighting.extraLight ||
         lhs.rhinoLighting.lightDirX != rhs.rhinoLighting.lightDirX ||
         lhs.rhinoLighting.lightDirY != rhs.rhinoLighting.lightDirY ||
         lhs.rhinoLighting.lightDirZ != rhs.rhinoLighting.lightDirZ ||
         lhs.rhinoLighting.normalTableSelection != rhs.rhinoLighting.normalTableSelection ||
         lhs.rhinoShadow.alpha != rhs.rhinoShadow.alpha ||
         lhs.rhinoShadow.gray != rhs.rhinoShadow.gray ||
         lhs.rhinoShadow.depthBias01 != rhs.rhinoShadow.depthBias01 ||
         lhs.rhinoTurret.rotationDegrees != rhs.rhinoTurret.rotationDegrees ||
         lhs.rhinoTurret.offsetPixels != rhs.rhinoTurret.offsetPixels;
}

bool drawImGuiDebugPanel(ImGuiDebugPanelState& state, const HouseColorSet& houseColors) {
  if (!gDebugPanelInitialized || !state.visible) {
    return false;
  }

  const ImGuiDebugPanelState previousState = state;

  ImGui::SetNextWindowPos(ImVec2(kPanelX, kPanelY), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(kPanelWidth, kPanelHeight), ImGuiCond_Always);
  constexpr ImGuiWindowFlags kWindowFlags =
    ImGuiWindowFlags_NoCollapse |
    ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoMove;

  if (ImGui::Begin(u8"调试面板", nullptr, kWindowFlags)) {
    ImGui::BeginChild("##PanelScroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    if (beginSection(u8"场景设置")) {
      ImGui::TextUnformatted(u8"国家色");
      const auto metrics =
        computeHouseColorGridMetrics(ImGui::GetContentRegionAvail().x, houseColors.colors.size());
      ImGui::BeginChild("##HouseColors",
                        ImVec2(0.0f, metrics.childHeight),
                        true,
                        ImGuiWindowFlags_NoScrollbar);
      drawHouseColorSwatches(state.style.houseColorIndex, houseColors);
      ImGui::EndChild();

      ImGui::Spacing();
      ImGui::TextUnformatted(u8"阵营");
      for (std::size_t index = 0; index < std::size(kAllBuildFactions); ++index) {
        const BuildFaction faction = kAllBuildFactions[index];
        if (index > 0) {
          ImGui::SameLine();
        }
        if (ImGui::RadioButton(factionLabel(faction), state.style.faction == faction)) {
          state.style.faction = faction;
        }
      }

      ImGui::Spacing();
      ImGui::TextUnformatted(u8"剧场风格");
      for (std::size_t index = 0; index < std::size(kAllTheaterStyles); ++index) {
        const TheaterStyle theater = kAllTheaterStyles[index];
        if (index > 0) {
          ImGui::SameLine();
        }
        if (ImGui::RadioButton(theaterLabel(theater), state.style.theater == theater)) {
          state.style.theater = theater;
        }
      }
    }

    int resolutionIndex = static_cast<int>(state.display.resolutionIndex);
    const auto safeResolutionIndex =
      std::clamp(resolutionIndex, 0, static_cast<int>(std::size(kDisplayResolutionOptions) - 1));
    if (ImGui::BeginCombo(u8"分辨率", kDisplayResolutionOptions[safeResolutionIndex].label)) {
      for (int index = 0; index < static_cast<int>(std::size(kDisplayResolutionOptions)); ++index) {
        const bool selected = safeResolutionIndex == index;
        if (ImGui::Selectable(kDisplayResolutionOptions[index].label, selected)) {
          resolutionIndex = index;
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
    state.display.resolutionIndex = static_cast<std::size_t>(std::clamp(resolutionIndex, 0, static_cast<int>(std::size(kDisplayResolutionOptions) - 1)));
    ImGui::SliderFloat(u8"游戏速度", &state.gameplay.speedMultiplier, 0.25f, 3.0f, "%.2fx");

    const float fps = ImGui::GetIO().Framerate;
    const float frameMs = 1000.0f / std::max(fps, 0.001f);
    const ImVec4 fpsColor = fps >= 55.0f
                                ? ImVec4(0.48f, 0.92f, 0.62f, 1.0f)
                                : (fps >= 30.0f
                                ? ImVec4(0.95f, 0.78f, 0.35f, 1.0f)
                                : ImVec4(0.95f, 0.36f, 0.32f, 1.0f));
    ImGui::TextColored(fpsColor, u8"当前帧率: %.1f FPS / %.2f ms", fps, frameMs);
    ImGui::Spacing();

    if (beginSection(u8"武器弹道")) {
      ImGui::TextDisabled("%s", u8"普通 120mm / Cannon / Arcing");
      ImGui::SliderFloat(u8"射程(cells)", &state.weapon.rangeCells, 0.25f, 12.0f, "%.2f");
      ImGui::SliderFloat(u8"ROF(帧)", &state.weapon.rofFrames, 1.0f, 180.0f, "%.0f");
      ImGui::SliderFloat(u8"ROF帧率", &state.weapon.rofFramesPerSecond, 1.0f, 60.0f, "%.1f fps");
      const float cooldownSeconds =
        state.weapon.rofFrames / std::max(1.0f, state.weapon.rofFramesPerSecond);
      ImGui::TextDisabled(u8"换算 CD: %.2fs", cooldownSeconds);

      ImGui::Separator();
      ImGui::TextUnformatted(u8"炮口 FLH");
      float fireFlh[3] = {
        state.weapon.fireForwardLeptons,
        state.weapon.fireLateralLeptons,
        state.weapon.fireHeightLeptons
      };
      if (ImGui::InputFloat3(u8"Forward / Lateral / Height", fireFlh, "%.1f")) {
        state.weapon.fireForwardLeptons = fireFlh[0];
        state.weapon.fireLateralLeptons = fireFlh[1];
        state.weapon.fireHeightLeptons = std::max(0.0f, fireFlh[2]);
      }

      ImGui::Separator();
      ImGui::TextUnformatted(u8"炮塔");
      ImGui::SliderFloat(u8"炮塔转速", &state.weapon.turretTurnSpeedRadiansPerSecond, 0.1f, 16.0f, "%.2f rad/s");
      ImGui::SliderAngle(u8"开火容差", &state.weapon.fireTurnToleranceRadians, 0.0f, 15.0f, "%.1f deg");

      ImGui::Separator();
      ImGui::TextUnformatted(u8"Arcing 物理");
      ImGui::SliderFloat(u8"弹体速度", &state.weapon.arcingSpeedLeptonsPerFrame, 1.0f, 120.0f, "%.1f leptons/frame");
      ImGui::SliderFloat(u8"重力", &state.weapon.gravityLeptonsPerFrameSquared, 0.1f, 24.0f, "%.2f");
      ImGui::SliderFloat(u8"弹道帧率", &state.weapon.projectileRulesFramesPerSecond, 1.0f, 60.0f, "%.1f fps");
      ImGui::SliderFloat(u8"最短飞行帧", &state.weapon.minDurationRulesFrames, 1.0f, 20.0f, "%.1f");
      ImGui::TextDisabled(u8"原版参考: Range=5.75, ROF=65, ArcingSpeed=50, Gravity=6");
    }

    if (beginSection(u8"渲染参数")) {
      ImGui::TextUnformatted(u8"法线表");
      int normalTableSelection = static_cast<int>(state.rhinoLighting.normalTableSelection);
      if (ImGui::RadioButton("Auto", normalTableSelection == 0)) {
        normalTableSelection = 0;
      }
      ImGui::SameLine();
      if (ImGui::RadioButton("TS", normalTableSelection == 1)) {
        normalTableSelection = 1;
      }
      ImGui::SameLine();
      if (ImGui::RadioButton("RA2", normalTableSelection == 2)) {
        normalTableSelection = 2;
      }
      state.rhinoLighting.normalTableSelection =
        static_cast<VoxelNormalTableSelection>(normalTableSelection);

      ImGui::TextUnformatted(u8"光照参数");
      ImGui::SliderFloat(u8"环境光", &state.rhinoLighting.extraLight, -0.25f, 1.0f, "%.3f");

      float lightDirection[3] = {
        state.rhinoLighting.lightDirX,
        state.rhinoLighting.lightDirY,
        state.rhinoLighting.lightDirZ
      };
      if (ImGui::InputFloat3(u8"光照方向", lightDirection, "%.4f")) {
        state.rhinoLighting.lightDirX = lightDirection[0];
        state.rhinoLighting.lightDirY = lightDirection[1];
        state.rhinoLighting.lightDirZ = lightDirection[2];
      }

      ImGui::TextUnformatted(u8"阴影参数");
      ImGui::SliderFloat(u8"透明度", &state.rhinoShadow.alpha, 0.0f, 1.0f, "%.3f");
      ImGui::SliderFloat(u8"灰度", &state.rhinoShadow.gray, 0.0f, 1.0f, "%.3f");
      ImGui::SliderFloat(u8"深度偏移", &state.rhinoShadow.depthBias01, 0.0f, 0.02f, "%.4f");
    }

    ImGui::EndChild();
  }
  ImGui::End();

  return !sameVisualStyle(previousState.style, state.style) ||
         resolutionChanged(previousState, state) ||
         gameplayStateChanged(previousState, state) ||
         weaponStateChanged(previousState, state) ||
         rhinoDebugStateChanged(previousState, state);
}

void renderImGuiDebugPanel() {
  if (!gDebugPanelInitialized) {
    return;
  }

  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool imguiDebugPanelWantsMouse() {
  return gDebugPanelInitialized && ImGui::GetIO().WantCaptureMouse;
}

bool imguiDebugPanelWantsKeyboard() {
  return gDebugPanelInitialized && ImGui::GetIO().WantCaptureKeyboard;
}

bool imguiDebugPanelContainsPoint(const ImGuiDebugPanelState& state,
                                  const float mouseX,
                                  const float mouseY) {
  return state.visible && pointInPanelBounds(mouseX, mouseY);
}
