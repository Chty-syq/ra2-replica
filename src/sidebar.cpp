#include "sidebar.h"

#include "shp_ts.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
constexpr float kSidebarWidth = 168.0f;
constexpr float kPowerPanelWidth = 17.0f;
constexpr float kRowHeight = 50.0f;
constexpr float kIconWidth = 60.0f;
constexpr float kIconHeight = 48.0f;

using FileIndex = std::unordered_map<std::string, std::filesystem::path>;

struct Rect {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

struct SidebarLayout {
  float left = 0.0f;
  float creditsY = 0.0f;
  float topY = 0.0f;
  float radarY = 0.0f;
  float side1Y = 0.0f;
  float panelY = 0.0f;
  float bottomY = 0.0f;
  int visibleRows = 1;
};

UiTexture uploadTexture(const int width, const int height, const std::vector<std::uint8_t>& rgba) {
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

[[nodiscard]] std::string toLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

[[nodiscard]] const FileIndex& fileIndexForRoot(const std::filesystem::path& root) {
  static std::unordered_map<std::string, FileIndex> cache;

  const auto key = root.lexically_normal().string();
  if (const auto it = cache.find(key); it != cache.end()) {
    return it->second;
  }

  FileIndex index;
  if (std::filesystem::exists(root)) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      index.emplace(toLowerAscii(entry.path().filename().string()), entry.path());
    }
  }

  return cache.emplace(key, std::move(index)).first->second;
}

[[nodiscard]] std::optional<std::filesystem::path> findIndexedFile(const std::filesystem::path& root,
                                                                   const std::string& fileName) {
  const auto& index = fileIndexForRoot(root);
  if (const auto it = index.find(toLowerAscii(fileName)); it != index.end()) {
    return it->second;
  }
  return std::nullopt;
}

UiTexture makeBlankTexture(const int width, const int height) {
  return uploadTexture(width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width * height * 4), 0));
}

void applyReferenceCameoCorners(std::vector<std::uint8_t>& rgba, const int width, const int height) {
  if (width < 2 || height < 2) {
    return;
  }

  const auto setBlack = [&](const int x, const int y) {
    const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4;
    rgba[index + 0] = 0;
    rgba[index + 1] = 0;
    rgba[index + 2] = 0;
    rgba[index + 3] = 255;
  };

  const std::array<std::pair<int, int>, 12> pixels{{
    {0, 0}, {1, 0}, {0, 1},
    {width - 1, 0}, {width - 2, 0}, {width - 1, 1},
    {0, height - 1}, {0, height - 2}, {1, height - 1},
    {width - 1, height - 1}, {width - 1, height - 2}, {width - 2, height - 1}
  }};

  for (const auto& pixel : pixels) {
    setBlack(pixel.first, pixel.second);
  }
}

UiTexture loadShpFrameTexture(const std::filesystem::path& path,
                              const Palette& palette,
                              const std::size_t frameIndex = 0,
                              const bool cameoCorners = false) {
  const auto shp = ShpTsFile::load(path);
  const auto frame = shp.decodeFrame(frameIndex);
  auto rgba = shp.decodeFrameRgba(frameIndex, palette);
  if (cameoCorners) {
    applyReferenceCameoCorners(rgba, frame.width, frame.height);
  }
  return uploadTexture(frame.width, frame.height, rgba);
}

SidebarLayout makeLayout(const float sidebarLeft, const int viewportHeight, const SidebarAssets& assets) {
  SidebarLayout layout{};
  layout.left = sidebarLeft;
  layout.creditsY = 0.0f;
  layout.topY = layout.creditsY + static_cast<float>(assets.credits.height);
  layout.radarY = layout.topY + static_cast<float>(assets.top.height);
  layout.side1Y = layout.radarY + static_cast<float>(assets.radar.height);
  layout.panelY = layout.side1Y + static_cast<float>(assets.side1.height);

  const int reservedHeight = assets.credits.height +
                             assets.top.height +
                             assets.radar.height +
                             assets.side1.height +
                             assets.side3.height +
                             assets.addon.height;
  layout.visibleRows = std::max(1, (viewportHeight - reservedHeight) / static_cast<int>(kRowHeight) + 1);
  layout.bottomY = layout.panelY + static_cast<float>(layout.visibleRows) * kRowHeight;
  return layout;
}

std::size_t tabIndex(const SidebarTab tab) {
  return static_cast<std::size_t>(tab);
}

Rect localToSidebar(const SidebarLayout& layout, const float sidebarTop, const Rect local) {
  return Rect{layout.left + local.x, sidebarTop + local.y, local.w, local.h};
}

Rect repairRect(const SidebarLayout& layout, const SidebarAssets& assets) {
  return localToSidebar(layout,
                        layout.side1Y,
                        Rect{assets.repairFrames[0].width <= 52 ? 32.0f : 20.0f,
                             8.0f,
                             static_cast<float>(assets.repairFrames[0].width),
                             static_cast<float>(assets.repairFrames[0].height)});
}

Rect sellRect(const SidebarLayout& layout, const SidebarAssets& assets) {
  return localToSidebar(layout,
                        layout.side1Y,
                        Rect{84.0f,
                             8.0f,
                             static_cast<float>(assets.sellFrames[0].width),
                             static_cast<float>(assets.sellFrames[0].height)});
}

