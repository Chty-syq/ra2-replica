#pragma once

#include "palette.h"
#include "renderer2d.h"
#include "ui_texture.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

enum class SidebarTab : int {
  Base = 0,
  Defense = 1,
  Infantry = 2,
  Vehicles = 3
};

enum class SidebarClickAction {
  None,
  ToggleRepair,
  ToggleSell,
  SelectTab,
  ScrollDown,
  ScrollUp,
  ClickIcon
};

struct SidebarIcon {
  std::string id;
  UiTexture texture;
};

struct SidebarState {
  SidebarTab selectedTab = SidebarTab::Base;
  bool repairSelected = false;
  bool sellSelected = false;
  std::array<int, 4> scrollRows{};
  std::array<bool, 4> tabVisible{{true, false, false, false}};
  std::array<bool, 4> tabReady{};
  bool flashOn = false;
  int visibleRowsHint = 1;
};

struct SidebarClickResult {
  bool consumed = false;
  SidebarClickAction action = SidebarClickAction::None;
  SidebarTab tab = SidebarTab::Base;
  std::string iconId;
};

struct SidebarAssets {
  UiTexture credits;
  UiTexture top;
  UiTexture diplo;
  UiTexture opt;
  UiTexture radar;
  UiTexture side1;
  UiTexture side2;
  UiTexture side3;
  UiTexture addon;
  std::array<UiTexture, 5> powerLines;
  std::array<UiTexture, 2> repairFrames;
  std::array<UiTexture, 2> sellFrames;
  std::array<UiTexture, 2> scrollDownFrames;
  std::array<UiTexture, 2> scrollUpFrames;
  std::array<std::array<UiTexture, 5>, 4> tabFrames;
  std::array<std::vector<SidebarIcon>, 4> panelIcons;
};

SidebarAssets loadSidebarAssets(const std::filesystem::path& uiSpriteRoot,
                                const std::filesystem::path& cameoSpriteRoot,
                                const Palette& sidebarPalette,
                                const Palette& cameoPalette);
void destroySidebarAssets(SidebarAssets& assets);

int computeSidebarVisibleRows(const SidebarAssets& assets, int viewportHeight);
void updateSidebarState(SidebarState& state, std::uint32_t nowTicks);
SidebarClickResult handleSidebarLeftClick(const SidebarState& state,
                                         const SidebarAssets& assets,
                                         int viewportWidth,
                                         int viewportHeight,
                                         float mouseX,
                                         float mouseY);
void applySidebarClick(SidebarState& state, const SidebarAssets& assets, const SidebarClickResult& click);
void drawSidebar(Renderer2D& renderer,
                 int viewportWidth,
                 int viewportHeight,
                 const SidebarAssets& assets,
                 const SidebarState& state);