std::array<float, 4> tabXOffsets(const SidebarAssets& assets) {
  const float tabWidth = static_cast<float>(assets.tabFrames[0][0].width);
  if (tabWidth >= 32.0f) {
    return {20.0f, 52.0f, 84.0f, 116.0f};
  }
  return {26.0f, 55.0f, 84.0f, 113.0f};
}

float tabY(const SidebarAssets&, const SidebarTab) {
  return 40.0f;
}

Rect tabRect(const SidebarLayout& layout, const SidebarAssets& assets, const SidebarTab tab) {
  const auto xOffsets = tabXOffsets(assets);
  return localToSidebar(layout,
                        layout.side1Y,
                        Rect{xOffsets[tabIndex(tab)],
                             tabY(assets, tab),
                             static_cast<float>(assets.tabFrames[tabIndex(tab)][0].width),
                             static_cast<float>(assets.tabFrames[tabIndex(tab)][0].height)});
}

Rect tabDrawRect(const SidebarLayout& layout, const SidebarAssets& assets, const SidebarTab tab) {
  auto rect = tabRect(layout, assets, tab);
  const float textureHeight = static_cast<float>(assets.tabFrames[tabIndex(tab)][0].height);
  rect.y += std::max(0.0f, (rect.h - textureHeight) * 0.5f);
  rect.h = textureHeight;
  return rect;
}

Rect scrollDownRect(const SidebarLayout& layout, const SidebarAssets& assets) {
  return localToSidebar(layout,
                        layout.bottomY,
                        Rect{38.0f,
                             8.0f,
                             static_cast<float>(assets.scrollDownFrames[0].width),
                             static_cast<float>(assets.scrollDownFrames[0].height)});
}

Rect scrollUpRect(const SidebarLayout& layout, const SidebarAssets& assets) {
  return localToSidebar(layout,
                        layout.bottomY,
                        Rect{84.0f,
                             8.0f,
                             static_cast<float>(assets.scrollUpFrames[0].width),
                             static_cast<float>(assets.scrollUpFrames[0].height)});
}

Rect iconRect(const SidebarLayout& layout, const std::size_t visibleIndex) {
  const std::size_t row = visibleIndex / 2;
  const std::size_t col = visibleIndex % 2;
  return Rect{
    layout.left + kPowerPanelWidth + (col == 0 ? 5.0f : 68.0f),
    layout.panelY + static_cast<float>(row) * kRowHeight,
    kIconWidth,
    kIconHeight
  };
}

bool pointInRect(const float px, const float py, const Rect rect) {
  return px >= rect.x && px < rect.x + rect.w && py >= rect.y && py < rect.y + rect.h;
}

void drawTexturedRect(Renderer2D& renderer,
                      const UiTexture& texture,
                      const float x,
                      const float y,
                      const float width,
                      const float height,
                      const float u0 = 0.0f,
                      const float v0 = 0.0f,
                      const float u1 = 1.0f,
                      const float v1 = 1.0f,
                      const float r = 1.0f,
                      const float g = 1.0f,
                      const float b = 1.0f,
                      const float a = 1.0f) {
  if (texture.texture == 0 || width <= 0.0f || height <= 0.0f) {
    return;
  }

  const RenderVertex vertices[] = {
    {x, y, 0.0f, u0, v0, r, g, b, a},
    {x + width, y, 0.0f, u1, v0, r, g, b, a},
    {x + width, y + height, 0.0f, u1, v1, r, g, b, a},
    {x, y, 0.0f, u0, v0, r, g, b, a},
    {x + width, y + height, 0.0f, u1, v1, r, g, b, a},
    {x, y + height, 0.0f, u0, v1, r, g, b, a}
  };
  renderer.draw(GL_TRIANGLES, texture.texture, vertices, sizeof(vertices) / sizeof(vertices[0]));
}

void blitRgba(std::vector<std::uint8_t>& dst,
              const int dstWidth,
              const int dstHeight,
              const std::vector<std::uint8_t>& src,
              const int srcWidth,
              const int srcHeight,
              const int dstX,
              const int dstY) {
  for (int y = 0; y < srcHeight; ++y) {
    const int outY = dstY + y;
    if (outY < 0 || outY >= dstHeight) {
      continue;
    }
    for (int x = 0; x < srcWidth; ++x) {
      const int outX = dstX + x;
      if (outX < 0 || outX >= dstWidth) {
        continue;
      }

      const std::size_t srcIndex = (static_cast<std::size_t>(y) * srcWidth + x) * 4;
      const std::uint8_t alpha = src[srcIndex + 3];
      if (alpha == 0) {
        continue;
      }

      const std::size_t dstIndex = (static_cast<std::size_t>(outY) * dstWidth + outX) * 4;
      dst[dstIndex + 0] = src[srcIndex + 0];
      dst[dstIndex + 1] = src[srcIndex + 1];
      dst[dstIndex + 2] = src[srcIndex + 2];
      dst[dstIndex + 3] = alpha;
    }
  }
}

UiTexture composeSide1Texture(const SidebarAssets& assets, const SidebarState& state) {
  std::vector<std::uint8_t> rgba = assets.side1.rgba;

  const auto& repair = state.repairSelected ? assets.repairFrames[1] : assets.repairFrames[0];
  const auto& sell = state.sellSelected ? assets.sellFrames[1] : assets.sellFrames[0];
  const int repairX = assets.repairFrames[0].width <= 52 ? 32 : 20;
  blitRgba(rgba, assets.side1.width, assets.side1.height, repair.rgba, repair.width, repair.height, repairX, 8);
  blitRgba(rgba, assets.side1.width, assets.side1.height, sell.rgba, sell.width, sell.height, 84, 8);

  for (const auto tab : {SidebarTab::Base, SidebarTab::Defense, SidebarTab::Infantry, SidebarTab::Vehicles}) {
    const auto& frames = assets.tabFrames[tabIndex(tab)];
    const UiTexture* texture = nullptr;
    if (!state.tabVisible[tabIndex(tab)]) {
      texture = &frames[2];
    } else if (state.tabReady[tabIndex(tab)]) {
      if (tab == state.selectedTab) {
        texture = state.flashOn ? &frames[1] : &frames[0];
      } else {
        texture = state.flashOn ? &frames[4] : &frames[3];
      }
    } else {
      texture = (tab == state.selectedTab) ? &frames[1] : &frames[0];
    }

    const auto xOffsets = tabXOffsets(assets);
    const int localX = static_cast<int>(xOffsets[tabIndex(tab)]);
    const int localY = static_cast<int>(tabY(assets, tab));
    blitRgba(rgba, assets.side1.width, assets.side1.height, texture->rgba, texture->width, texture->height, localX, localY);
  }

  return uploadTexture(assets.side1.width, assets.side1.height, rgba);
}

UiTexture composeTopTexture(const SidebarAssets& assets) {
  std::vector<std::uint8_t> rgba = assets.top.rgba;
  blitRgba(rgba, assets.top.width, assets.top.height, assets.diplo.rgba, assets.diplo.width, assets.diplo.height, 12, 5);
  blitRgba(rgba, assets.top.width, assets.top.height, assets.opt.rgba, assets.opt.width, assets.opt.height, 84, 5);
  return uploadTexture(assets.top.width, assets.top.height, rgba);
}

UiTexture composeBottomTexture(const SidebarAssets& assets) {
  const int width = assets.side3.width;
  const int height = assets.side3.height + assets.addon.height;
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width * height * 4), 0);
  blitRgba(rgba, width, height, assets.side3.rgba, assets.side3.width, assets.side3.height, 0, 0);
  blitRgba(rgba, width, height, assets.addon.rgba, assets.addon.width, assets.addon.height, 0, assets.side3.height);
  blitRgba(rgba, width, height, assets.scrollDownFrames[0].rgba, assets.scrollDownFrames[0].width, assets.scrollDownFrames[0].height, 38, 8);
  blitRgba(rgba, width, height, assets.scrollUpFrames[0].rgba, assets.scrollUpFrames[0].width, assets.scrollUpFrames[0].height, 84, 8);
  return uploadTexture(width, height, rgba);
}

int maxScrollRows(const std::vector<SidebarIcon>& icons, const int visibleRows) {
  const int totalRows = static_cast<int>((icons.size() + 1) / 2);
  return std::max(0, totalRows - visibleRows);
}

const std::vector<SidebarIcon>& activePanelIcons(const SidebarAssets& assets, const SidebarState& state) {
  return assets.panelIcons[tabIndex(state.selectedTab)];
}

UiTexture loadOptionalCameo(const std::filesystem::path& cameoSpriteRoot,
                            const Palette& cameoPalette,
                            const std::string& name) {
  const auto path = findIndexedFile(cameoSpriteRoot, name + ".shp");
  if (!path.has_value()) {
    return makeBlankTexture(60, 48);
  }
  return loadShpFrameTexture(*path, cameoPalette, 0, true);
}

template <typename Fn>
void forEachPanelIconName(const BuildFaction faction, const SidebarTab tab, Fn&& fn) {
  switch (faction) {
    case BuildFaction::Allied:
      switch (tab) {
        case SidebarTab::Base:
          for (const auto* name : {
                 "powricon", "reficon", "brrkicon", "gwepicon", "heliicon",
                 "ayaricon", "fixicon", "techicon", "gorep"
               }) {
            fn(name);
          }
          return;
        case SidebarTab::Defense:
          for (const auto* name : {
                 "paraicon", "aparicon", "chroicon", "bolticon", "wallicon",
                 "pillicon", "samicon", "prisicon", "gapicon", "gcanicon",
                 "asaticon", "csphicon", "wethicon"
               }) {
            fn(name);
          }
          return;
        case SidebarTab::Infantry:
          for (const auto* name : {
                 "giicon", "engnicon", "adogicon", "jjeticon", "snipicon",
                 "spyicon", "sealicon", "tanyicon", "clegicon"
               }) {
            fn(name);
          }
          return;
        case SidebarTab::Vehicles:
          for (const auto* name : {
                 "ahrvicon", "gtnkicon", "fvicon", "tnkdicon", "beagicon",
                 "sreficon", "rtnkicon", "mcvicon", "falcicon", "shadicon",
                 "landicon", "desticon", "dlphicon", "agisicon", "carricon"
               }) {
            fn(name);
          }
          return;
      }
      return;
    case BuildFaction::Soviet:
      switch (tab) {
        case SidebarTab::Base:
          for (const auto* name : {
                 "npwricon", "nreficon", "handicon", "nwepicon", "nradicon",
                 "yardicon", "rfixicon", "ntchicon", "nrcticon"
               }) {
            fn(name);
          }
          return;
        case SidebarTab::Defense:
          for (const auto* name : {
                 "nwalicon", "flakicon", "samicon", "tslaicon",
                 "ironicon", "nukeicon", "npsiicon"
               }) {
            fn(name);
          }
          return;
        case SidebarTab::Infantry:
          for (const auto* name : {
                 "e1icon", "engnicon", "dogicon", "flkticon",
                 "ivanicon", "desoicon", "shkicon"
               }) {
            fn(name);
          }
          return;
        case SidebarTab::Vehicles:
          for (const auto* name : {
                 "harvicon", "htnkicon", "htkicon", "v3icon", "ttnkicon",
                 "mtnkicon", "sapcicon", "dronicon", "subicon", "dredicon",
                 "zepicon", "mcvicon"
               }) {
            fn(name);
          }
          return;
      }
      return;
  }
}
}

namespace {
struct SidebarNode {
  Rect bounds{};
  const UiTexture* texture = nullptr;
  float u0 = 0.0f;
  float v0 = 0.0f;
  float u1 = 1.0f;
  float v1 = 1.0f;
  float r = 1.0f;
  float g = 1.0f;
  float b = 1.0f;
  float a = 1.0f;
  SidebarClickAction action = SidebarClickAction::None;
  SidebarTab tab = SidebarTab::Base;
  std::string_view iconId{};
  bool visible = true;
  std::vector<SidebarNode> children;
};

Rect makeRect(const float x, const float y, const float w, const float h) {
  return Rect{x, y, w, h};
}

SidebarNode makeNode(const Rect bounds) {
  SidebarNode node{};
  node.bounds = bounds;
  return node;
}

SidebarNode makeImageNode(const Rect bounds,
                          const UiTexture& texture,
                          const float u0 = 0.0f,
                          const float v0 = 0.0f,
                          const float u1 = 1.0f,
                          const float v1 = 1.0f) {
  SidebarNode node = makeNode(bounds);
  node.texture = &texture;
  node.u0 = u0;
  node.v0 = v0;
  node.u1 = u1;
  node.v1 = v1;
  return node;
}

SidebarNode& appendChild(SidebarNode& parent, SidebarNode child) {
  parent.children.push_back(std::move(child));
  return parent.children.back();
}

const UiTexture& activeTabTexture(const SidebarAssets& assets, const SidebarState& state, const SidebarTab tab) {
  const auto& frames = assets.tabFrames[tabIndex(tab)];
  if (!state.tabVisible[tabIndex(tab)]) {
    return frames[2];
  }
  if (state.tabReady[tabIndex(tab)]) {
    if (tab == state.selectedTab) {
      return state.flashOn ? frames[1] : frames[0];
    }
    return state.flashOn ? frames[4] : frames[3];
  }
  return (tab == state.selectedTab) ? frames[1] : frames[0];
}

SidebarNode buildTopNode(const SidebarAssets& assets, const float y) {
  SidebarNode top = makeImageNode(makeRect(0.0f, y, static_cast<float>(assets.top.width), static_cast<float>(assets.top.height)),
                                  assets.top);
  appendChild(top, makeImageNode(makeRect(12.0f, 5.0f, static_cast<float>(assets.diplo.width), static_cast<float>(assets.diplo.height)),
                                 assets.diplo));
  appendChild(top, makeImageNode(makeRect(84.0f, 5.0f, static_cast<float>(assets.opt.width), static_cast<float>(assets.opt.height)),
                                 assets.opt));
  return top;
}

SidebarNode buildSide1Node(const SidebarAssets& assets, const SidebarState& state, const float y) {
  SidebarNode side1 = makeImageNode(makeRect(0.0f, y, static_cast<float>(assets.side1.width), static_cast<float>(assets.side1.height)),
                                    assets.side1);

  SidebarNode repair = makeNode(makeRect(assets.repairFrames[0].width <= 52 ? 32.0f : 20.0f,
                                         8.0f,
                                         static_cast<float>(assets.repairFrames[0].width),
                                         static_cast<float>(assets.repairFrames[0].height)));
  repair.action = SidebarClickAction::ToggleRepair;
  appendChild(repair, makeImageNode(makeRect(0.0f, 0.0f,
                                             static_cast<float>(assets.repairFrames[0].width),
                                             static_cast<float>(assets.repairFrames[0].height)),
                                    state.repairSelected ? assets.repairFrames[1] : assets.repairFrames[0]));
  appendChild(side1, std::move(repair));

  SidebarNode sell = makeNode(makeRect(84.0f, 8.0f,
                                       static_cast<float>(assets.sellFrames[0].width),
                                       static_cast<float>(assets.sellFrames[0].height)));
  sell.action = SidebarClickAction::ToggleSell;
  appendChild(sell, makeImageNode(makeRect(0.0f, 0.0f,
                                           static_cast<float>(assets.sellFrames[0].width),
                                           static_cast<float>(assets.sellFrames[0].height)),
                                  state.sellSelected ? assets.sellFrames[1] : assets.sellFrames[0]));
  appendChild(side1, std::move(sell));

  const auto tabX = tabXOffsets(assets);
  for (const auto tab : {SidebarTab::Base, SidebarTab::Defense, SidebarTab::Infantry, SidebarTab::Vehicles}) {
    const auto tabIdx = tabIndex(tab);
    const auto& texture = activeTabTexture(assets, state, tab);
    SidebarNode label = makeNode(makeRect(tabX[tabIdx],
                                          tabY(assets, tab),
                                          static_cast<float>(texture.width),
                                          static_cast<float>(texture.height)));
    if (state.tabVisible[tabIdx]) {
      label.action = SidebarClickAction::SelectTab;
      label.tab = tab;
    }
    appendChild(label,
                makeImageNode(makeRect(0.0f,
                                       0.0f,
                                       static_cast<float>(texture.width),
                                       static_cast<float>(texture.height)),
                              texture));
    appendChild(side1, std::move(label));
  }

  return side1;
}

SidebarNode buildPowerPanelNode(const SidebarAssets& assets, const int visibleRows) {
  SidebarNode powerPanel = makeNode(makeRect(0.0f, 0.0f, kPowerPanelWidth, static_cast<float>(visibleRows) * kRowHeight));
  const float powerU1 = kPowerPanelWidth / static_cast<float>(assets.side2.width);

  for (int row = 0; row < visibleRows; ++row) {
    appendChild(powerPanel,
                makeImageNode(makeRect(0.0f, static_cast<float>(row) * kRowHeight, kPowerPanelWidth, kRowHeight),
                              assets.side2,
                              0.0f,
                              0.0f,
                              powerU1,
                              1.0f));
  }

  const std::array<const char*, 42> powerPattern{{
    "rd","ru","none","rd","ru","none","rd","ru","none","rd","ru","none",
    "yd","yu","none","yd","yu","none","yd","yu","none","gd","gu","none",
    "gd","gu","none","gd","gu","none","gd","gu","none","gd","gu","none",
    "gd","gu","none","gd","gu","none"
  }};

  for (std::size_t i = 0; i < powerPattern.size(); ++i) {
    const std::string_view code = powerPattern[i];
    if (code == "none") {
      continue;
    }

    const UiTexture* line = nullptr;
    float v0 = 0.0f;
    float v1 = 0.5f;
    if (code.front() == 'r') {
      line = &assets.powerLines[3];
    } else if (code.front() == 'y') {
      line = &assets.powerLines[2];
    } else {
      line = &assets.powerLines[1];
    }
    if (code.back() == 'd') {
      v0 = 0.5f;
      v1 = 1.0f;
    }

    const float lineY = static_cast<float>(visibleRows) * kRowHeight - 1.0f - static_cast<float>(i);
    appendChild(powerPanel, makeImageNode(makeRect(5.0f, lineY, 12.0f, 1.0f), *line, 0.0f, v0, 1.0f, v1));
  }

  return powerPanel;
}

SidebarNode buildUnitPanelNode(const SidebarAssets& assets,
                               const SidebarState& state,
                               const SidebarTab tab,
                               const int visibleRows) {
  SidebarNode panel = makeNode(makeRect(0.0f, 0.0f, kSidebarWidth - kPowerPanelWidth, static_cast<float>(visibleRows) * kRowHeight));
  panel.visible = (state.selectedTab == tab);

  const auto& icons = assets.panelIcons[tabIndex(tab)];
  const int scrollRows = std::min(state.scrollRows[tabIndex(tab)], maxScrollRows(icons, visibleRows));
  const std::size_t startIndex = static_cast<std::size_t>(scrollRows * 2);
  const float u0 = kPowerPanelWidth / static_cast<float>(assets.side2.width);

  for (int row = 0; row < visibleRows; ++row) {
    SidebarNode rowNode = makeImageNode(makeRect(0.0f, static_cast<float>(row) * kRowHeight, kSidebarWidth - kPowerPanelWidth, kRowHeight),
                                        assets.side2,
                                        u0,
                                        0.0f,
                                        1.0f,
                                        1.0f);

    const std::size_t leftIndex = startIndex + static_cast<std::size_t>(row * 2);
    if (leftIndex < icons.size()) {
      SidebarNode iconNode = makeNode(makeRect(5.0f, 0.0f, kIconWidth, kIconHeight));
      iconNode.action = SidebarClickAction::ClickIcon;
      iconNode.iconId = icons[leftIndex].id;
      appendChild(iconNode, makeImageNode(makeRect(0.0f, 0.0f, kIconWidth, kIconHeight), icons[leftIndex].texture));
      appendChild(rowNode, std::move(iconNode));
    }

    const std::size_t rightIndex = leftIndex + 1;
    if (rightIndex < icons.size()) {
      SidebarNode iconNode = makeNode(makeRect(68.0f, 0.0f, kIconWidth, kIconHeight));
      iconNode.action = SidebarClickAction::ClickIcon;
      iconNode.iconId = icons[rightIndex].id;
      appendChild(iconNode, makeImageNode(makeRect(0.0f, 0.0f, kIconWidth, kIconHeight), icons[rightIndex].texture));
      appendChild(rowNode, std::move(iconNode));
    }

    appendChild(panel, std::move(rowNode));
  }

  return panel;
}

SidebarNode buildUnitCompNode(const SidebarAssets& assets, const SidebarState& state, const float y, const int visibleRows) {
  const float panelHeight = static_cast<float>(visibleRows) * kRowHeight;
  SidebarNode unitComp = makeNode(makeRect(0.0f, y, kSidebarWidth, panelHeight));

  appendChild(unitComp, buildPowerPanelNode(assets, visibleRows));

  SidebarNode rightPanel = makeNode(makeRect(kPowerPanelWidth, 0.0f, kSidebarWidth - kPowerPanelWidth, panelHeight));
  for (const auto tab : {SidebarTab::Base, SidebarTab::Defense, SidebarTab::Infantry, SidebarTab::Vehicles}) {
    appendChild(rightPanel, buildUnitPanelNode(assets, state, tab, visibleRows));
  }
  appendChild(unitComp, std::move(rightPanel));

  return unitComp;
}

SidebarNode buildBottomNode(const SidebarAssets& assets, const float y) {
  const float bottomHeight = static_cast<float>(assets.side3.height + assets.addon.height);
  SidebarNode bottom = makeNode(makeRect(0.0f, y, static_cast<float>(assets.side3.width), bottomHeight));
  appendChild(bottom, makeImageNode(makeRect(0.0f, 0.0f, static_cast<float>(assets.side3.width), static_cast<float>(assets.side3.height)),
                                    assets.side3));
  appendChild(bottom, makeImageNode(makeRect(0.0f, static_cast<float>(assets.side3.height), static_cast<float>(assets.addon.width), static_cast<float>(assets.addon.height)),
                                    assets.addon));

  SidebarNode scrollDown = makeNode(makeRect(38.0f, 8.0f,
                                             static_cast<float>(assets.scrollDownFrames[0].width),
                                             static_cast<float>(assets.scrollDownFrames[0].height)));
  scrollDown.action = SidebarClickAction::ScrollDown;
  appendChild(scrollDown, makeImageNode(makeRect(0.0f, 0.0f,
                                                 static_cast<float>(assets.scrollDownFrames[0].width),
                                                 static_cast<float>(assets.scrollDownFrames[0].height)),
                                        assets.scrollDownFrames[0]));
  appendChild(bottom, std::move(scrollDown));

  SidebarNode scrollUp = makeNode(makeRect(84.0f, 8.0f,
                                           static_cast<float>(assets.scrollUpFrames[0].width),
                                           static_cast<float>(assets.scrollUpFrames[0].height)));
  scrollUp.action = SidebarClickAction::ScrollUp;
  appendChild(scrollUp, makeImageNode(makeRect(0.0f, 0.0f,
                                               static_cast<float>(assets.scrollUpFrames[0].width),
                                               static_cast<float>(assets.scrollUpFrames[0].height)),
                                      assets.scrollUpFrames[0]));
  appendChild(bottom, std::move(scrollUp));

  return bottom;
}

SidebarNode buildSidebarTree(const SidebarAssets& assets,
                             const SidebarState& state,
                             const int viewportWidth,
                             const int viewportHeight) {
  const auto layout = makeLayout(static_cast<float>(viewportWidth) - kSidebarWidth, viewportHeight, assets);
  SidebarNode root = makeNode(makeRect(layout.left, 0.0f, kSidebarWidth, static_cast<float>(viewportHeight)));

  float y = 0.0f;
  appendChild(root, makeImageNode(makeRect(0.0f, y, static_cast<float>(assets.credits.width), static_cast<float>(assets.credits.height)),
                                  assets.credits));
  y += static_cast<float>(assets.credits.height);

  appendChild(root, buildTopNode(assets, y));
  y += static_cast<float>(assets.top.height);

  appendChild(root, makeImageNode(makeRect(0.0f, y, static_cast<float>(assets.radar.width), static_cast<float>(assets.radar.height)),
                                  assets.radar));
  y += static_cast<float>(assets.radar.height);

  appendChild(root, buildSide1Node(assets, state, y));
  y += static_cast<float>(assets.side1.height);

  appendChild(root, buildUnitCompNode(assets, state, y, layout.visibleRows));
  y += static_cast<float>(layout.visibleRows) * kRowHeight;

  appendChild(root, buildBottomNode(assets, y));
  return root;
}

void drawSidebarNode(Renderer2D& renderer, const SidebarNode& node, const float parentX, const float parentY) {
  if (!node.visible) {
    return;
  }

  const float absoluteX = parentX + node.bounds.x;
  const float absoluteY = parentY + node.bounds.y;
  if (node.texture != nullptr) {
    drawTexturedRect(renderer,
                     *node.texture,
                     absoluteX,
                     absoluteY,
                     node.bounds.w,
                     node.bounds.h,
                     node.u0,
                     node.v0,
                     node.u1,
                     node.v1,
                     node.r,
                     node.g,
                     node.b,
                     node.a);
  }

  for (const auto& child : node.children) {
    drawSidebarNode(renderer, child, absoluteX, absoluteY);
  }
}

SidebarClickResult hitTestSidebarNode(const SidebarNode& node,
                                      const float parentX,
                                      const float parentY,
                                      const float mouseX,
                                      const float mouseY) {
  SidebarClickResult result{};
  if (!node.visible) {
    return result;
  }

  const Rect absoluteRect{parentX + node.bounds.x, parentY + node.bounds.y, node.bounds.w, node.bounds.h};
  if (!pointInRect(mouseX, mouseY, absoluteRect)) {
    return result;
  }

  for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
    auto child = hitTestSidebarNode(*it, absoluteRect.x, absoluteRect.y, mouseX, mouseY);
    if (child.consumed) {
      return child;
    }
  }

  if (node.action != SidebarClickAction::None) {
    result.consumed = true;
    result.action = node.action;
    result.tab = node.tab;
    result.iconId = std::string(node.iconId);
  }
  return result;
}
}

SidebarAssets loadSidebarAssets(const std::filesystem::path& uiSpriteRoot,
                                const std::filesystem::path& cameoSpriteRoot,
                                const Palette& sidebarPalette,
                                const Palette& cameoPalette,
                                const BuildFaction faction) {
  SidebarAssets assets{};
  assets.credits = loadShpFrameTexture(uiSpriteRoot / "credits.shp", sidebarPalette, 0);
  assets.top = loadShpFrameTexture(uiSpriteRoot / "top.shp", sidebarPalette, 0);
  assets.diplo = loadShpFrameTexture(uiSpriteRoot / "diplobtn.shp", sidebarPalette, 0);
  assets.opt = loadShpFrameTexture(uiSpriteRoot / "optbtn.shp", sidebarPalette, 0);
  assets.radar = loadShpFrameTexture(uiSpriteRoot / "radar.shp", sidebarPalette, 0);
  assets.side1 = loadShpFrameTexture(uiSpriteRoot / "side1.shp", sidebarPalette, 0);
  assets.side2 = loadShpFrameTexture(uiSpriteRoot / "side2.shp", sidebarPalette, 0);
  assets.side3 = loadShpFrameTexture(uiSpriteRoot / "side3.shp", sidebarPalette, 0);
  assets.addon = loadShpFrameTexture(uiSpriteRoot / "addon.shp", sidebarPalette, 0);

  for (std::size_t i = 0; i < assets.powerLines.size(); ++i) {
    assets.powerLines[i] = loadShpFrameTexture(uiSpriteRoot / "powerp.shp", sidebarPalette, i);
  }
  for (std::size_t i = 0; i < 2; ++i) {
    assets.repairFrames[i] = loadShpFrameTexture(uiSpriteRoot / "repair.shp", sidebarPalette, i);
    assets.sellFrames[i] = loadShpFrameTexture(uiSpriteRoot / "sell.shp", sidebarPalette, i);
    assets.scrollDownFrames[i] = loadShpFrameTexture(uiSpriteRoot / "r-dn.shp", sidebarPalette, i);
    assets.scrollUpFrames[i] = loadShpFrameTexture(uiSpriteRoot / "r-up.shp", sidebarPalette, i);
  }
  for (std::size_t tab = 0; tab < 4; ++tab) {
    const auto tabName = std::string("tab0") + std::to_string(static_cast<int>(tab));
    for (std::size_t frame = 0; frame < 5; ++frame) {
      assets.tabFrames[tab][frame] = loadShpFrameTexture(uiSpriteRoot / (tabName + ".shp"), sidebarPalette, frame);
    }
  }

  const auto addPanel = [&](const SidebarTab tab) {
    auto& panel = assets.panelIcons[tabIndex(tab)];
    forEachPanelIconName(faction, tab, [&](const char* name) {
      panel.push_back(SidebarIcon{std::string(name), loadOptionalCameo(cameoSpriteRoot, cameoPalette, name)});
    });
  };

  addPanel(SidebarTab::Base);
  addPanel(SidebarTab::Defense);
  addPanel(SidebarTab::Infantry);
  addPanel(SidebarTab::Vehicles);

  return assets;
}

int computeSidebarVisibleRows(const SidebarAssets& assets, const int viewportHeight) {
  return makeLayout(0.0f, viewportHeight, assets).visibleRows;
}

void destroySidebarAssets(SidebarAssets& assets) {
  const auto destroyTexture = [](UiTexture& texture) {
    if (texture.texture != 0) {
      glDeleteTextures(1, &texture.texture);
      texture.texture = 0;
    }
  };

  destroyTexture(assets.credits);
  destroyTexture(assets.top);
  destroyTexture(assets.diplo);
  destroyTexture(assets.opt);
  destroyTexture(assets.radar);
  destroyTexture(assets.side1);
  destroyTexture(assets.side2);
  destroyTexture(assets.side3);
  destroyTexture(assets.addon);
  for (auto& texture : assets.powerLines) {
    destroyTexture(texture);
  }
  for (auto& texture : assets.repairFrames) {
    destroyTexture(texture);
  }
  for (auto& texture : assets.sellFrames) {
    destroyTexture(texture);
  }
  for (auto& texture : assets.scrollDownFrames) {
    destroyTexture(texture);
  }
  for (auto& texture : assets.scrollUpFrames) {
    destroyTexture(texture);
  }
  for (auto& tabFrames : assets.tabFrames) {
    for (auto& texture : tabFrames) {
      destroyTexture(texture);
    }
  }
  for (auto& panel : assets.panelIcons) {
    for (auto& icon : panel) {
      destroyTexture(icon.texture);
    }
    panel.clear();
  }
}

void updateSidebarState(SidebarState& state, const std::uint32_t nowTicks) {
  state.flashOn = ((nowTicks / 180U) % 2U) != 0U;
}

SidebarClickResult handleSidebarLeftClick(const SidebarState& state,
                                         const SidebarAssets& assets,
                                         const int viewportWidth,
                                         const int viewportHeight,
                                         const float mouseX,
                                         const float mouseY) {
  SidebarClickResult result{};
  const auto tree = buildSidebarTree(assets, state, viewportWidth, viewportHeight);
  if (!pointInRect(mouseX, mouseY, tree.bounds)) {
    return result;
  }

  result = hitTestSidebarNode(tree, 0.0f, 0.0f, mouseX, mouseY);
  if (!result.consumed) {
    result.consumed = true;
  }
  return result;
}

void applySidebarClick(SidebarState& state, const SidebarAssets& assets, const SidebarClickResult& click) {
  switch (click.action) {
    case SidebarClickAction::ToggleRepair:
      state.repairSelected = !state.repairSelected;
      if (state.repairSelected) {
        state.sellSelected = false;
      }
      break;
    case SidebarClickAction::ToggleSell:
      state.sellSelected = !state.sellSelected;
      if (state.sellSelected) {
        state.repairSelected = false;
      }
      break;
    case SidebarClickAction::SelectTab:
      state.selectedTab = click.tab;
      break;
    case SidebarClickAction::ScrollDown: {
      const auto selected = tabIndex(state.selectedTab);
      const int maxRows = maxScrollRows(assets.panelIcons[selected], state.visibleRowsHint);
      if (state.scrollRows[selected] < maxRows) {
        ++state.scrollRows[selected];
      }
      break;
    }
    case SidebarClickAction::ScrollUp:
      if (state.scrollRows[tabIndex(state.selectedTab)] > 0) {
        --state.scrollRows[tabIndex(state.selectedTab)];
      }
      break;
    case SidebarClickAction::ClickIcon:
    case SidebarClickAction::None:
      break;
  }
}

void drawSidebar(Renderer2D& renderer,
                 const int viewportWidth,
                 const int viewportHeight,
                 const SidebarAssets& assets,
                 const SidebarState& state) {
  const float left = static_cast<float>(viewportWidth) - kSidebarWidth;
  const auto layout = makeLayout(left, viewportHeight, assets);

  auto composedTop = composeTopTexture(assets);
  auto composedSide1 = composeSide1Texture(assets, state);
  auto composedBottom = composeBottomTexture(assets);

  drawTexturedRect(renderer, assets.credits, left, layout.creditsY, static_cast<float>(assets.credits.width), static_cast<float>(assets.credits.height));
  drawTexturedRect(renderer, composedTop, left, layout.topY, static_cast<float>(composedTop.width), static_cast<float>(composedTop.height));
  drawTexturedRect(renderer, assets.radar, left, layout.radarY, static_cast<float>(assets.radar.width), static_cast<float>(assets.radar.height));
  drawTexturedRect(renderer, composedSide1, left, layout.side1Y, static_cast<float>(composedSide1.width), static_cast<float>(composedSide1.height));

  const float powerU1 = kPowerPanelWidth / static_cast<float>(assets.side2.width);
  for (int row = 0; row < layout.visibleRows; ++row) {
    const float rowY = layout.panelY + static_cast<float>(row) * kRowHeight;
    drawTexturedRect(renderer, assets.side2, left, rowY, kPowerPanelWidth, kRowHeight, 0.0f, 0.0f, powerU1, 1.0f);
    drawTexturedRect(renderer, assets.side2, left + kPowerPanelWidth, rowY, kSidebarWidth - kPowerPanelWidth, kRowHeight, powerU1, 0.0f, 1.0f, 1.0f);
  }

  drawTexturedRect(renderer, composedBottom, left, layout.bottomY, static_cast<float>(composedBottom.width), static_cast<float>(composedBottom.height));

  const std::array<const char*, 42> powerPattern{{
    "rd","ru","none","rd","ru","none","rd","ru","none","rd","ru","none",
    "yd","yu","none","yd","yu","none","yd","yu","none","gd","gu","none",
    "gd","gu","none","gd","gu","none","gd","gu","none","gd","gu","none",
    "gd","gu","none","gd","gu","none"
  }};
  for (std::size_t i = 0; i < powerPattern.size(); ++i) {
    const float y = layout.panelY + static_cast<float>(layout.visibleRows) * kRowHeight - 1.0f - static_cast<float>(i);
    const std::string code = powerPattern[i];
    if (code == "none") {
      continue;
    }

    const UiTexture* line = nullptr;
    float v0 = 0.0f;
    float v1 = 0.5f;
    if (code.front() == 'r') {
      line = &assets.powerLines[3];
    } else if (code.front() == 'y') {
      line = &assets.powerLines[2];
    } else {
      line = &assets.powerLines[1];
    }
    if (code.back() == 'd') {
      v0 = 0.5f;
      v1 = 1.0f;
    }
    drawTexturedRect(renderer, *line, left + 5.0f, y, 12.0f, 1.0f, 0.0f, v0, 1.0f, v1);
  }

  const auto& icons = activePanelIcons(assets, state);
  const int scrollRows = std::min(state.scrollRows[tabIndex(state.selectedTab)],
                                  maxScrollRows(icons, layout.visibleRows));
  const std::size_t startIndex = static_cast<std::size_t>(scrollRows * 2);
  const std::size_t endIndex = std::min(icons.size(), startIndex + static_cast<std::size_t>(layout.visibleRows * 2));
  for (std::size_t iconIndex = startIndex; iconIndex < endIndex; ++iconIndex) {
    const auto rect = iconRect(layout, iconIndex - startIndex);
    drawTexturedRect(renderer, icons[iconIndex].texture, rect.x, rect.y, rect.w, rect.h);
  }

  glDeleteTextures(1, &composedTop.texture);
  glDeleteTextures(1, &composedSide1.texture);
  glDeleteTextures(1, &composedBottom.texture);
}
