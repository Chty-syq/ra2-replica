#include "building_system.h"
#include "demo_app_support.h"
#include "demo_scene.h"
#include "imgui_debug_panel.h"
#include "iso_grid_renderer.h"
#include "iso_world.h"
#include "palette.h"
#include "renderer2d.h"
#include "rhino_unit.h"
#include "rhino_render_state.h"
#include "rules_colors.h"
#include "sidebar.h"
#include "vpl_box_renderer.h"
#include "voxel_unit.h"
#include "weapon_system.h"
#include "presentation/world_view.h"
#include "presentation/naval_view.h"
#include "presentation/resource_view.h"
#include "presentation/special_vehicle_view.h"
#include "presentation/infantry_view.h"
#include "presentation/building_action_view.h"
#include "interaction/unit_selection.h"
#include <imgui.h>

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {
struct ViewportState { int width = DemoAppConfig::kViewportWidth; int height = DemoAppConfig::kViewportHeight; };
struct VehicleRendererCache { std::unordered_map<std::string, std::unique_ptr<VplBoxRenderer>> renderers; };
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
DemoVisualStyle opponentVisualStyle(DemoVisualStyle style,const HouseColorSet& colors) {
  style.houseColorIndex=(style.houseColorIndex+1)%colors.colors.size();
  for(std::size_t i=0;i<colors.colors.size();++i)if(ra2::canonicalId(colors.colors[i].name)=="RED")style.houseColorIndex=i;
  return style;
}
void destroyVehicleRendererCache(VehicleRendererCache& cache) {
  for (auto& [id, renderer] : cache.renderers) { (void)id; renderer->destroy(); }
  cache.renderers.clear();
}
void setVehicleRendererCachePalette(VehicleRendererCache& cache, const Palette& palette) {
  for (auto& [id, renderer] : cache.renderers) { (void)id; renderer->setPalette(palette); }
}
VplBoxRenderer& vehicleRendererForType(const ra2::UnitTypeDef& type, VehicleRendererCache& cache,
                                      SDL_Window* window, const DemoAppPaths& paths,
                                      const VplFile& lighting, const Palette& palette,int turretVariant=0) {
  const auto key=type.id+":"+std::to_string(turretVariant);
  if (const auto it = cache.renderers.find(key); it != cache.renderers.end()) return *it->second;
  if (type.visual.folder.empty() || type.visual.body.empty()) throw std::runtime_error("Missing visual mapping: " + type.id);
  auto renderer = std::make_unique<VplBoxRenderer>();
  renderer->initialize(window);
  const auto turret=type.visual.turret+((type.gunner || type.chargeTurrets) && turretVariant?std::to_string(turretVariant):"");
  const auto body=type.visual.body+(((type.missileLauncher && type.missileLauncher->alternateBody) || (type.navalSpawn && type.navalSpawn->alternateBody)) && turretVariant?"wo":"");
  renderer->loadVehicleAssets(paths.vehicleRoot / type.visual.folder, lighting, body, turret, type.visual.barrel);
  renderer->setPalette(palette);
  return *cache.renderers.emplace(key, std::move(renderer)).first->second;
}
VplBoxRenderer& navalTorpedoRenderer(VehicleRendererCache& cache,SDL_Window* window,const DemoAppPaths& paths,const VplFile& lighting,const Palette& palette,const std::string& stem="subt") {
  const std::string key="@projectile:"+stem;if(const auto it=cache.renderers.find(key);it!=cache.renderers.end())return *it->second;
  auto model=std::make_unique<VplBoxRenderer>();model->initialize(window);model->loadVehicleAssets(paths.effectsRoot/"projectiles_voxel",lighting,stem);model->setPalette(palette);return *cache.renderers.emplace(key,std::move(model)).first->second;
}
VplBoxRenderer& buildingTurretRenderer(const ra2::UnitTypeDef& type,VehicleRendererCache& cache,
                                       SDL_Window* window,const DemoAppPaths& paths,const VplFile& lighting,const Palette& palette) {
  const auto key="building:"+type.id;
  if(const auto found=cache.renderers.find(key);found!=cache.renderers.end())return *found->second;
  auto stem=type.buildingTurret;std::transform(stem.begin(),stem.end(),stem.begin(),[](unsigned char c){return std::tolower(c);});
  auto renderer=std::make_unique<VplBoxRenderer>();renderer->initialize(window);
  renderer->loadTurretAssets(paths.buildingRoot/"turrets",lighting,stem,"");renderer->setPalette(palette);
  return *cache.renderers.emplace(key,std::move(renderer)).first->second;
}
void cleanupApp(SDL_Window*& window,
                SDL_GLContext& glContext,
                Renderer2D& renderer,
                IsoGridRenderer& gridRenderer,
                VplBoxRenderer& rhinoRenderer,
                VplBoxRenderer& grizzlyRenderer,
                VehicleRendererCache& vehicleRendererCache,
                VplBoxRenderer& grandCannonRenderer,
                VplBoxRenderer& mcvRenderer,
                SidebarAssets& sidebarAssets,
                BuildingAssetCache& buildingAssetCache,
                RhinoUnitUiAssets& rhinoUiAssets,
                WeaponVisualAssets& weaponVisualAssets,
                ra2::ResourceVisualAssets& resourceVisualAssets,
                ra2::InfantryVisualAssets& infantryVisualAssets,
                ra2::SpecialVehicleAssets& specialVehicleAssets,
                ra2::BuildingActionVisualAssets& buildingActionAssets,ra2::NavalVisualAssets& navalVisualAssets) {
  ra2::destroyNavalVisualAssets(navalVisualAssets);
  shutdownImGuiDebugPanel();
  ra2::destroyBuildingActionVisualAssets(buildingActionAssets);
  ra2::destroyInfantryVisualAssets(infantryVisualAssets);
  ra2::destroySpecialVehicleAssets(specialVehicleAssets);
  ra2::destroyResourceVisualAssets(resourceVisualAssets);
  destroyWeaponVisualAssets(weaponVisualAssets);
  destroySidebarAssets(sidebarAssets);
  destroyBuildingAssetCache(buildingAssetCache);
  destroyRhinoUnitUiAssets(rhinoUiAssets);
  mcvRenderer.destroy();
  grandCannonRenderer.destroy();
  destroyVehicleRendererCache(vehicleRendererCache);
  grizzlyRenderer.destroy();
  rhinoRenderer.destroy();
  gridRenderer.destroy();
  renderer.destroy();

  if (glContext != nullptr) {
    SDL_GL_DeleteContext(glContext);
    glContext = nullptr;
  }
  if (window != nullptr) {
    SDL_DestroyWindow(window);
    window = nullptr;
  }

  SDL_Quit();
}

[[nodiscard]] ViewportState viewportStateForPanel(const ImGuiDebugPanelState& debugPanelState) {
  const std::size_t safeIndex =
    std::min(debugPanelState.display.resolutionIndex, std::size(kDisplayResolutionOptions) - 1);
  const auto& resolution = kDisplayResolutionOptions[safeIndex];
  return ViewportState{resolution.width, resolution.height};
}

[[nodiscard]] Vec2 mapOriginForViewport(const ViewportState& viewport) {
  return Vec2{viewport.width * 0.5f, DemoAppConfig::kMapOrigin.y};
}

[[nodiscard]] const char* defaultPowerPlantId(const BuildFaction faction) {
  switch (faction) {
    case BuildFaction::Allied: return "GAPOWR";
    case BuildFaction::Soviet: return "NAPOWR";
  }
  return "GAPOWR";
}

[[nodiscard]] const char* mcvUnitIdForFaction(const BuildFaction faction) {
  switch (faction) {
    case BuildFaction::Allied: return "AMCV";
    case BuildFaction::Soviet: return "SMCV";
  }
  return "AMCV";
}

[[nodiscard]] bool isLayeredWarFactoryInstance(const BuildingInstance& building) {
  return building.layeredFactory &&
         building.state == BuildingState::Complete;
}

[[nodiscard]] float normalizeAngleRadians(float radians) {
  while (radians < 0.0f) {
    radians += kTwoPi;
  }
  while (radians >= kTwoPi) {
    radians -= kTwoPi;
  }
  return radians;
}

[[nodiscard]] float shortestAngleDelta(const float fromRadians, const float toRadians) {
  float delta = normalizeAngleRadians(toRadians) - normalizeAngleRadians(fromRadians);
  if (delta > kPi) {
    delta -= kTwoPi;
  } else if (delta < -kPi) {
    delta += kTwoPi;
  }
  return delta;
}

[[nodiscard]] Vec2 buildingTurretAnchor(const BuildingInstance& building,
                                           const ra2::IniDocument& rules,
                                           const Vec2 mapOrigin,
                                           const float tileWidth,
                                           const float tileHeight) {
  const auto center = buildingDrawCenter(building.placement, mapOrigin, tileWidth, tileHeight);
  return {center.x + rules.integer(building.assetId, "TurretAnimX"),
          center.y + rules.integer(building.assetId, "TurretAnimY")};
}

[[nodiscard]] VplBoxRendererState buildingTurretRenderState(const HouseColorSet& houseColors,
                                                         const ImGuiDebugPanelState& debugPanelState,
                                                         const BuildingInstance& building) {
  auto turretDebugState=debugPanelState;
  turretDebugState.rhinoTransform={};
  auto state = buildRhinoTankRenderState(houseColors,
                                         turretDebugState,
                                         building.turretHeadingRadians);
  state.bodyRotationDegrees = 0.0f;
  state.turretRotationDegrees = 0.0f;
  state.turretOffsetPixels = 0.0f;
  // TurretAnimX/Y place the VXL on the SHP base. TurretAnimZAdjust is a
  // sorting adjustment, not a physical height; lifting the model opens a gap.
  state.modelOffset={0,0,0};
  if(building.owner)state.remapColor={235,45,45,255};
  if(building.frozen)state.objectTint={.4f,.7f,1};
  state.shadowAlpha = 0.0f;
  return state;
}

[[nodiscard]] float buildingTurretDepth(const BuildingPlacement& placement,
                                           const float tileWidth,
                                           const float tileHeight) {
  constexpr float kTurretDepthBias01 = 0.00008f;

  float frontMostDepth = 0.999f;
  for (const auto tile : foundationTiles(placement)) {
    const auto tileDepth = depthFromLogicalCenter(Vec2{static_cast<float>(tile.x),
                                                       static_cast<float>(tile.y)},
                                                 tileWidth,
                                                 tileHeight);
    frontMostDepth = std::min(frontMostDepth, tileDepth);
  }
  return std::clamp(frontMostDepth - kTurretDepthBias01, 0.001f, 0.999f);
}

[[nodiscard]] bool rhinoOccupiesMouseTile(const RhinoUnitState& tank, const TileCoord mouseTile) {
  const int minX = static_cast<int>(std::floor(tank.tilePosition.x));
  const int maxX = static_cast<int>(std::ceil(tank.tilePosition.x));
  const int minY = static_cast<int>(std::floor(tank.tilePosition.y));
  const int maxY = static_cast<int>(std::ceil(tank.tilePosition.y));
  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      if (mouseTile.x == x && mouseTile.y == y) {
        return true;
      }
    }
  }
  return mouseTile.x == tank.occupiedCell.x && mouseTile.y == tank.occupiedCell.y;
}

[[nodiscard]] bool drawnWithFactory(const RhinoUnitState& vehicle, const std::vector<BuildingInstance>& buildings) {
  return std::any_of(buildings.begin(), buildings.end(), [&](const auto& factory) {
    return ra2::isFactoryExitVehicle(vehicle, factory);
  });
}

[[nodiscard]] bool isStopCommandEvent(const SDL_Event& event) {
  return event.type == SDL_KEYDOWN &&
         (event.key.keysym.sym == SDLK_s || event.key.keysym.scancode == SDL_SCANCODE_S);
}

[[nodiscard]] bool isCtrlModifierDown() {
  int keyCount = 0;
  const auto* keyboardState = SDL_GetKeyboardState(&keyCount);
  const bool scancodeCtrl =
    keyboardState != nullptr &&
    keyCount > SDL_SCANCODE_RCTRL &&
    (keyboardState[SDL_SCANCODE_LCTRL] != 0 || keyboardState[SDL_SCANCODE_RCTRL] != 0);
  return (SDL_GetModState() & KMOD_CTRL) != 0 || scancodeCtrl;
}

[[nodiscard]] std::optional<std::size_t> hitTestRhinoTanks(const std::vector<RhinoUnitState>& tanks,
                                                           const float mouseX,
                                                           const float mouseY,
                                                           const Vec2 mapOrigin,
                                                           const float tileWidth,
                                                           const float tileHeight) {
  const auto mouseTile = screenToIso(mouseX, mouseY, mapOrigin, tileWidth, tileHeight);
  for (std::size_t reverseIndex = tanks.size(); reverseIndex > 0; --reverseIndex) {
    const std::size_t index = reverseIndex - 1;
    if (rhinoOccupiesMouseTile(tanks[index], mouseTile)) {
      return index;
    }
  }
  return std::nullopt;
}

[[nodiscard]] SidebarAssets loadSidebarAssetsForStyle(const DemoAppPaths& paths,
                                                      const DemoVisualStyle& style,
                                                      const Palette& cameoPalette) {
  const auto sidebarPalette = Palette::load(sidebarPalettePathForFaction(paths, style.faction));
  return loadSidebarAssets(sidebarUiRootForFaction(paths, style.faction),
                           paths.iconRoot,
                           sidebarPalette,
                           cameoPalette);
}

void applyViewportState(SDL_Window* window,
                        Renderer2D& renderer,
                        IsoGridRenderer& gridRenderer,
                        const ViewportState& viewport) {
  SDL_SetWindowSize(window, viewport.width, viewport.height);
  renderer.setViewport(viewport.width, viewport.height);
  gridRenderer.setViewport(viewport.width, viewport.height);
}

// Exercise the real GPU path, including coverage, tinting, world depth and life.
void verifyLaserBeamRendering(Renderer2D& renderer,const ra2::DefinitionRegistry& definitions,int viewportHeight,
                              const std::filesystem::path& outputDirectory) {
  const Rgba blue{24,96,208,255},red{208,64,20,255};
  const ra2::BeamSnapshot far{{0,0},{6000,-6000},0,0,10,15,0,"COMET"},
                          near{{1000,1000},{7000,-5000},256,256,10,15,1,"COMETFRAGMENT"};
  const auto render=[&](std::span<const ra2::BeamSnapshot> rays,ra2::Tick frame,Rgba background,bool occluder=false) {
    renderer.beginFrame(background.r/255.f,background.g/255.f,background.b/255.f,1);
    if(occluder) {
      renderer.beginWorldPass();
      const std::array<RenderVertex,6> vertices{{{180,130,.01f},{220,130,.01f},{220,150,.01f},
                                                {180,130,.01f},{220,150,.01f},{180,150,.01f}}};
      renderer.draw(GL_TRIANGLES,renderer.whiteTexture(),vertices.data(),vertices.size());
    }
    ra2::drawLaserBeams(renderer,rays,definitions,frame,{80,140},60,30,blue,red);
    std::array<std::uint8_t,11*4> pixels{};
    glReadPixels(200,viewportHeight-145,1,11,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    return pixels;
  };
  const auto peak=[](const auto& pixels,int channel) {
    int best=0;for(int y=1;y<11;++y)if(pixels[y*4+channel]>pixels[best*4+channel])best=y;
    return Rgba{pixels[best*4],pixels[best*4+1],pixels[best*4+2],255};
  };
  const auto full=render(std::span(&far,1),10,{});const auto color=peak(full,2);
  if(color.b<180 || color.b>220 || color.b<2*color.g || color.g<2*color.r)
    throw std::runtime_error("Laser pixel regression: house-color beam brightness/hue");
  int width=0;for(int i=0;i<11;++i)width+=full[i*4+2]>4;
  if(width<2 || width>5)throw std::runtime_error("Laser pixel regression: native-scale beam width");
  int previous=color.b;
  for(ra2::Tick age=1;age<15;++age) {
    const auto aged=peak(render(std::span(&far,1),10+age,{}),2);
    if(aged.b<=0 || aged.b>previous)throw std::runtime_error("Laser pixel regression: beam life must fade monotonically");
    previous=aged.b;
  }
  if(previous>color.b/4)throw std::runtime_error("Laser pixel regression: last frame remains too bright");
  const Rgba background{112,128,96,255};const auto tinted=peak(render(std::span(&far,1),10,background),2);
  if(tinted.r>=background.r || tinted.g>=background.g || tinted.b>220)
    throw std::runtime_error("Laser pixel regression: additive overexposure returned");
  const std::array repeated{far,far};const auto twice=peak(render(repeated,10,{}),2);
  if(twice.b!=color.b)throw std::runtime_error("Laser pixel regression: a crossing burns brighter than its source color");
  const std::array crossing{near,far};const auto combined=peak(render(crossing,10,{}),2);
  if(combined.b!=color.b)throw std::runtime_error("Laser pixel regression: a previous beam wrote world depth");
  const auto hidden=render(std::span(&far,1),10,{},true);
  for(int i=0;i<11;++i)if(hidden[i*4]!=255 || hidden[i*4+1]!=255 || hidden[i*4+2]!=255)
    throw std::runtime_error("Laser pixel regression: world geometry does not occlude the beam");
  const auto expired=render(std::span(&far,1),25,{});
  const auto future=render(std::span(&far,1),9,{});
  for(int i=0;i<11;++i)if(expired[i*4] || expired[i*4+1] || expired[i*4+2] || future[i*4+2])
    throw std::runtime_error("Laser pixel regression: beam visible outside its life");

  // All eight projected directions, fractional camera positions and zooms.
  const std::array<ra2::Position,8> directions{{{4000,-4000},{4000,0},{4000,4000},{0,4000},
                                               {-4000,4000},{-4000,0},{-4000,-4000},{0,-4000}}};
  for(float zoom:{.5f,1.f,2.f})for(const auto delta:directions) {
    auto ray=far;ray.end=delta;
    const auto capture=[&](const ra2::BeamSnapshot& beam) {
      renderer.beginFrame(0,0,0,1);
      ra2::drawLaserBeams(renderer,std::span(&beam,1),definitions,10,{280.25f,280.375f},60*zoom,30*zoom,blue,red);
      std::vector<std::uint8_t> pixels(520*520*4);
      glReadPixels(20,viewportHeight-540,520,520,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
      return pixels;
    };
    const auto forward=capture(ray);
    std::swap(ray.start,ray.end);
    const auto backward=capture(ray);
    int covered=0,partial=0;
    for(std::size_t i=0;i<forward.size();i+=4) {
      covered+=forward[i+2]>4;partial+=forward[i+2]>4 && forward[i+2]<color.b-4;
      if(std::abs(int(forward[i+2])-backward[i+2])>2)
        throw std::runtime_error("Laser pixel regression: endpoint direction changes antialias coverage");
    }
    if(covered<20 || partial<10)throw std::runtime_error("Laser pixel regression: missing sloped/subpixel coverage");
  }
  renderer.beginFrame(background.r/255.f,background.g/255.f,background.b/255.f,1);
  ra2::drawLaserBeams(renderer,std::span(&far,1),definitions,10,{80,140},60,30,{0,0,208,255},red);
  std::array<std::uint8_t,4> channelPixel{};
  glReadPixels(200,viewportHeight-141,1,1,GL_RGBA,GL_UNSIGNED_BYTE,channelPixel.data());
  if(channelPixel[0]!=background.r || channelPixel[1]!=background.g || channelPixel[2]<180)
    throw std::runtime_error("Laser pixel regression: zero color channels must preserve the background");
  GLboolean depthWrite=GL_FALSE,channels[4]{};
  glGetBooleanv(GL_DEPTH_WRITEMASK,&depthWrite);glGetBooleanv(GL_COLOR_WRITEMASK,channels);
  GLint blend=0;glGetIntegerv(GL_BLEND_DST_RGB,&blend);
  if(!depthWrite || !std::all_of(std::begin(channels),std::end(channels),[](auto c){return c==GL_TRUE;}) ||
     blend!=GL_ONE_MINUS_SRC_ALPHA || glGetError()!=GL_NO_ERROR)
    throw std::runtime_error("Laser pixel regression: renderer state was not restored");

  // Optional review artifact: real GPU output, three ages and eight directions.
  if(!outputDirectory.empty()) {
    renderer.beginFrame(.42f,.44f,.46f,1);
    for(int row=0;row<3;++row)for(int col=0;col<8;++col) {
      auto ray=far;ray.end={directions[col].x/2,directions[col].y/2};
      ra2::drawLaserBeams(renderer,std::span(&ray,1),definitions,10+row*7,
                           {80.f+col*150,75.f+row*140},60,30,blue,red);
    }
    constexpr std::array<Rgba,8> colors{{{255,24,24,255},{255,192,24,255},{48,208,48,255},{24,208,208,255},
                                        {24,96,208,255},{192,24,208,255},{240,240,240,255},{0,0,208,255}}};
    auto ray=far;ray.end={2000,-2000};
    for(int col=0;col<8;++col)
      ra2::drawLaserBeams(renderer,std::span(&ray,1),definitions,10,{35.f+col*150,485},60,30,colors[col],red);
    std::vector<std::uint8_t> pixels(1280*540*4);
    glReadPixels(0,viewportHeight-540,1280,540,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    for(int y=0;y<270;++y)std::swap_ranges(pixels.begin()+y*1280*4,pixels.begin()+(y+1)*1280*4,pixels.begin()+(539-y)*1280*4);
    SDL_Surface* surface=SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(),1280,540,32,1280*4,SDL_PIXELFORMAT_RGBA32);
    if(!surface)throw std::runtime_error(SDL_GetError());
    const int result=SDL_SaveBMP(surface,(outputDirectory/"beam-atlas.bmp").string().c_str());SDL_FreeSurface(surface);
    if(result!=0)throw std::runtime_error(SDL_GetError());
  }
  std::cout<<"Laser GPU readback passed: house tint, 15-frame fade, depth, 8 directions and 3 zooms\n";
}

// Mirage uses the complete frontal RTNK voxel and the terrain sprite's own
// canvas/offset/palette. Assert these properties against actual GPU output.
void verifyMirageRendering(Renderer2D& renderer,VplBoxRenderer& model,const HouseColorSet& houseColors,
                            const PaletteLibrary& palettes,const ra2::SpecialVehicleAssets& assets,
                            const std::filesystem::path& assetRoot,int viewportHeight,
                            const std::filesystem::path& outputDirectory) {
  constexpr std::array<const char*,3> folders{"temperate","snow","urban"},extensions{".tem",".sno",".urb"};
  std::vector<std::uint8_t> atlas(1024*384*4);
  const auto remap=renderer.remapColor();
  for(int theater=0;theater<3;++theater) {
    const auto style=static_cast<TheaterStyle>(theater);
    const auto& palette=palettesForTheater(palettes,style);
    model.setPalette(palette.unit);
    renderer.setIndexedPalettes(palette.unit,palette.terrain);
    for(int facing=0;facing<8;++facing) {
      auto state=buildRhinoTankRenderState(houseColors,ImGuiDebugPanelState{},float(facing)*kPi/4);
      auto first=model.renderToTexture(state,128,128);
      state.turretRotationDegrees=137;
      auto second=model.renderToTexture(state,128,128);
      if(first.rgba!=second.rgba)throw std::runtime_error("Mirage GPU regression: Turret=no model has a rotating attachment");
      int opaque=0;for(std::size_t i=3;i<first.rgba.size();i+=4)opaque+=first.rgba[i]>0;
      if(opaque<100)throw std::runtime_error("Mirage GPU regression: complete RTNK body is missing");
      for(int y=0;y<128;++y)std::copy_n(first.rgba.data()+y*128*4,128*4,
        atlas.data()+((theater*128+y)*1024+facing*128)*4);
      glDeleteTextures(1,&first.texture);glDeleteTextures(1,&second.texture);
    }
    for(int tree=1;tree<=4;++tree) {
      const std::string name="tree0"+std::to_string(tree);
      const auto shp=ShpTsFile::load(assetRoot/"terrain"/"disguises"/folders[theater]/(name+extensions[theater]));
      const auto body=shp.decodeFrame(0),shadow=shp.decodeFrame(1);
      RhinoUnitState unit;unit.tilePosition={0,0};unit.disguiseImage="TREE0"+std::to_string(tree);
      const auto capture=[&](Rgba color) {
        renderer.beginFrame(128.f/255,128.f/255,128.f/255,1);
        renderer.setRemapColor(color);
        ra2::drawVehicleDisguise(renderer,assets,style,unit,{100.5f,130.5f},60,30);
        std::vector<std::uint8_t> pixels(200*180*4);
        glReadPixels(0,viewportHeight-180,200,180,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        return pixels;
      };
      const auto pixels=capture({255,0,0,255});
      if(pixels!=capture({0,0,255,255}))throw std::runtime_error("Mirage GPU regression: terrain disguise acquired a house remap");
      const int left=int(100.5f-1-body.width*.5f),top=int(130.5f-16-body.height*.5f);
      int minX=200,minY=180,shadowSamples=0;
      for(int y=0;y<180;++y)for(int x=0;x<200;++x) {
        const auto i=((179-y)*200+x)*4;
        if(pixels[i]!=128 || pixels[i+1]!=128 || pixels[i+2]!=128) {minX=std::min(minX,x);minY=std::min(minY,y);}
      }
      if(std::abs(minX-(left+body.frameX))>1 || std::abs(minY-(top+body.frameY))>1)
        throw std::runtime_error("Mirage GPU regression: terrain canvas offset was lost");
      for(int y=1;y<body.height-1;++y)for(int x=1;x<body.width-1;++x) {
        bool emptyBody=true,fullShadow=true;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
          const auto i=(y+dy)*body.width+x+dx;
          emptyBody&=body.indices[i]==0;fullShadow&=shadow.indices[i]!=0;
        }
        const int px=left+x,py=top+y;
        if(!emptyBody || !fullShadow || px<0 || px>=200 || py<0 || py>=180)continue;
        const auto i=((179-py)*200+px)*4;
        if(std::abs(int(pixels[i])-64)>1 || std::abs(int(pixels[i+1])-64)>1 || std::abs(int(pixels[i+2])-64)>1)
          throw std::runtime_error("Mirage GPU regression: original SHP shadow is not a half-opacity shadow");
        ++shadowSamples;
      }
      if(shadowSamples<10)throw std::runtime_error("Mirage GPU regression: original terrain shadow is missing");
    }
  }
  model.setPalette(palettes.temperate.unit);
  renderer.setIndexedPalettes(palettes.temperate.unit,palettes.temperate.terrain);
  renderer.setRemapColor(remap);
  renderer.setViewport(DemoAppConfig::kViewportWidth,viewportHeight);
  if(!outputDirectory.empty()) {
    auto* surface=SDL_CreateRGBSurfaceWithFormatFrom(atlas.data(),1024,384,32,1024*4,SDL_PIXELFORMAT_RGBA32);
    if(!surface)throw std::runtime_error(SDL_GetError());
    const int saved=SDL_SaveBMP(surface,(outputDirectory/"mirage-body-atlas.bmp").string().c_str());SDL_FreeSurface(surface);
    if(saved!=0)throw std::runtime_error(SDL_GetError());
  }
  std::cout<<"Mirage GPU readback passed: frontal RTNK body, 8 facings, 3 theaters, terrain offset/palette/shadows\n";
}

void verifyMirageTimingRendering(Renderer2D& renderer,VplBoxRenderer& model,const HouseColorSet& colors,
                                  const ra2::DefinitionRegistry& definitions,const ra2::SpecialVehicleAssets& trees,
                                  const RhinoUnitUiAssets& ui,const WeaponVisualAssets& weapons,int viewportHeight,
                                  const std::filesystem::path& outputDirectory) {
  auto rules=definitions.rules();rules.overlay(ra2::IniDocument::parse(
    "[MGTK]\nCanPassiveAquire=no\nStrength=10000\n[HTNK]\nCanPassiveAquire=no\nStrength=10000\n"));
  const auto d=ra2::DefinitionRegistry::compile(rules,definitions.art(),definitions.replica());
  ra2::World world(d);const auto tank=world.spawn("MGTK",{}),target=world.spawn("HTNK",{4,0},1);
  world.enqueue({35,0,1,ra2::CommandKind::AttackEntity,tank,target});world.enqueue({36,0,2,ra2::CommandKind::Stop,tank});
  constexpr std::array<ra2::Tick,11> frames{0,14,15,29,30,35,42,49,50,64,65};
  constexpr int size=128,width=size*frames.size(),height=size*2;
  constexpr int sequenceHeight=size*12;
  std::vector<std::uint8_t> atlas(width*height*4),sequence(width*sequenceHeight*4);
  std::array<std::vector<std::uint8_t>,2> treeReference,bodyReference;
  const auto capture=[&](const RhinoUnitState& unit,const std::vector<CombatEffectInstance>& effects) {
    renderer.beginFrame(128.f/255,128.f/255,128.f/255,1);
    if(unit.disguiseVisible) {
      ra2::drawVehicleDisguise(renderer,trees,TheaterStyle::Temperate,unit,{64.5f,100.5f},60,30);
      renderer.beginUiPass();drawRhinoHealthBar(renderer,ui,unit,{64.5f,100.5f},60,30);
    } else {
      const auto state=buildRhinoTankRenderState(colors,ImGuiDebugPanelState{},unit.headingRadians);
      drawRhinoUnit(renderer,model,ui,unit,{64.5f,100.5f},60,30,1,
                    DemoAppConfig::kViewportWidth,viewportHeight,0,state);
    }
    drawWeaponProjectilesAndEffects(renderer,weapons,{},effects,{64.5f,100.5f},60,30);
    std::vector<std::uint8_t> pixels(size*size*4);
    glReadPixels(0,viewportHeight-size,size,size,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());return pixels;
  };
  for(ra2::Tick frame=0;frame<=65;++frame) {
    while(world.tick()<frame)world.step();
    for(int viewer=0;viewer<2;++viewer) {
      const auto view=ra2::makeWorldView(world,d,viewer==0?tank:0,viewer);
      const auto unit=*std::find_if(view.vehicles.begin(),view.vehicles.end(),[&](const auto& u){return u.entityId==tank;});
      const bool expected=viewer?(frame<35 || frame>=50):(frame<15 || (frame>=30 && frame<35) || (frame>=50 && frame<65));
      if(unit.disguiseVisible!=expected)throw std::runtime_error("Mirage GPU timeline: wrong viewer/form at frame "+std::to_string(world.tick()));
      const auto plain=capture(unit,{});
      if(frame==0) {
        treeReference[viewer]=plain;auto body=unit;body.disguiseVisible=false;bodyReference[viewer]=capture(body,{});
        if(treeReference[viewer]==bodyReference[viewer])throw std::runtime_error("Mirage GPU timeline: body/tree reference images are identical");
      }
      if(plain!=(expected?treeReference[viewer]:bodyReference[viewer]))throw std::runtime_error("Mirage GPU timeline: actual rendered form differs from reference");
      const auto pixels=capture(unit,view.effects);
      if(world.tick()==35 && pixels==plain)throw std::runtime_error("Mirage GPU timeline: original VTMUZZLE missing on the firing frame");
      const auto key=std::find(frames.begin(),frames.end(),frame);
      for(int y=0;y<size;++y) {
        const auto* row=pixels.data()+(size-1-y)*size*4;
        if(key!=frames.end())std::copy_n(row,size*4,atlas.data()+((viewer*size+y)*width+(key-frames.begin())*size)*4);
        std::copy_n(row,size*4,sequence.data()+(((viewer*6+frame/11)*size+y)*width+(frame%11)*size)*4);
      }
    }
  }
  if(!outputDirectory.empty()) {
    auto* surface=SDL_CreateRGBSurfaceWithFormatFrom(atlas.data(),width,height,32,width*4,SDL_PIXELFORMAT_RGBA32);
    if(!surface)throw std::runtime_error(SDL_GetError());
    const int saved=SDL_SaveBMP(surface,(outputDirectory/"mirage-timing-atlas.bmp").string().c_str());SDL_FreeSurface(surface);
    if(saved!=0)throw std::runtime_error(SDL_GetError());
    surface=SDL_CreateRGBSurfaceWithFormatFrom(sequence.data(),width,sequenceHeight,32,width*4,SDL_PIXELFORMAT_RGBA32);
    if(!surface)throw std::runtime_error(SDL_GetError());
    const int sequenceSaved=SDL_SaveBMP(surface,(outputDirectory/"mirage-timing-sequence.bmp").string().c_str());SDL_FreeSurface(surface);
    if(sequenceSaved!=0)throw std::runtime_error(SDL_GetError());
  }
  std::cout<<"Mirage GPU timeline passed: 66 continuous frames, owner/enemy, original firing effect, 15-frame reveal and return to tree\n";
}

// Hidden-window regression: compare rendered source pixels, not just enum values.
// This catches palette routing, accidental house remaps and the old 0.82 dimming.
void verifyMineralPaletteRendering(Renderer2D& renderer, const ra2::ResourceVisualAssets& assets,
                                   const PaletteLibrary& library, const Palette& animationPalette,
                                   const std::filesystem::path& assetRoot, int viewportHeight) {
  constexpr std::array<const char*,3> directories{"temperate","snow","urban"};
  constexpr std::array<const char*,3> extensions{".tem",".sno",".urb"};
  constexpr std::array<const char*,3> palettes{"temperat.pal","snow.pal","urban.pal"};
  for (std::size_t theater=0;theater<directories.size();++theater) {
    const auto& active=palettesForTheater(library,static_cast<TheaterStyle>(theater));
    renderer.setIndexedPalettes(active.unit,active.terrain);
    renderer.setResourcePalettes(active.resource,animationPalette);
    renderer.setRemapColor({0,255,255,255});
    const auto expectedResource=Palette::load(assetRoot/"palettes"/"resources"/palettes[theater]);
    for(int kind=0;kind<3;++kind) {
      const bool effect=kind==2;
      const auto source=effect ? assetRoot/"effects"/"harvesting"/"oregath.shp" :
        assetRoot/"terrain"/"resources"/directories[theater]/(std::string(kind==0?"tib01":"gem01")+extensions[theater]);
      const auto frame=ShpTsFile::load(source).decodeFrame(effect?6:11);
      const auto& texture=effect ? assets.gathering : assets.overlays[theater][kind];
      const auto& palette=effect ? animationPalette : expectedResource;
      const float u0=effect?6.f/15:11.f/12, u1=effect?7.f/15:1.f, v1=effect?1.f/8:1.f/12;
      const float w=frame.width,h=frame.height;
      const std::array<RenderVertex,6> quad{{{0,0,0,u0,0},{w,0,0,u1,0},{w,h,0,u1,v1},
                                            {0,0,0,u0,0},{w,h,0,u1,v1},{0,h,0,u0,v1}}};
      renderer.beginFrame(0,0,0,1);
      renderer.beginUiPass();
      renderer.draw(GL_TRIANGLES,texture,quad.data(),quad.size());
      std::vector<std::uint8_t> pixels(frame.width*frame.height*4);
      glReadPixels(0,viewportHeight-frame.height,frame.width,frame.height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
      for(int y=0;y<frame.height;++y) for(int x=0;x<frame.width;++x) {
        const auto index=frame.indices[y*frame.width+x];
        const auto expected=index==0?Rgba{0,0,0,255}:palette.color(index);
        const auto offset=((frame.height-1-y)*frame.width+x)*4;
        if(std::abs(int(pixels[offset])-expected.r)>1 || std::abs(int(pixels[offset+1])-expected.g)>1 ||
           std::abs(int(pixels[offset+2])-expected.b)>1)
          throw std::runtime_error("Mineral palette pixel mismatch: "+source.string()+" index="+std::to_string(index));
      }
    }
  }
  std::cout<<"Mineral palette GPU readback passed for ore, gems and gathering in all 3 theaters\n";
}

void verifyMixedSidebar(Renderer2D& renderer,SDL_Window* window,const DemoAppPaths& paths,const Palette& cameoPalette,
                         const ra2::DefinitionRegistry& definitions,int width,int height,
                         const std::filesystem::path& outputDirectory) {
  constexpr int sidebarWidth=DemoAppConfig::kSidebarWidth,captures=6;
  constexpr std::array<SidebarTab,captures> tabs{SidebarTab::Base,SidebarTab::Defense,SidebarTab::Infantry,
    SidebarTab::Infantry,SidebarTab::Vehicles,SidebarTab::Vehicles};
  for(const auto skin:kAllBuildFactions) {
    DemoVisualStyle style;style.faction=skin;
    auto assets=loadSidebarAssetsForStyle(paths,style,cameoPalette);
    updateSidebarAvailability(assets,definitions);
    try {
      const int panelY=assets.credits.height+assets.top.height+assets.radar.height+assets.side1.height;
      const int visibleRows=computeSidebarVisibleRows(assets,height);
      for(int panel=0;panel<4;++panel) {
        SidebarState state;state.selectedTab=static_cast<SidebarTab>(panel);state.tabVisible.fill(true);
        const auto& icons=assets.panelIcons[panel];
        if(icons.size()<2 || icons[0].faction!=BuildFaction::Allied || icons[1].faction!=BuildFaction::Soviet)
          throw std::runtime_error("Mixed sidebar lost a faction in panel "+std::to_string(panel));
        const int maxRows=std::max(0,int((icons.size()+1)/2)-visibleRows);
        for(std::size_t index=0;index<icons.size();++index) {
          const int scroll=std::min(int(index/2),maxRows);state.scrollRows[panel]=scroll;
          const float x=width-sidebarWidth+(index%2?115.f:52.f),y=panelY+(int(index/2)-scroll)*50.f+24;
          const auto click=handleSidebarLeftClick(state,assets,width,height,x,y);
          if(click.action!=SidebarClickAction::ClickIcon || click.iconId!=icons[index].id || click.faction!=icons[index].faction || click.tab!=state.selectedTab)
            throw std::runtime_error("Mixed sidebar click/scroll lost icon identity: "+icons[index].id);
          if(panel>=2) {
            const auto* unit=definitions.sidebarUnit(click.iconId,click.faction==BuildFaction::Allied?"ALLIED":"SOVIET");
            if(unit && unit->support()!=ra2::SupportStatus::Unsupported &&
               std::none_of(icons[index].texture.rgba.begin(),icons[index].texture.rgba.end(),[](auto c){return c!=0;}))
              throw std::runtime_error("Supported mixed sidebar unit has a blank cameo: "+unit->id);
            if(click.iconId=="engnicon" && (!unit || unit->id!=(click.faction==BuildFaction::Allied?"ENGINEER":"SENGINEER")))
              throw std::runtime_error("Mixed sidebar confused the two engineers");
            if(click.iconId=="mcvicon" && (!unit || unit->id!=(click.faction==BuildFaction::Allied?"AMCV":"SMCV")))
              throw std::runtime_error("Mixed sidebar confused the two MCVs");
          }
        }
      }
      std::vector<std::uint8_t> atlas(sidebarWidth*captures*height*4);
      std::vector<std::uint8_t> previousPixels;
      for(int column=0;column<captures;++column) {
        SidebarState state;state.selectedTab=tabs[column];state.tabVisible.fill(true);
        state.powerProduced=200;state.powerConsumed=80;state.credits=10000;
        updateSidebarState(state,0);
        if(column>=1) {
          state.powerProduced=400;state.powerConsumed=180;state.credits=9500;
          updateSidebarState(state,1000);updateSidebarState(state,column==1?1240:1720);
        }
        if(column>=3) {
          state.powerConsumed=470;state.credits=0;
          updateSidebarState(state,2000);updateSidebarState(state,column==3?2240:2720);
        }
        if(column==5) {
          state.powerOutage=true;state.credits=2147483647;
          updateSidebarState(state,3000);updateSidebarState(state,3240);
        }
        const auto panel=static_cast<std::size_t>(state.selectedTab);
        if(column==3 || column==5)state.scrollRows[panel]=std::max(0,int((assets.panelIcons[panel].size()+1)/2)-visibleRows);
        beginImGuiDebugPanelFrame();
        renderer.beginFrame(.1f,.1f,.1f,1);renderer.beginUiPass();drawSidebar(renderer,width,height,assets,state);
        renderImGuiDebugPanel();
        std::vector<std::uint8_t> pixels(sidebarWidth*height*4);
        glReadPixels(width-sidebarWidth,0,sidebarWidth,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        if(column>0 && column<5) {
          bool changed=false;
          for(int y=height-(panelY+visibleRows*50);y<height-panelY;++y)for(int x=1;x<17;++x)
            for(int c=0;c<3;++c)changed|=pixels[(y*sidebarWidth+x)*4+c]!=previousPixels[(y*sidebarWidth+x)*4+c];
          if(!changed)throw std::runtime_error("Power meter did not render its production/load transition");
        }
        if(column==1 && std::equal(pixels.end()-sidebarWidth*assets.credits.height*4,pixels.end(),
                                  previousPixels.end()-sidebarWidth*assets.credits.height*4))
          throw std::runtime_error("Sidebar balance text failed to update");
        previousPixels=pixels;
        for(int y=0;y<height;++y)std::copy_n(pixels.data()+(height-1-y)*sidebarWidth*4,sidebarWidth*4,
          atlas.data()+(y*sidebarWidth*captures+column*sidebarWidth)*4);
      }
      if(!outputDirectory.empty()) {
        auto* surface=SDL_CreateRGBSurfaceWithFormatFrom(atlas.data(),sidebarWidth*captures,height,32,sidebarWidth*captures*4,SDL_PIXELFORMAT_RGBA32);
        if(!surface)throw std::runtime_error(SDL_GetError());
        const auto file=outputDirectory/(skin==BuildFaction::Allied?"sidebar-allied.bmp":"sidebar-soviet.bmp");
        const int saved=SDL_SaveBMP(surface,file.string().c_str());SDL_FreeSurface(surface);
        if(saved!=0)throw std::runtime_error(SDL_GetError());
      }
      for(const auto size:{kDisplayResolutionOptions[0],kDisplayResolutionOptions[4]}) {
        SDL_SetWindowSize(window,size.width,size.height);renderer.setViewport(size.width,size.height);
        SidebarState state;state.tabVisible.fill(true);state.powerProduced=0;state.powerConsumed=800;state.credits=0;
        updateSidebarState(state,0);
        std::vector<std::uint8_t> before(sidebarWidth*size.height*4),after(before.size());
        for(int pass=0;pass<2;++pass) {
          beginImGuiDebugPanelFrame();renderer.beginFrame(.1f,.1f,.1f,1);renderer.beginUiPass();
          drawSidebar(renderer,size.width,size.height,assets,state);
          // An off-screen/edge power plant must never paint over the build menu.
          if(pass)drawPowerPlantReadout(state,float(size.width-sidebarWidth+5),100,size.width,size.height);
          renderImGuiDebugPanel();
          glReadPixels(size.width-sidebarWidth,0,sidebarWidth,size.height,GL_RGBA,GL_UNSIGNED_BYTE,(pass?after:before).data());
        }
        if(before!=after)throw std::runtime_error("Power-plant readout leaked into the sidebar after resizing");
      }
      SDL_SetWindowSize(window,width,height);renderer.setViewport(width,height);
    } catch(...) {SDL_SetWindowSize(window,width,height);renderer.setViewport(width,height);destroySidebarAssets(assets);throw;}
    destroySidebarAssets(assets);
  }
  if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Mixed sidebar rendering reported an OpenGL error");
  std::cout<<"Mixed sidebar passed: both skins, all panels, scrolled clicks, original cameos, distinct engineers/MCVs, credits and animated production/load meter\n";
}

void verifyDefenseRendering(Renderer2D& renderer,SDL_Window* window,const DemoAppPaths& paths,
    const ra2::DefinitionRegistry& definitions,const ArtIni& art,const PaletteLibrary& palettes,
    const HouseColorSet& colors,const VplFile& lighting,BuildingAssetCache& buildings,
    VehicleRendererCache& turrets,VplBoxRenderer& grandCannon,const WeaponVisualAssets& weapons,int width,int height,
    const std::filesystem::path& output) {
  constexpr int size=256,columns=4;const auto icons=makeSidebarPanel(SidebarTab::Defense);
  if(icons.size()!=16)throw std::runtime_error("Defense catalog must contain 16 buildings");
  const auto read=[&] {std::vector<std::uint8_t> pixels(size*size*4);glReadPixels(0,height-size,size,size,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());return pixels;};
  const auto save=[&](const std::vector<std::uint8_t>& pixels,int w,int h,const std::string& name) {
    if(output.empty())return;
    auto* surface=SDL_CreateRGBSurfaceWithFormatFrom(const_cast<std::uint8_t*>(pixels.data()),w,h,32,w*4,SDL_PIXELFORMAT_RGBA32);
    if(!surface)throw std::runtime_error(SDL_GetError());
    const int result=SDL_SaveBMP(surface,(output/name).string().c_str());SDL_FreeSurface(surface);
    if(result!=0)throw std::runtime_error(SDL_GetError());
  };
  for(int theater=0;theater<3;++theater) {
    DemoVisualStyle style;style.theater=static_cast<TheaterStyle>(theater);
    const auto& palette=palettesForTheater(palettes,style.theater);
    renderer.setIndexedPalettes(palette.unit,palette.terrain);renderer.setRemapColor(activeHouseColorValue(style,colors));
    setVehicleRendererCachePalette(turrets,palette.unit);grandCannon.setPalette(palette.unit);
    std::array<std::vector<std::uint8_t>,3> atlases;
    for(auto& atlas:atlases)atlas.resize(size*size*columns*4*4);
    for(std::size_t index=0;index<icons.size();++index) {
      const auto id=buildingIdForIcon(icons[index].id,icons[index].faction);
      if(!id)throw std::runtime_error("Missing defense mapping: "+icons[index].id);
      const auto& type=definitions.unit(*id);
      const auto& asset=ensureCachedAssetForStyle(buildings,paths.buildingRoot,art,palettes,colors,style,*id);
      if(type.wall) {
        if(!asset.buildupTextures.empty() || asset.completeLayers.front().textures.size()!=48)
          throw std::runtime_error("Wall body/shadow frames were mistaken for a buildup: "+*id);
        const auto expected=std::string(1,type.id[0]=='G'?'g':'n')+std::array{"twall","awall","uwall"}[theater];
        if(asset.completeLayers.front().id!=expected)throw std::runtime_error("Wall selected the wrong theater overlay: "+*id);
      }else if(asset.buildupTextures.empty())throw std::runtime_error("Missing original buildup: "+*id);
      if(type.fireDelay && std::count_if(asset.completeLayers.begin(),asset.completeLayers.end(),[](const auto& layer){return layer.role==BuildingAsset::LayerRole::Fire;})!=1)
        throw std::runtime_error("Missing defense charging layer: "+*id);
      if(!definitions.rules().string(*id,"SuperWeapon").empty() && asset.completeLayers.size()<2)
        throw std::runtime_error("Missing superweapon building idle body: "+*id);
      if(*id=="NAMISL" && asset.completeLayers.back().sourceFrames!=std::vector<std::size_t>{0})
        throw std::runtime_error("Silo idle animation includes damaged frames");
      BuildingInstance instance;instance.assetId=*id;instance.placement={{0,0},asset.foundation.width,asset.foundation.height};
      instance.hp=instance.maxHp=type.strength;instance.stateDurationMs=3000;instance.visualFrame=28;
      instance.firingStartedFrame=1;instance.wallConnections=15;
      const Vec2 origin{128,140};const auto depth=depthFromLogicalCenter({0,0},60,30);
      for(int stage=0;stage<3;++stage) {
        instance.state=stage==2 || type.wall?BuildingState::Complete:BuildingState::Constructing;
        const auto time=stage==0?400U:stage==1?1500U:4000U;
        renderer.beginFrame(.16f,.18f,.2f,1);renderer.beginWorldPass();
        drawBuildingInstance(renderer,asset,instance,origin,60,30,time,depth);
        const auto base=read();
        if(stage==2 && !type.buildingTurret.empty()) {
          auto& model=type.id=="GTGCAN"?grandCannon:buildingTurretRenderer(type,turrets,window,paths,lighting,palette.unit);
          const auto anchor=buildingTurretAnchor(instance,definitions.rules(),origin,60,30);
          std::set<std::vector<std::uint8_t>> views;
          for(int facing=0;facing<8;++facing) {
            renderer.beginFrame(.16f,.18f,.2f,1);renderer.beginWorldPass();
            drawBuildingInstance(renderer,asset,instance,origin,60,30,time,depth);
            instance.turretHeadingRadians=facing*kPi/4;
            ImGuiDebugPanelState debug;debug.style=style;
            model.renderInWorld(buildingTurretRenderState(colors,debug,instance),width,height,anchor.x,anchor.y,
              buildingTurretDepth(instance.placement,60,30),0);
            auto pixels=read();int changed=0;for(std::size_t p=0;p<pixels.size();p+=4)if(pixels[p]!=base[p] || pixels[p+1]!=base[p+1] || pixels[p+2]!=base[p+2])++changed;
            if(changed<20)throw std::runtime_error("Defense voxel turret is invisible: "+*id);
            views.insert(std::move(pixels));
          }
          if(views.size()<4)throw std::runtime_error("Defense voxel turret does not rotate: "+*id);
        }
        const auto pixels=read();int body=0;
        for(std::size_t p=0;p<pixels.size();p+=4)if(std::abs(int(pixels[p])-41)+std::abs(int(pixels[p+1])-46)+std::abs(int(pixels[p+2])-51)>20)++body;
        if(body<10)throw std::runtime_error("Blank defense building stage: "+*id);
        for(int p=0;p<size;++p)for(const int edge:{p,p*size,p*size+size-1,(size-1)*size+p})
          if(std::abs(int(pixels[edge*4])-41)+std::abs(int(pixels[edge*4+1])-46)+std::abs(int(pixels[edge*4+2])-51)>20)
            throw std::runtime_error("Defense verification capture clips the sprite: "+*id);
        for(int y=0;y<size;++y)std::copy_n(pixels.data()+(size-1-y)*size*4,size*4,
          atlases[stage].data()+((int(index)/columns*size+y)*columns*size+int(index)%columns*size)*4);
      }
      if(type.fireDelay)for(const auto& layer:asset.completeLayers)if(layer.role==BuildingAsset::LayerRole::Fire) {
        const auto renderOptionalLayers=[&] {
          renderer.beginFrame(.16f,.18f,.2f,1);renderer.beginWorldPass();
          drawBuildingInstance(renderer,asset,instance,origin,60,30,4000,depth);
          RhinoUnitState nearby;nearby.tilePosition={-1,0};nearby.occupiedCell={-1,0};
          const auto& cached=cachedAssetsForStyle(buildings,paths.buildingRoot,art,palettes,colors,style);
          redrawBuildingOccludersForRhino(renderer,cached,{instance},nearby,origin,60,30,width,height,1,4000);
        };
        instance.firingStartedFrame=0;if(buildingLayerTexture(layer,instance,0))throw std::runtime_error("Idle defense shows a charging animation");
        // Exercise the actual drawing path with an absent optional Fire layer.
        // A texture lookup alone does not detect a renderer dereferencing it.
        for(const auto state:{BuildingState::Preview,BuildingState::Complete}) {
          instance.state=state;renderOptionalLayers();
        }
        instance.firingStartedFrame=1;instance.visualFrame=1;if(!buildingLayerTexture(layer,instance,0))throw std::runtime_error("Defense charge animation did not start");
        instance.visualFrame=40;if(buildingLayerTexture(layer,instance,0))throw std::runtime_error("Defense charge animation loops after firing");
        renderOptionalLayers();
        instance.powered=false;instance.visualFrame=1;
        renderOptionalLayers();
      }
    }
    for(int stage=0;stage<3;++stage)save(atlases[stage],size*columns,size*4,"defense-"+std::to_string(theater)+"-"+std::to_string(stage)+".bmp");
  }
  const auto palette=Palette::load(paths.paletteRoot/"ui"/"palette.pal");
  const ra2::BeamSnapshot bolt{{0,0},{4000,0},300,0,1,17,0,"COILBOLT"};
  const auto boltImage=[&](ra2::Tick frame) {renderer.beginFrame(0,0,0,1);ra2::drawElectricBolts(renderer,std::span(&bolt,1),definitions,frame,{25,70},60,30,palette);return read();};
  const auto first=boltImage(1),second=boltImage(2),expired=boltImage(18);
  if(first==second || first!=boltImage(1))throw std::runtime_error("Electric bolt must flicker deterministically by game frame");
  int lit=0,expiredLit=0;for(std::size_t p=0;p<first.size();p+=4){lit+=first[p] || first[p+1] || first[p+2];expiredLit+=expired[p] || expired[p+1] || expired[p+2];}
  if(lit<100 || expiredLit)throw std::runtime_error("Electric bolt rendering/lifetime failed");
  std::vector<std::uint8_t> flipped(first.size());for(int y=0;y<size;++y)std::copy_n(first.data()+(size-1-y)*size*4,size*4,flipped.data()+y*size*4);
  save(flipped,size,size,"tesla-bolt.bmp");
  for(const auto name:{"TSTIMPCT","GCMUZZLE"}) {
    const auto found=weapons.animations.find(name);
    if(found==weapons.animations.end() || found->second.empty())throw std::runtime_error(std::string("Missing original defense effect: ")+name);
    CombatEffectInstance effect;effect.animation=name;effect.animationFrame=std::min<std::size_t>(3,found->second.size()-1);
    renderer.beginFrame(0,0,0,1);renderer.beginWorldPass();
    drawWeaponProjectilesAndEffects(renderer,weapons,{},std::vector{effect},{128,128},60,30);
    const auto pixels=read();int effectPixels=0;for(std::size_t p=0;p<pixels.size();p+=4)effectPixels+=pixels[p] || pixels[p+1] || pixels[p+2];
    if(effectPixels<10)throw std::runtime_error(std::string("Blank defense firing effect: ")+name);
  }
  if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Defense rendering reported an OpenGL error");
  std::cout<<"Defense GPU passed: all 16 buildings, three theaters, construction, four rotating voxel turrets, charging and electric bolts\n";
}

} // namespace

void verifyDogDroneRendering(Renderer2D& renderer,const ra2::InfantryVisualAssets& assets,
    const PaletteLibrary& palettes,int height,const std::filesystem::path& output) {
  constexpr int size=128;std::vector<std::uint8_t> sheet(size*8*size*9*4);
  for(int theater=0;theater<3;++theater) {
    const auto& palette=palettesForTheater(palettes,static_cast<TheaterStyle>(theater));renderer.setIndexedPalettes(palette.unit,palette.terrain);
    for(int type=0;type<3;++type)for(int state=0;state<3;++state)for(int facing=0;facing<8;++facing) {
      ra2::InfantryVisual unit;unit.type=std::array{"DOG","ADOG","DRON"}[type];unit.hp=unit.maxHp=100;unit.vehicleSprite=type==2;
      unit.leaping=state==2;unit.airborne=unit.leaping;unit.heightLeptons=unit.leaping?48:0;
      unit.frame=unit.leaping?facing*4:state==0?(type==2?48:0)+facing:(type==2?0:8)+facing*6;
      renderer.beginFrame(.18f,.18f,.18f,1);ra2::drawInfantry(renderer,assets,std::span(&unit,1),{64,70},60,30,{32,96,224,255},{224,32,32,255});
      std::vector<std::uint8_t> frame(size*size*4);glReadPixels(0,height-size,size,size,GL_RGBA,GL_UNSIGNED_BYTE,frame.data());
      int visible=0;for(std::size_t i=0;i<frame.size();i+=4)visible+=frame[i]!=46 || frame[i+1]!=46 || frame[i+2]!=46;
      if(visible<12)throw std::runtime_error("Native dog/drone frame rendered empty");
      if(theater==0)for(int y=0;y<size;++y)std::copy_n(frame.data()+(size-1-y)*size*4,size*4,sheet.data()+(((type*3+state)*size+y)*size*8+facing*size)*4);
    }
  }
  if(!assets.atlases.at("DRON").shadow.texture || !assets.atlases.at("DRON@JUMP").shadow.texture || assets.atlases.at("DOG@JUMP").frames!=32)
    throw std::runtime_error("Native sprite shadow/facing layout lost");
  if(!output.empty()) {
    auto* surface=SDL_CreateRGBSurfaceWithFormatFrom(sheet.data(),1024,1152,32,4096,SDL_PIXELFORMAT_RGBA32);
    if(!surface)throw std::runtime_error(SDL_GetError());const int result=SDL_SaveBMP(surface,(output/"native-dog-drone.bmp").string().c_str());SDL_FreeSurface(surface);
    if(result)throw std::runtime_error(SDL_GetError());
  }
  renderer.beginWorldPass();if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Dog/drone GPU error");
}
void verifyRadiationV3Rendering(Renderer2D& renderer,SDL_Window* window,const DemoAppPaths& paths,
    const ra2::DefinitionRegistry& definitions,const PaletteLibrary& palettes,const HouseColorSet& colors,
    const VplFile& lighting,VehicleRendererCache& cache,const WeaponVisualAssets& weapons,int height,const std::filesystem::path& output) {
  const auto save=[&](const std::vector<std::uint8_t>& pixels,int w,int h,const char* name) {
    if(output.empty())return;
    auto* surface=SDL_CreateRGBSurfaceWithFormatFrom(const_cast<std::uint8_t*>(pixels.data()),w,h,32,w*4,SDL_PIXELFORMAT_RGBA32);
    if(!surface)throw std::runtime_error(SDL_GetError());const int result=SDL_SaveBMP(surface,(output/name).string().c_str());SDL_FreeSurface(surface);
    if(result)throw std::runtime_error(SDL_GetError());
  };
  constexpr int size=128;std::vector<std::uint8_t> atlas(size*8*size*9*4);
  for(int theater=0;theater<3;++theater)for(int variant=0;variant<3;++variant) {
    const auto& type=definitions.unit(variant==2?"V3ROCKET":"V3");const auto& palette=palettesForTheater(palettes,static_cast<TheaterStyle>(theater));
    auto& model=vehicleRendererForType(type,cache,window,paths,lighting,palette.unit,variant==1?1:0);model.setPalette(palette.unit);
    for(int facing=0;facing<8;++facing) {
      auto state=buildRhinoTankRenderState(colors,ImGuiDebugPanelState{},float(facing)*kPi/4);
      state.remapEnabled=definitions.art().boolean(type.image,"Remapable",true);
      if(variant==2)applyRhinoSurfaceTilt(state,45,0);
      auto frame=model.renderToTexture(state,size,size);int pixels=0;for(std::size_t i=3;i<frame.rgba.size();i+=4)pixels+=frame.rgba[i]>0;
      if(pixels<20)throw std::runtime_error("V3 native voxel rendered empty");
      if(variant==2) {
        state.remapColor={0,255,255,255};auto alternate=model.renderToTexture(state,size,size);
        if(frame.rgba!=alternate.rgba)throw std::runtime_error("V3ROCKET Remapable=no is ignored");glDeleteTextures(1,&alternate.texture);
      }
      for(int y=0;y<size;++y)std::copy_n(frame.rgba.data()+y*size*4,size*4,atlas.data()+(((theater*3+variant)*size+y)*size*8+facing*size)*4);
      glDeleteTextures(1,&frame.texture);
    }
  }
  // Loaded and empty launchers must be distinct original models in every facing.
  for(int facing=0;facing<8;++facing) {
    bool different=false;for(int y=0;y<size;++y)for(int x=0;x<size*4;++x)
      different|=atlas[(y*size*8+facing*size)*4+x]!=atlas[((y+size)*size*8+facing*size)*4+x];
    if(!different)throw std::runtime_error("V3 loaded/empty body did not change");
  }
  save(atlas,1024,1152,"v3-native-models.bmp");
  constexpr int area=256;const auto read=[&]{std::vector<std::uint8_t> p(area*area*4);glReadPixels(0,height-area,area,area,GL_RGBA,GL_UNSIGNED_BYTE,p.data());return p;};
  ra2::World test(definitions);const auto actor=test.spawn("DESO",{});test.enqueue({1,0,1,ra2::CommandKind::Deploy,actor});for(int i=0;i<30;++i)test.step();
  renderer.beginFrame(.16f,.16f,.16f,1);ra2::drawRadiationField(renderer,test,definitions,{128,128},60,30);const auto field=read();
  int green=0;for(std::size_t i=0;i<field.size();i+=4)green+=field[i+1]>field[i]+20 && field[i+1]>field[i+2]+20;
  if(green<1000)throw std::runtime_error("Radiation field lighting is missing");
  std::vector<std::uint8_t> flipped(field.size());for(int y=0;y<area;++y)std::copy_n(field.data()+(area-1-y)*area*4,area*4,flipped.data()+y*area*4);save(flipped,area,area,"radiation-field.bmp");
  test.enqueue({test.tick()+1,0,2,ra2::CommandKind::Deploy,actor});for(int i=0;i<650;++i)test.step();
  renderer.beginFrame(.16f,.16f,.16f,1);ra2::drawRadiationField(renderer,test,definitions,{128,128},60,30);const auto gone=read();
  for(std::size_t i=0;i<gone.size();i+=4)if(gone[i]!=gone[i+1] || gone[i]!=gone[i+2])throw std::runtime_error("Expired radiation kept tinting terrain");
  const ra2::BeamSnapshot beam{{0,0},{4000,0},100,0,1,15,0,"RADBEAMWEAPON"};
  renderer.beginFrame(0,0,0,1);ra2::drawRadiationBeams(renderer,std::span(&beam,1),definitions,1,{30,100},60,30);const auto ray=read();
  int lit=0;for(std::size_t i=0;i<ray.size();i+=4)lit+=ray[i+1]>20;if(lit<40)throw std::runtime_error("Radiation beam missing");
  renderer.beginFrame(0,0,0,1);ra2::drawRadiationBeams(renderer,std::span(&beam,1),definitions,16,{30,100},60,30);const auto expired=read();
  for(std::size_t i=0;i<expired.size();i+=4)if(expired[i] || expired[i+1] || expired[i+2])throw std::runtime_error("Expired radiation beam remains visible");
  for(const auto name:{"NUKEDIE","V3TAKOFF","V3TRAIL","TWLT070"})if(!weapons.animations.contains(name) || weapons.animations.at(name).empty())throw std::runtime_error(std::string("Native effect missing: ")+name);
  if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Radiation/V3 pixel regression GL error");
  std::cout<<"Radiation/V3 pixels passed: eight facings, loaded/empty launcher, rocket palette, three theaters, terrain light/decay and beam lifetime\n";
}

void verifyDemolitionRendering(Renderer2D& renderer,SDL_Window* window,const DemoAppPaths& paths,
    const ra2::DefinitionRegistry& definitions,const PaletteLibrary& palettes,const HouseColorSet& colors,
    const VplFile& lighting,VehicleRendererCache& cache,const ra2::InfantryVisualAssets& infantry,
    const WeaponVisualAssets& weapons,int height,const std::filesystem::path& output) {
  const auto save=[&](const auto& pixels,int w,int h,const char* name) {
    if(output.empty())return;
    auto* surface=SDL_CreateRGBSurfaceWithFormatFrom(const_cast<std::uint8_t*>(pixels.data()),w,h,32,w*4,SDL_PIXELFORMAT_RGBA32);
    if(!surface)throw std::runtime_error(SDL_GetError());const int result=SDL_SaveBMP(surface,(output/name).string().c_str());SDL_FreeSurface(surface);
    if(result)throw std::runtime_error(SDL_GetError());
  };
  constexpr int size=128;std::vector<std::uint8_t> atlas(size*8*size*9*4);
  for(int theater=0;theater<3;++theater) {
    const auto& palette=palettesForTheater(palettes,static_cast<TheaterStyle>(theater));renderer.setIndexedPalettes(palette.unit,palette.terrain);
    const auto& type=definitions.unit("DTRUCK");auto& model=vehicleRendererForType(type,cache,window,paths,lighting,palette.unit);model.setPalette(palette.unit);
    for(int state=0;state<3;++state)for(int facing=0;facing<8;++facing) {
      std::vector<std::uint8_t> pixels;
      if(state<2) {
        ra2::InfantryVisual foot;foot.type="TERROR";foot.frame=state?8+facing*6+2:facing;foot.hp=foot.maxHp=75;foot.selected=true;
        renderer.beginFrame(.18f,.18f,.18f,1);ra2::drawInfantry(renderer,infantry,std::span(&foot,1),{64,75},60,30,{224,32,32,255},{32,96,224,255});
        ra2::drawInfantrySelection(renderer,infantry,std::span(&foot,1),{64,75},60,30);
        std::vector<std::uint8_t> raw(size*size*4);glReadPixels(0,height-size,size,size,GL_RGBA,GL_UNSIGNED_BYTE,raw.data());pixels.resize(raw.size());
        for(int y=0;y<size;++y)std::copy_n(raw.data()+(size-1-y)*size*4,size*4,pixels.data()+y*size*4);
        int visible=0;for(std::size_t p=0;p<pixels.size();p+=4)visible+=pixels[p]!=46 || pixels[p+1]!=46 || pixels[p+2]!=46;
        if(visible<20)throw std::runtime_error("Original terrorist frame rendered empty");
      } else {
        auto pose=buildRhinoTankRenderState(colors,ImGuiDebugPanelState{},float(facing)*kPi/4);const auto frame=model.renderToTexture(pose,size,size);pixels=frame.rgba;
        int visible=0;for(std::size_t p=3;p<pixels.size();p+=4)visible+=pixels[p]>0;
        glDeleteTextures(1,&frame.texture);if(visible<20)throw std::runtime_error("Original demolition truck voxel rendered empty");
      }
      for(int y=0;y<size;++y)std::copy_n(pixels.data()+y*size*4,size*4,atlas.data()+(((theater*3+state)*size+y)*size*8+facing*size)*4);
    }
  }
  // All 188 native TRST shadow entries are empty; preserve their absence.
  if(infantry.atlases.at("TERROR").frames!=188 || infantry.atlases.at("TERROR").shadow.texture)throw std::runtime_error("Terrorist body/shadow layout wrong");
  save(atlas,1024,1152,"demolition-native-units.bmp");
  constexpr int area=256;std::vector<std::uint8_t> explosions(area*6*area*2*4);
  for(int type=0;type<2;++type) {
    const auto name=type?"DEMTEXP":"MININUKE";const auto& frames=weapons.animations.at(name);
    if(frames.size()!=std::size_t(type?25:39))throw std::runtime_error("Missing original demolition explosion frames");
    for(int frame=0;frame<6;++frame) {
      CombatEffectInstance effect;effect.animation=name;effect.animationFrame=frame*4;
      renderer.beginFrame(0,0,0,1);renderer.beginWorldPass();drawWeaponProjectilesAndEffects(renderer,weapons,{},std::vector{effect},{128,150},60,30);
      std::vector<std::uint8_t> pixels(area*area*4);glReadPixels(0,height-area,area,area,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
      int visible=0;for(std::size_t p=0;p<pixels.size();p+=4)visible+=pixels[p] || pixels[p+1] || pixels[p+2];
      if(visible<20)throw std::runtime_error("Original demolition explosion rendered empty");
      for(int y=0;y<area;++y)std::copy_n(pixels.data()+(area-1-y)*area*4,area*4,explosions.data()+(((type*area+y)*6+frame)*area)*4);
    }
  }
  save(explosions,1536,512,"demolition-native-explosions.bmp");
  renderer.beginWorldPass();if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Demolition GPU error");
  std::cout<<"Demolition pixels passed: original infantry/shadows/health, truck voxels, eight facings, three theaters and native explosions\n";
}

void verifyIvanApocalypseRendering(Renderer2D& renderer,SDL_Window* window,const DemoAppPaths& paths,
    const ra2::DefinitionRegistry& definitions,const PaletteLibrary& palettes,const HouseColorSet& colors,
    const VplFile& lighting,VehicleRendererCache& cache,const ra2::InfantryVisualAssets& infantry,
    const WeaponVisualAssets& weapons,int height,const std::filesystem::path& output) {
  const auto save=[&](const auto& pixels,int w,int h,const char* name) {
    if(output.empty())return;
    auto* surface=SDL_CreateRGBSurfaceWithFormatFrom(const_cast<std::uint8_t*>(pixels.data()),w,h,32,w*4,SDL_PIXELFORMAT_RGBA32);
    if(!surface)throw std::runtime_error(SDL_GetError());const int result=SDL_SaveBMP(surface,(output/name).string().c_str());SDL_FreeSurface(surface);if(result)throw std::runtime_error(SDL_GetError());
  };
  const auto read=[&](int width,int rows) {std::vector<std::uint8_t> raw(width*rows*4),pixels(raw.size());glReadPixels(0,height-rows,width,rows,GL_RGBA,GL_UNSIGNED_BYTE,raw.data());for(int y=0;y<rows;++y)std::copy_n(raw.data()+(rows-1-y)*width*4,width*4,pixels.data()+y*width*4);return pixels;};
  constexpr int size=128;std::vector<std::uint8_t> atlas(size*8*size*12*4);
  for(int theater=0;theater<3;++theater) {
    const auto& palette=palettesForTheater(palettes,static_cast<TheaterStyle>(theater));renderer.setIndexedPalettes(palette.unit,palette.terrain);
    auto& model=vehicleRendererForType(definitions.unit("APOC"),cache,window,paths,lighting,palette.unit);model.setPalette(palette.unit);
    for(int state=0;state<4;++state)for(int facing=0;facing<8;++facing) {
      std::vector<std::uint8_t> pixels;
      if(state<3) {
        ra2::InfantryVisual foot;foot.type="IVAN";foot.frame=state==0?facing:state==1?8+facing*6+2:164+facing*6+3;foot.hp=foot.maxHp=125;foot.selected=true;
        renderer.beginFrame(.18f,.18f,.18f,1);ra2::drawInfantry(renderer,infantry,std::span(&foot,1),{64,75},60,30,{224,32,32,255},{32,96,224,255});ra2::drawInfantrySelection(renderer,infantry,std::span(&foot,1),{64,75},60,30);pixels=read(size,size);
        int visible=0;for(std::size_t p=0;p<pixels.size();p+=4)visible+=pixels[p]!=46 || pixels[p+1]!=46 || pixels[p+2]!=46;if(visible<20)throw std::runtime_error("Original Ivan pose rendered empty");
      } else {
        auto pose=buildRhinoTankRenderState(colors,ImGuiDebugPanelState{},float(facing)*kPi/4);const auto frame=model.renderToTexture(pose,size,size);pixels=frame.rgba;
        int visible=0;for(std::size_t p=3;p<pixels.size();p+=4)visible+=pixels[p]>0;glDeleteTextures(1,&frame.texture);if(visible<20)throw std::runtime_error("Original Apocalypse voxel rendered empty");
      }
      for(int y=0;y<size;++y)std::copy_n(pixels.data()+y*size*4,size*4,atlas.data()+(((theater*4+state)*size+y)*size*8+facing*size)*4);
    }
  }
  if(infantry.atlases.at("IVAN").frames!=267 || infantry.bombIcons.size()!=13)throw std::runtime_error("Native Ivan/bomb layout lost");save(atlas,1024,1536,"ivan-apocalypse-native-units.bmp");
  renderer.beginFrame(.18f,.18f,.18f,1);renderer.beginUiPass();
  for(int frame=0;frame<13;++frame) {
    const ra2::BombVisual icon{0,{float(frame)*64/60,-float(frame)*64/60},0,frame};ra2::drawIvanBombIcons(renderer,infantry,std::span(&icon,1),{32,90},60,30);
  }
  const auto icons=read(832,128);save(icons,832,128,"ivan-native-bomb-icons.bmp");
  ra2::World test(definitions);const auto source=test.spawn("IVAN",{}),target=test.spawn("HTNK",{1,0},1);test.enqueue({1,0,1,ra2::CommandKind::AttackEntity,source,target});
  ra2::Tick planted=0,due=0;for(int i=0;i<150 && !due;++i){test.step();for(const auto& e:test.snapshot())if(e.id==target && e.bomb){planted=e.bomb->planted;due=e.bomb->detonates;}}
  if(!due)throw std::runtime_error("Bomb timing renderer fixture failed");
  if(!ra2::makeWorldView(test,definitions,0,1).bombs.empty())throw std::runtime_error("Enemy saw bomb without a detector");
  test.spawn("ENGINEER",{1,2},1);if(ra2::makeWorldView(test,definitions,0,1).bombs.size()!=1)throw std::runtime_error("Native BombSight failed to reveal bomb");
  for(const auto offset:{0,7,8,74,75,82,449}) {
    while(test.tick()<planted+offset)test.step();const auto view=ra2::makeWorldView(test,definitions,0);
    const int expected=std::min(5,offset/75)*2+((offset/8)&1);
    if(view.bombs.size()!=1 || view.bombs.front().frame!=expected)throw std::runtime_error("Native six-stage bomb flicker timing wrong");
  }
  test.step();if(!ra2::makeWorldView(test,definitions,0).bombs.empty())throw std::runtime_error("Expired bomb icon survived detonation");
  constexpr int area=192;std::vector<std::uint8_t> effects(area*5*area*3*4);
  for(int type=0;type<3;++type) {
    const auto name=std::array{"CRIVEXP","APOCEXP","APMUZZLE"}[type];const auto& frames=weapons.animations.at(name);int litFrames=0;
    for(int sample=0;sample<5;++sample) {
      CombatEffectInstance effect;effect.animation=name;effect.animationFrame=sample*int(frames.size()-1)/4;
      renderer.beginFrame(0,0,0,1);renderer.beginWorldPass();drawWeaponProjectilesAndEffects(renderer,weapons,{},std::vector{effect},{96,140},60,30);const auto pixels=read(area,area);
      int lit=0;for(std::size_t p=0;p<pixels.size();p+=4)lit+=pixels[p] || pixels[p+1] || pixels[p+2];litFrames+=lit>10;
      for(int y=0;y<area;++y)std::copy_n(pixels.data()+y*area*4,area*4,effects.data()+(((type*area+y)*5+sample)*area)*4);
    }
    if(litFrames<3)throw std::runtime_error(std::string("Missing original animation pixels: ")+name);
  }
  save(effects,960,576,"ivan-apocalypse-native-effects.bmp");renderer.beginWorldPass();if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Ivan/Apocalypse GPU error");
  std::cout<<"Ivan/Apocalypse pixels passed: original poses/voxels, eight facings/three theaters, native bomb icons/palette/detection/flicker and explosion/muzzle frames\n";
}

void verifyCommandoRendering(Renderer2D& renderer,const ra2::DefinitionRegistry& definitions,
    const PaletteLibrary& palettes,const ra2::InfantryVisualAssets& infantry,const WeaponVisualAssets& weapons,
    int viewportHeight,const std::filesystem::path& output) {
  const auto read=[&](int w,int h){std::vector<std::uint8_t> raw(w*h*4),pixels(raw.size());glReadPixels(0,viewportHeight-h,w,h,GL_RGBA,GL_UNSIGNED_BYTE,raw.data());for(int y=0;y<h;++y)std::copy_n(raw.data()+(h-y-1)*w*4,w*4,pixels.data()+y*w*4);return pixels;};
  const auto save=[&](const auto& pixels,int w,int h,const std::string& name){if(output.empty())return;auto* surface=SDL_CreateRGBSurfaceWithFormatFrom(const_cast<std::uint8_t*>(pixels.data()),w,h,32,w*4,SDL_PIXELFORMAT_RGBA32);if(!surface)throw std::runtime_error(SDL_GetError());const auto result=SDL_SaveBMP(surface,(output/name).string().c_str());SDL_FreeSurface(surface);if(result)throw std::runtime_error(SDL_GetError());};
  constexpr int size=96,columns=8,rows=24;
  for(int theater=0;theater<3;++theater) {
    const auto& palette=palettesForTheater(palettes,static_cast<TheaterStyle>(theater));renderer.setIndexedPalettes(palette.unit,palette.terrain);
    std::vector<std::uint8_t> atlas(size*columns*size*rows*4);
    for(int kind=0;kind<4;++kind)for(int pose=0;pose<6;++pose)for(int facing=0;facing<8;++facing) {
      const auto id=std::array{"GHOST","TANY","SPY","CLEG"}[kind];const auto& art=*definitions.unit(id).infantry;
      const auto sequence=std::array{"READY","WALK","FIREUP","PRONE",kind<2?"SWIM":"READY",kind<2?"TREAD":"READY"}[pose];
      ra2::InfantryVisual foot;foot.type=id;foot.frame=art.sequence(sequence).frame(facing,pose==2?std::min(art.fireUp,art.sequence(sequence).count-1):2,true);foot.hp=foot.maxHp=definitions.unit(id).strength;foot.selected=true;foot.veteran=pose==3;
      if(kind==3 && pose==4)foot.reforming=true;if(kind==3 && pose==5)foot.frozen=true;
      renderer.beginFrame(.18f,.18f,.18f,1);ra2::drawInfantry(renderer,infantry,std::span(&foot,1),{48,58},60,30,{32,96,224,255},{224,32,32,255},theater==1);
      const auto body=read(size,size);int visible=0;for(std::size_t p=0;p<body.size();p+=4)visible+=body[p]!=46 || body[p+1]!=46 || body[p+2]!=46;
      if(visible<8)throw std::runtime_error(std::string("Empty original commando pose: ")+id+"/"+sequence);
      ra2::drawInfantrySelection(renderer,infantry,std::span(&foot,1),{48,58},60,30);const auto pixels=read(size,size);
      for(int y=0;y<size;++y)std::copy_n(pixels.data()+y*size*4,size*4,atlas.data()+(((kind*6+pose)*size+y)*size*columns+facing*size)*4);
    }
    save(atlas,768,2304,"commandos-theater-"+std::to_string(theater)+".bmp");
  }
  if(infantry.atlases.at("GHOST@ARCTIC").frames!=infantry.atlases.at("GHOST").frames)throw std::runtime_error("SEAL arctic sequence layout lost");
  constexpr int area=128;std::vector<std::uint8_t> animations(area*5*area*3*4);
  for(int kind=0;kind<3;++kind) {
    const auto name=std::array{"WARPIN","WARPOUT","WARPAWAY"}[kind];const auto& frames=weapons.animations.at(name);int litFrames=0;
    for(int sample=0;sample<5;++sample) {
      CombatEffectInstance effect;effect.animation=name;effect.animationFrame=sample*int(frames.size()-1)/4;effect.alpha=.5f;
      renderer.beginFrame(0,0,0,1);renderer.beginWorldPass();drawWeaponProjectilesAndEffects(renderer,weapons,{},std::vector{effect},{64,80},60,30);const auto pixels=read(area,area);
      int lit=0;for(std::size_t p=0;p<pixels.size();p+=4)lit+=pixels[p] || pixels[p+1] || pixels[p+2];litFrames+=lit>5;
      for(int y=0;y<area;++y)std::copy_n(pixels.data()+y*area*4,area*4,animations.data()+(((kind*area+y)*5+sample)*area)*4);
    }
    if(litFrames<2)throw std::runtime_error(std::string("Empty original chrono effect: ")+name);
  }
  save(animations,640,384,"commandos-native-warp-effects.bmp");
  const auto& palette=palettesForTheater(palettes,TheaterStyle::Temperate);renderer.setIndexedPalettes(palette.unit,palette.terrain);
  auto rules=definitions.rules();for(const auto& [id,type]:definitions.units()){(void)type;rules.overlay(ra2::IniDocument::parse("["+id+"]\nCanPassiveAquire=no\n"));}
  rules.overlay(ra2::IniDocument::parse("[NeutronRifle]\nDamage=80\n"));const auto tested=ra2::DefinitionRegistry::compile(rules,definitions.art(),definitions.replica());
  ra2::World world(tested);const auto source=world.spawn("CLEG",{}),target=world.spawn("E2",{3,0},1),spy=world.spawn("SPY",{0,3});
  std::uint64_t sequence=0;ra2::UnitSelection selection;selection.selectOne(source);
  if(ra2::issueSelectionCommand(world,tested,selection,ra2::CommandKind::AttackEntity,{},target,sequence)!=1)throw std::runtime_error("Chrono selection rejected target");
  bool frozen=false,erased=false;
  for(int frame=0;frame<90;++frame) {
    world.step();const auto view=ra2::makeWorldView(world,tested,source);
    const bool locked=std::any_of(view.infantry.begin(),view.infantry.end(),[&](auto& e){return e.id==target && e.frozen;});
    const bool alive=std::any_of(view.infantry.begin(),view.infantry.end(),[&](auto& e){return e.id==target;});
    if((locked && !frozen) || (!alive && !erased)) {
      renderer.beginFrame(.12f,.14f,.12f,1);ra2::drawInfantry(renderer,infantry,view.infantry,{100,90},60,30,{32,96,224,255},{224,32,32,255});
      ra2::drawRadiationBeams(renderer,view.beams,tested,world.tick(),{100,90},60,30);drawWeaponProjectilesAndEffects(renderer,weapons,{},view.effects,{100,90},60,30);
      const auto pixels=read(320,240);save(pixels,320,240,locked?"commandos-temporal-lock.bmp":"commandos-temporal-erase.bmp");
      if(locked){bool blue=false;for(std::size_t p=0;p<pixels.size();p+=4)blue|=pixels[p+2]>100 && pixels[p+2]>pixels[p]+30;if(!blue || view.beams.empty())throw std::runtime_error("Temporal beam is missing or not blue");frozen=true;}else erased=true;
    }
  }
  selection.selectOne(spy);const auto copy=world.spawn("E1",{30,0},1);if(ra2::issueSelectionCommand(world,tested,selection,ra2::CommandKind::AttackEntity,{},copy,sequence)!=1)throw std::runtime_error("Spy disguise click rejected");world.step();
  const auto enemy=ra2::makeWorldView(world,tested,ra2::EntityId{},1);const auto disguise=std::find_if(enemy.infantry.begin(),enemy.infantry.end(),[&](auto& e){return e.id==spy;});
  if(disguise==enemy.infantry.end() || disguise->type!="E1" || disguise->owner!=1)throw std::runtime_error("Spy disguise did not render copied infantry/remap for enemy viewer");
  if(!frozen || !erased || glGetError()!=GL_NO_ERROR)throw std::runtime_error("Commando GPU smoke failed");
  std::cout<<"Commando pixels passed: original four bodies, arctic SEAL, eight facings, six poses, three theaters, warp SHPs, blue temporal beam, frozen body and viewer-specific spy disguise\n";
}

void verifyNavalRendering(Renderer2D& renderer,SDL_Window* window,const DemoAppPaths& paths,
    const ra2::DefinitionRegistry& definitions,const PaletteLibrary& palettes,const HouseColorSet& colors,
    const VplFile& lighting,VehicleRendererCache& cache,const ra2::NavalVisualAssets& terrain,
    const WeaponVisualAssets& weapons,int height,const std::filesystem::path& output) {
  const auto save=[&](const auto& pixels,int w,int h,const std::string& name){if(output.empty())return;auto* s=SDL_CreateRGBSurfaceWithFormatFrom(const_cast<std::uint8_t*>(pixels.data()),w,h,32,w*4,SDL_PIXELFORMAT_RGBA32);if(!s)throw std::runtime_error(SDL_GetError());const auto result=SDL_SaveBMP(s,(output/name).string().c_str());SDL_FreeSurface(s);if(result)throw std::runtime_error(SDL_GetError());};
  const auto read=[&](int w,int h){std::vector<std::uint8_t> raw(w*h*4),pixels(raw.size());glReadPixels(0,height-h,w,h,GL_RGBA,GL_UNSIGNED_BYTE,raw.data());for(int y=0;y<h;++y)std::copy_n(raw.data()+(h-y-1)*w*4,w*4,pixels.data()+y*w*4);return pixels;};
  constexpr int size=192;std::vector<std::uint8_t> atlas(size*8*size*18*4);
  for(int theater=0;theater<3;++theater) {
    const auto& palette=palettesForTheater(palettes,static_cast<TheaterStyle>(theater));renderer.setIndexedPalettes(palette.unit,palette.terrain);
    for(int kind=0;kind<6;++kind)for(int facing=0;facing<8;++facing) {
      auto& model=kind==5?navalTorpedoRenderer(cache,window,paths,lighting,palette.unit):vehicleRendererForType(definitions.unit(std::array{"LCRF","SAPC","SUB","HYD","AEGIS"}[kind]),cache,window,paths,lighting,palette.unit);
      model.setPalette(palette.unit);auto state=buildRhinoTankRenderState(colors,ImGuiDebugPanelState{},facing*kPi/4);if(kind==5)state.remapEnabled=false;
      const auto frame=model.renderToTexture(state,size,size);int visible=0;for(std::size_t p=3;p<frame.rgba.size();p+=4)visible+=frame.rgba[p]>0;glDeleteTextures(1,&frame.texture);
      if(visible<(kind==5?5:60))throw std::runtime_error("Original naval voxel rendered empty");
      for(int y=0;y<size;++y)std::copy_n(frame.rgba.data()+y*size*4,size*4,atlas.data()+(((theater*6+kind)*size+y)*size*8+facing*size)*4);
    }
    ra2::World coast(definitions);ra2::initializeNavalCoast(coast,0,0,8,12);
    renderer.beginFrame(.18f,.18f,.18f,1);ra2::drawNavalTerrain(renderer,terrain,static_cast<TheaterStyle>(theater),coast.map(),{220,20},60,30,640,384);const auto pixels=read(640,384);int blue=0;for(std::size_t p=0;p<pixels.size();p+=4)blue+=pixels[p+2]>pixels[p]+20;if(blue<1000)throw std::runtime_error("Original water TMP or theater palette missing");save(pixels,640,384,"naval-coast-"+std::to_string(theater)+".bmp");
  }
  save(atlas,1536,3456,"naval-native-voxels.bmp");
  constexpr int area=160;std::vector<std::uint8_t> effects(area*5*area*4*4);
  for(int kind=0;kind<4;++kind){const auto name=std::array{"WAKE1","H2O_EXP1","H2O_EXP2","BBBLELRG"}[kind];const auto& frames=weapons.animations.at(name);int seen=0;
    for(int sample=0;sample<5;++sample){CombatEffectInstance effect;effect.animation=name;effect.animationFrame=sample*int(frames.size()-1)/4;effect.alpha=.5f;renderer.beginFrame(0,0,0,1);renderer.beginWorldPass();drawWeaponProjectilesAndEffects(renderer,weapons,{},std::vector{effect},{80,100},60,30);const auto pixels=read(area,area);int lit=0;for(std::size_t p=0;p<pixels.size();p+=4)lit+=pixels[p] || pixels[p+1] || pixels[p+2];seen+=lit>3;for(int y=0;y<area;++y)std::copy_n(pixels.data()+y*area*4,area*4,effects.data()+(((kind*area+y)*5+sample)*area)*4);}
    if(seen<2)throw std::runtime_error(std::string("Empty original naval animation: ")+name);
  }
  save(effects,800,640,"naval-native-effects.bmp");if(!weapons.projectileFrames.contains("MEDUSA") || weapons.projectileFrames.at("MEDUSA").size()!=32)throw std::runtime_error("Native Medusa facings missing");
  ra2::World detection(definitions);ra2::initializeNavalCoast(detection,0,0,12,20);const auto sub=detection.spawn("SUB",{4,4},1);if(!sub)throw std::runtime_error("Sub pixel setup failed");for(int i=0;i<30;++i)detection.step();
  const auto hidden=ra2::makeWorldView(detection,definitions,ra2::EntityId{},0),own=ra2::makeWorldView(detection,definitions,ra2::EntityId{},1);
  if(!hidden.vehicles.empty() || own.vehicles.size()!=1 || own.vehicles.front().opacity>.5f)throw std::runtime_error("Underwater visibility/owner translucency failed");
  detection.spawn("SUB",{6,4});const auto revealed=ra2::makeWorldView(detection,definitions,ra2::EntityId{},0);if(revealed.vehicles.size()!=2)throw std::runtime_error("Sonar did not reveal native sub body");
  if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Naval pixel OpenGL error");std::cout<<"Naval pixels passed: five original ships and voxel torpedo, eight facings/three theaters, native TMP water/shore palettes, wake/splash/bubbles, Medusa and underwater visibility\n";
}

void verifyNavalSpecialRendering(Renderer2D& renderer,SDL_Window* window,const DemoAppPaths& paths,
    const ra2::DefinitionRegistry& definitions,const PaletteLibrary& palettes,const HouseColorSet& colors,
    const VplFile& lighting,VehicleRendererCache& cache,const ra2::InfantryVisualAssets& infantry,
    const WeaponVisualAssets& weapons,int height,const std::filesystem::path& output) {
  const auto save=[&](const auto& pixels,int w,int h,const std::string& name){if(output.empty())return;auto* s=SDL_CreateRGBSurfaceWithFormatFrom(const_cast<std::uint8_t*>(pixels.data()),w,h,32,w*4,SDL_PIXELFORMAT_RGBA32);if(!s)throw std::runtime_error(SDL_GetError());const auto result=SDL_SaveBMP(s,(output/name).string().c_str());SDL_FreeSurface(s);if(result)throw std::runtime_error(SDL_GetError());};
  const auto read=[&](int w,int h){std::vector<std::uint8_t> raw(w*h*4),pixels(raw.size());glReadPixels(0,height-h,w,h,GL_RGBA,GL_UNSIGNED_BYTE,raw.data());for(int y=0;y<h;++y)std::copy_n(raw.data()+(h-y-1)*w*4,w*4,pixels.data()+y*w*4);return pixels;};
  constexpr int size=192;std::vector<std::uint8_t> voxels(size*8*size*24*4),creatures(size*8*size*18*4);
  for(int theater=0;theater<3;++theater) {
    const auto& palette=palettesForTheater(palettes,static_cast<TheaterStyle>(theater));renderer.setIndexedPalettes(palette.unit,palette.terrain);
    for(int kind=0;kind<8;++kind)for(int facing=0;facing<8;++facing) {
      const auto id=std::array{"DEST","DEST","CARRIER","DRED","DRED","ASW","HORNET","DMISL"}[kind];
      auto& model=vehicleRendererForType(definitions.unit(id),cache,window,paths,lighting,palette.unit,(kind==1 || kind==4)?1:0);
      model.setPalette(palette.unit);auto state=buildRhinoTankRenderState(colors,ImGuiDebugPanelState{},facing*kPi/4);state.remapEnabled=std::string_view(id)!="DMISL";
      const auto frame=model.renderToTexture(state,size,size);int visible=0;for(std::size_t p=3;p<frame.rgba.size();p+=4)visible+=frame.rgba[p]>0;glDeleteTextures(1,&frame.texture);
      if(visible<40)throw std::runtime_error("Empty original special navy voxel");
      for(int y=0;y<size;++y)std::copy_n(frame.rgba.data()+y*size*4,size*4,voxels.data()+(((theater*8+kind)*size+y)*size*8+facing*size)*4);
    }
    for(int kind=0;kind<2;++kind)for(int pose=0;pose<3;++pose)for(int facing=0;facing<8;++facing) {
      const auto id=kind?"SQD":"DLPH";const auto& type=definitions.unit(id);const auto& atlas=infantry.atlases.at(id);const auto& sequence=type.spriteVehicle->sequence(std::array{"READY","WALK","FIREUP"}[pose]);
      ra2::InfantryVisual unit;unit.id=1;unit.type=id;unit.frame=sequence.frame(facing,pose==1?8:2,true);unit.hp=unit.maxHp=type.strength;unit.vehicleSprite=unit.selected=true;
      if(unit.frame<0 || unit.frame>=atlas.frames)throw std::runtime_error("Sea creature native frame outside SHP body");
      renderer.beginFrame(0,0,0,1);ra2::drawInfantry(renderer,infantry,std::span(&unit,1),{96,110},60,30,{24,96,208,255},{208,32,24,255});ra2::drawInfantrySelection(renderer,infantry,std::span(&unit,1),{96,110},60,30);
      const auto pixels=read(size,size);int visible=0;for(std::size_t p=0;p<pixels.size();p+=4)visible+=pixels[p] || pixels[p+1] || pixels[p+2];if(visible<50)throw std::runtime_error("Empty sea creature native SHP render");
      for(int y=0;y<size;++y)std::copy_n(pixels.data()+y*size*4,size*4,creatures.data()+(((theater*6+kind*3+pose)*size+y)*size*8+facing*size)*4);
    }
  }
  save(voxels,1536,4608,"naval-special-native-voxels.bmp");save(creatures,1536,3456,"naval-special-native-creatures.bmp");
  if(infantry.atlases.at("DLPH").frames!=116 || infantry.atlases.at("SQD").frames!=296 || infantry.atlases.at("SQD").shadow.texture)throw std::runtime_error("SQD NoShadow/full frame layout lost");
  std::vector<std::uint8_t> grapple(size*8*size*4*4);
  for(int direction=0;direction<8;++direction)for(int sample=0;sample<4;++sample) {
    const auto name=std::string("SQDG_")+std::array{"N","NE","E","SE","S","SW","W","NW"}[direction];if(weapons.animations.at(name).size()!=30)throw std::runtime_error("Native grapple directional frames missing");
    if(weapons.animations.at("SQDG").size()!=240 || weapons.animations.at("SQDG").front().paletteKind!=UiTexturePaletteKind::Unit || weapons.animations.at("SQDG").front().colorMode!=UiTextureColorMode::IndexedPalette)throw std::runtime_error("Squid grapple lost native unit palette");
    CombatEffectInstance effect;effect.animation="SQDG";effect.animationFrame=direction*10+std::array{0,9,85,165}[sample];renderer.beginFrame(0,0,0,1);renderer.beginWorldPass();drawWeaponProjectilesAndEffects(renderer,weapons,{},std::vector{effect},{96,110},60,30);
    const auto pixels=read(size,size);for(int y=0;y<size;++y)std::copy_n(pixels.data()+y*size*4,size*4,grapple.data()+(((sample*size+y)*size*8+direction*size))*4);
  }
  save(grapple,1536,768,"naval-special-native-grapple.bmp");
  ra2::BeamSnapshot wave{{0,0},{4000,0},0,0,0,16,0,"SONICZAP"};renderer.beginFrame(0,0,0,1);ra2::drawSonicWaves(renderer,std::span(&wave,1),definitions,7,{100,70},60,30);const auto sonic=read(384,256);int cyan=0;for(std::size_t p=0;p<sonic.size();p+=4)cyan+=sonic[p+2]>sonic[p]+10 && sonic[p+1]>sonic[p]+5;if(cyan<30)throw std::runtime_error("Sonic wave fan missing native cyan silhouette");save(sonic,384,256,"naval-special-sonic.bmp");
  ra2::World hidden(definitions);ra2::initializeNavalCoast(hidden,0,0,12,20);hidden.spawn("DLPH",{4,3},1);hidden.spawn("SQD",{7,3},1);for(int i=0;i<80;++i)hidden.step();
  if(!ra2::makeWorldView(hidden,definitions,ra2::EntityId{},0).infantry.empty())throw std::runtime_error("Enemy submerged sea creatures visible without sonar");
  const auto own=ra2::makeWorldView(hidden,definitions,ra2::EntityId{},1);if(own.infantry.size()!=2 || own.infantry.front().opacity>.5f)throw std::runtime_error("Owned sea creature translucency missing");
  if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Special navy OpenGL error");std::cout<<"Special navy pixels passed: native bodies/empty decks/aircraft, 8 facings/3 theaters, SHP creatures/health bars, grapple directions and cyan wave\n";
}

void verifyAircraftRendering(Renderer2D& renderer,SDL_Window* window,const DemoAppPaths& paths,
  const ra2::DefinitionRegistry& definitions,const PaletteLibrary& library,const HouseColorSet& colors,const VplFile& lighting,
  VehicleRendererCache& cache,int viewportHeight,const std::filesystem::path& output) {
  constexpr int size=192;std::vector<std::uint8_t> atlas(1536*4608*4);bool rotorChanged=false;
  std::vector<std::uint8_t> rotor;
  for(int theater=0;theater<3;++theater)for(int kind=0;kind<4;++kind)for(int pose=0;pose<2;++pose)for(int facing=0;facing<8;++facing) {
    const auto id=std::array{"ORCA","BEAG","SHAD","ZEP"}[kind];const auto& palette=palettesForTheater(library,static_cast<TheaterStyle>(theater));
    auto& model=vehicleRendererForType(definitions.unit(id),cache,window,paths,lighting,palette.unit);model.setPalette(palette.unit);
    auto state=buildRhinoTankRenderState(colors,ImGuiDebugPanelState{},facing*kPi/4);state.animationFrame=pose;
    auto frame=model.renderToTexture(state,size,size);int pixels=0;for(std::size_t p=3;p<frame.rgba.size();p+=4)pixels+=frame.rgba[p]>0;
    glDeleteTextures(1,&frame.texture);if(pixels<20)throw std::runtime_error("Native aircraft voxel rendered empty");
    if(theater==0 && kind==2 && facing==0){if(!pose)rotor=frame.rgba;else rotorChanged=rotor!=frame.rgba;}
    for(int y=0;y<size;++y)std::copy_n(frame.rgba.data()+y*size*4,size*4,atlas.data()+(((((theater*4+kind)*2+pose)*size+y)*1536+facing*size)*4));
  }
  if(!rotorChanged)throw std::runtime_error("Night hawk HVA rotor animation is frozen");
  if(!output.empty()) {
    SDL_Surface* surface=SDL_CreateRGBSurfaceWithFormatFrom(atlas.data(),1536,4608,32,1536*4,SDL_PIXELFORMAT_RGBA32);
    if(!surface || SDL_SaveBMP(surface,(output/"aircraft-native-voxels.bmp").string().c_str()))throw std::runtime_error(SDL_GetError());SDL_FreeSurface(surface);
  }
  renderer.setViewport(DemoAppConfig::kViewportWidth,viewportHeight);
  std::cout<<"Aircraft GPU passed: four native voxel/HVA models, eight facings, three theaters and animated night hawk rotors\n";
}

int main(const int argc, char** argv) {
  const bool sandboxSmokeTest=argc>=2 && std::string_view(argv[1])=="--sandbox-smoke-test";
  const bool aircraftSmokeTest=argc>=2 && std::string_view(argv[1])=="--aircraft-smoke-test";
  const bool navalSpecialSmokeTest=argc>=2 && std::string_view(argv[1])=="--naval-special-smoke-test";
  const bool navalSmokeTest=argc>=2 && std::string_view(argv[1])=="--naval-smoke-test";
  const bool commandoSmokeTest=argc>=2 && std::string_view(argv[1])=="--commando-smoke-test";
  const bool simulationSmokeTest = argc >= 2 && std::string_view(argv[1]) == "--simulation-smoke-test";
  const bool gameSpeedSmokeTest = argc == 2 && std::string_view(argv[1]) == "--game-speed-smoke-test";
  const bool buildingSmokeTest = argc >= 2 && std::string_view(argv[1]) == "--building-smoke-test";
  const bool miningSmokeTest = argc >= 2 && std::string_view(argv[1]) == "--mining-smoke-test";
  const bool tankSmokeTest = argc >= 2 && std::string_view(argv[1]) == "--tank-smoke-test";
  const bool selectionSmokeTest = argc >= 2 && std::string_view(argv[1]) == "--selection-smoke-test";
  const bool supportInfantrySmokeTest=argc>=2 && std::string_view(argv[1])=="--support-infantry-smoke-test";
  const bool transportSmokeTest=argc>=2 && std::string_view(argv[1])=="--transport-smoke-test";
  const bool airDefenseSmokeTest=argc>=2 && std::string_view(argv[1])=="--air-defense-smoke-test";
  const bool specialVehicleSmokeTest=argc>=2 && std::string_view(argv[1])=="--special-vehicles-smoke-test";
  const bool sidebarSmokeTest=argc>=2 && std::string_view(argv[1])=="--sidebar-smoke-test";
  const bool defenseSmokeTest=argc>=2 && std::string_view(argv[1])=="--defense-smoke-test";
  const bool buildingActionsSmokeTest=argc>=2 && std::string_view(argv[1])=="--building-actions-smoke-test";
  const bool teslaUnitsSmokeTest=argc>=2 && std::string_view(argv[1])=="--tesla-units-smoke-test";
  const bool dogDroneSmokeTest=argc>=2 && std::string_view(argv[1])=="--dog-drone-smoke-test";
  const bool demolitionSmokeTest=argc>=2 && std::string_view(argv[1])=="--demolition-smoke-test";
  const bool ivanApocalypseSmokeTest=argc>=2 && std::string_view(argv[1])=="--ivan-apocalypse-smoke-test";
  const bool radiationV3SmokeTest=argc>=2 && std::string_view(argv[1])=="--radiation-v3-smoke-test";
  const bool infantrySmokeTest=specialVehicleSmokeTest || airDefenseSmokeTest || transportSmokeTest || supportInfantrySmokeTest || (argc>=2 && std::string_view(argv[1])=="--infantry-smoke-test");
  const bool smokeTest = sandboxSmokeTest || aircraftSmokeTest || navalSpecialSmokeTest || navalSmokeTest || commandoSmokeTest || ivanApocalypseSmokeTest || demolitionSmokeTest || dogDroneSmokeTest || radiationV3SmokeTest || teslaUnitsSmokeTest || buildingActionsSmokeTest || defenseSmokeTest || sidebarSmokeTest || infantrySmokeTest || selectionSmokeTest || tankSmokeTest || miningSmokeTest || simulationSmokeTest || gameSpeedSmokeTest || buildingSmokeTest || (argc == 2 && std::string_view(argv[1]) == "--smoke-test");
  SDL_Window* window = nullptr;
  SDL_GLContext glContext = nullptr;
  Renderer2D renderer;
  IsoGridRenderer gridRenderer;
  VplBoxRenderer rhinoRenderer;
  VplBoxRenderer grizzlyRenderer;
  VehicleRendererCache vehicleRendererCache;
  VplBoxRenderer grandCannonRenderer;
  VplBoxRenderer mcvRenderer;
  SidebarAssets sidebarAssets;
  BuildingAssetCache buildingAssetCache;
  RhinoUnitUiAssets rhinoUiAssets;
  WeaponVisualAssets weaponVisualAssets;
  ra2::ResourceVisualAssets resourceVisualAssets;
  ra2::InfantryVisualAssets infantryVisualAssets;
  ra2::SpecialVehicleAssets specialVehicleAssets;
  ra2::BuildingActionVisualAssets buildingActionAssets;
  ra2::NavalVisualAssets navalVisualAssets;

  try {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
      throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }

    configureOpenGlContext();

    ImGuiDebugPanelState debugPanelState{};
    const ViewportState initialViewport = viewportStateForPanel(debugPanelState);

    window = SDL_CreateWindow("RA2 Replica Demo - Construction Sandbox",
                              SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED,
                              initialViewport.width,
                              initialViewport.height,
                              SDL_WINDOW_OPENGL | (smokeTest ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN));
    if (window == nullptr) {
      throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }

    glContext = SDL_GL_CreateContext(window);
    if (glContext == nullptr) {
      throw std::runtime_error(std::string("SDL_GL_CreateContext failed: ") + SDL_GetError());
    }
    SDL_GL_SetSwapInterval(smokeTest?0:1);

    renderer.initialize(initialViewport.width, initialViewport.height);
    gridRenderer.initialize(initialViewport.width, initialViewport.height);
    initializeImGuiDebugPanel(window, glContext);

    const auto paths = locateDemoAppPaths(std::filesystem::current_path());
    auto definitions = ra2::DefinitionRegistry::load(paths.settingsRoot);
    if(aircraftSmokeTest || navalSmokeTest || navalSpecialSmokeTest) {
      auto rules=definitions.rules(),profile=definitions.replica();
      for(const auto& [id,type]:definitions.units()){(void)type;rules.overlay(ra2::IniDocument::parse("["+id+"]\nCanPassiveAquire=no\n"));}
      rules.overlay(ra2::IniDocument::parse("[NAIRON]\nStrength=10000\n[HYD]\nStrength=10000\n[JUMPJET]\nStrength=10000\n[E2]\nStrength=10000\nFearless=yes\n"));profile.overlay(ra2::IniDocument::parse("[Simulation]\nProductionFramesOverride=12\n"));
      definitions=ra2::DefinitionRegistry::compile(rules,definitions.art(),profile);
    }
    if(supportInfantrySmokeTest) {
      auto profile=definitions.replica();profile.overlay(ra2::IniDocument::parse("[Simulation]\nProductionFramesOverride=30\n"));
      definitions=ra2::DefinitionRegistry::compile(definitions.rules(),definitions.art(),profile);
    }
    if(transportSmokeTest) {
      auto rules=definitions.rules();rules.overlay(ra2::IniDocument::parse("[E1]\nCanPassiveAquire=no\n[E2]\nCanPassiveAquire=no\nStrength=10000\nFearless=yes\n[FV]\nCanPassiveAquire=no\n[HTK]\nCanPassiveAquire=no\n[HTNK]\nCanPassiveAquire=no\n"));
      definitions=ra2::DefinitionRegistry::compile(rules,definitions.art(),definitions.replica());
    }
    if(airDefenseSmokeTest) {
      auto rules=definitions.rules();rules.overlay(ra2::IniDocument::parse("[FLAKT]\nCanPassiveAquire=no\n[JUMPJET]\nCanPassiveAquire=no\n[E2]\nCanPassiveAquire=no\nStrength=10000\nFearless=yes\n[CombatDamage]\nBallisticScatter=0\n"));
      definitions=ra2::DefinitionRegistry::compile(rules,definitions.art(),definitions.replica());
    }
    if(specialVehicleSmokeTest) {
      auto rules=definitions.rules();rules.overlay(ra2::IniDocument::parse("[SREF]\nCanPassiveAquire=no\n[MGTK]\nCanPassiveAquire=no\n[HTNK]\nCanPassiveAquire=no\nStrength=10000\n[MTNK]\nCanPassiveAquire=no\n[E2]\nCanPassiveAquire=no\nStrength=10000\n[TNKD]\nCanPassiveAquire=no\n"));
      definitions=ra2::DefinitionRegistry::compile(rules,definitions.art(),definitions.replica());
    }
    if(teslaUnitsSmokeTest) {
      auto rules=definitions.rules(),art=definitions.art();
      rules.overlay(ra2::IniDocument::parse("[SHK]\nCanPassiveAquire=no\n[TTNK]\nCanPassiveAquire=no\n[TESLA]\nCanPassiveAquire=no\n[FV]\nCanPassiveAquire=no\n[HTNK]\nCanPassiveAquire=no\nStrength=100000\n[MTNK]\nCanPassiveAquire=no\n[E2]\nCanPassiveAquire=no\nFearless=yes\n"));
      art.overlay(ra2::IniDocument::parse("[ELECTRO]\nEnd="+std::to_string(definitions.effectAnimation("ELECTRO").frameCount-1)));
      definitions=ra2::DefinitionRegistry::compile(rules,art,definitions.replica());
    }
    if(ivanApocalypseSmokeTest) {
      auto rules=definitions.rules(),art=definitions.art();
      for(const auto id:{"IVAN","APOC","ENGINEER","SENGINEER","HTNK","MTNK","JUMPJET","FV"})rules.overlay(ra2::IniDocument::parse(std::string("[")+id+"]\nCanPassiveAquire=no\n"));
      rules.overlay(ra2::IniDocument::parse("[HTNK]\nStrength=10000\n[MTNK]\nPrimary=SmokeApocHit\n[SmokeApocHit]\nDamage=600\nROF=1000\nRange=30\nProjectile=InvisibleLow\nWarhead=SmokeApocWH\n[SmokeApocWH]\nVerses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%\n"));
      for(const auto& [id,animation]:definitions.effectAnimations())if(animation.frameCount>0)art.overlay(ra2::IniDocument::parse("["+id+"]\nEnd="+std::to_string(animation.frameCount-1)));
      definitions=ra2::DefinitionRegistry::compile(rules,art,definitions.replica());
    }
    if(demolitionSmokeTest) {
      auto rules=definitions.rules(),art=definitions.art();
      for(const auto id:{"TERROR","DTRUCK","HTNK","MTNK","E2","FV"})rules.overlay(ra2::IniDocument::parse(std::string("[")+id+"]\nCanPassiveAquire=no\n"));
      rules.overlay(ra2::IniDocument::parse("[HTNK]\nStrength=100000\n"));
      for(const auto& [id,animation]:definitions.effectAnimations())if(animation.frameCount>0)art.overlay(ra2::IniDocument::parse("["+id+"]\nEnd="+std::to_string(animation.frameCount-1)));
      definitions=ra2::DefinitionRegistry::compile(rules,art,definitions.replica());
    }
    if(dogDroneSmokeTest) {
      auto rules=definitions.rules(),art=definitions.art();
      for(const auto id:{"DOG","ADOG","DRON","HTNK","MTNK","E2","FV"})rules.overlay(ra2::IniDocument::parse(std::string("[")+id+"]\nCanPassiveAquire=no\n"));
      rules.overlay(ra2::IniDocument::parse("[HTNK]\nStrength=100\n"));
      for(const auto& [id,animation]:definitions.effectAnimations())if(animation.frameCount>0)art.overlay(ra2::IniDocument::parse("["+id+"]\nEnd="+std::to_string(animation.frameCount-1)));
      definitions=ra2::DefinitionRegistry::compile(rules,art,definitions.replica());
    }
    if(radiationV3SmokeTest) {
      auto rules=definitions.rules(),art=definitions.art();
      rules.overlay(ra2::IniDocument::parse("[DESO]\nCanPassiveAquire=no\n[V3]\nCanPassiveAquire=no\n[HTNK]\nCanPassiveAquire=no\nStrength=10000\n[MTNK]\nCanPassiveAquire=no\n[FV]\nCanPassiveAquire=no\n[E2]\nStrength=500\nFearless=yes\nCanPassiveAquire=no\n[FLAKT]\nCanPassiveAquire=yes\n[FlakGuyGun]\nRange=.01\n[FlakGuyAAGun]\nDamage=100000\n[CombatDamage]\nBallisticScatter=0\n"));
      for(const auto& [id,animation]:definitions.effectAnimations())if(animation.frameCount>0)art.overlay(ra2::IniDocument::parse("["+id+"]\nEnd="+std::to_string(animation.frameCount-1)));
      definitions=ra2::DefinitionRegistry::compile(rules,art,definitions.replica());
    }
    if(defenseSmokeTest) {
      auto rules=definitions.rules();rules.overlay(ra2::IniDocument::parse("[HTNK]\nCanPassiveAquire=no\n[MTNK]\nCanPassiveAquire=no\nStrength=100000\n"));
      definitions=ra2::DefinitionRegistry::compile(rules,definitions.art(),definitions.replica());
    }
    if(buildingActionsSmokeTest) {
      auto rules=definitions.rules();rules.overlay(ra2::IniDocument::parse("[GAPOWR]\nStrength=800\n[HTNK]\nCanPassiveAquire=no\n[MTNK]\nCanPassiveAquire=no\n[SREF]\nCanPassiveAquire=no\n[COMET]\nDamage=100\nWarhead=ServiceTestWH\n[ServiceTestWH]\nVerses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%\n"));
      definitions=ra2::DefinitionRegistry::compile(rules,definitions.art(),definitions.replica());
    }
    const auto artIni = ArtIni::fromDocument(definitions.art(), definitions.replica(), definitions.rules());
    const auto houseColors = loadHouseColors(definitions.rules());
    const auto voxelLighting = VplFile::load(paths.paletteRoot / "voxel" / "voxels.vpl");
    const auto paletteLibrary = loadPaletteLibrary(paths.paletteRoot);
    const auto cameoPalette = Palette::load(paths.paletteRoot / "ui" / "cameo.pal");
    const auto mousePalette = Palette::load(paths.paletteRoot / "ui" / "mousepal.pal");
    const auto overlayPalette = Palette::load(paths.paletteRoot / "ui" / "palette.pal");
    const auto animationPalette = Palette::load(paths.paletteRoot / "effects" / "anim.pal");
    (void)mousePalette;

    debugPanelState.style.houseColorIndex = houseColors.defaultIndex;
    ViewportState viewport = viewportStateForPanel(debugPanelState);

    const auto& initialTheaterPalettes = palettesForTheater(paletteLibrary, debugPanelState.style.theater);
    renderer.setIndexedPalettes(initialTheaterPalettes.unit, initialTheaterPalettes.terrain);
    renderer.setResourcePalettes(initialTheaterPalettes.resource, animationPalette);
    renderer.setRemapColor(activeHouseColorValue(debugPanelState.style, houseColors));

    BuildingAssetMap* activeBuildingAssets =
      &cachedAssetsForStyle(buildingAssetCache,
                            paths.buildingRoot,
                            artIni,
                            paletteLibrary,
                            houseColors,
                            debugPanelState.style);
    sidebarAssets = loadSidebarAssetsForStyle(paths, debugPanelState.style, cameoPalette);
    if(sidebarSmokeTest)verifyMixedSidebar(renderer,window,paths,cameoPalette,definitions,viewport.width,viewport.height,
      argc==3?std::filesystem::path(argv[2]).parent_path():std::filesystem::path{});
    rhinoUiAssets = loadRhinoUnitUiAssets(paths.unitOverlayRoot, overlayPalette);
    weaponVisualAssets = loadWeaponVisualAssets(paths.effectsRoot, animationPalette, &definitions);
    ra2::loadResourceVisualAssets(resourceVisualAssets, paths.projectRoot / "assets");
    ra2::loadInfantryVisualAssets(infantryVisualAssets,definitions,paths.projectRoot/"assets");
    ra2::loadSpecialVehicleAssets(specialVehicleAssets,definitions,paths.projectRoot/"assets");
    ra2::loadBuildingActionVisualAssets(buildingActionAssets,paths.projectRoot/"assets");
    ra2::loadNavalVisualAssets(navalVisualAssets,paths.projectRoot/"assets");
    if(aircraftSmokeTest)verifyAircraftRendering(renderer,window,paths,definitions,paletteLibrary,houseColors,voxelLighting,vehicleRendererCache,viewport.height,argc==3?std::filesystem::path(argv[2]).parent_path():std::filesystem::path{});
    if(navalSpecialSmokeTest)verifyNavalSpecialRendering(renderer,window,paths,definitions,paletteLibrary,houseColors,voxelLighting,vehicleRendererCache,infantryVisualAssets,weaponVisualAssets,viewport.height,argc==3?std::filesystem::path(argv[2]).parent_path():std::filesystem::path{});
    if(navalSmokeTest) {
      verifyNavalRendering(renderer,window,paths,definitions,paletteLibrary,houseColors,voxelLighting,vehicleRendererCache,navalVisualAssets,weaponVisualAssets,viewport.height,argc==3?std::filesystem::path(argv[2]).parent_path():std::filesystem::path{});
      setVehicleRendererCachePalette(vehicleRendererCache,initialTheaterPalettes.unit);renderer.setIndexedPalettes(initialTheaterPalettes.unit,initialTheaterPalettes.terrain);
    }
    if(commandoSmokeTest) {
      verifyCommandoRendering(renderer,definitions,paletteLibrary,infantryVisualAssets,weaponVisualAssets,viewport.height,
        argc==3?std::filesystem::path(argv[2]).parent_path():std::filesystem::path{});
      renderer.setIndexedPalettes(initialTheaterPalettes.unit,initialTheaterPalettes.terrain);
    }
    if(ivanApocalypseSmokeTest) {
      verifyIvanApocalypseRendering(renderer,window,paths,definitions,paletteLibrary,houseColors,voxelLighting,vehicleRendererCache,infantryVisualAssets,weaponVisualAssets,viewport.height,
        argc==3?std::filesystem::path(argv[2]).parent_path():std::filesystem::path{});
      setVehicleRendererCachePalette(vehicleRendererCache,initialTheaterPalettes.unit);renderer.setIndexedPalettes(initialTheaterPalettes.unit,initialTheaterPalettes.terrain);
    }
    if(demolitionSmokeTest) {
      verifyDemolitionRendering(renderer,window,paths,definitions,paletteLibrary,houseColors,voxelLighting,vehicleRendererCache,infantryVisualAssets,weaponVisualAssets,viewport.height,
        argc==3?std::filesystem::path(argv[2]).parent_path():std::filesystem::path{});
      setVehicleRendererCachePalette(vehicleRendererCache,initialTheaterPalettes.unit);
      renderer.setIndexedPalettes(initialTheaterPalettes.unit,initialTheaterPalettes.terrain);
    }
    if(dogDroneSmokeTest) {
      verifyDogDroneRendering(renderer,infantryVisualAssets,paletteLibrary,viewport.height,argc==3?std::filesystem::path(argv[2]).parent_path():std::filesystem::path{});
      renderer.setIndexedPalettes(initialTheaterPalettes.unit,initialTheaterPalettes.terrain);
    }
    if(radiationV3SmokeTest) {
      verifyRadiationV3Rendering(renderer,window,paths,definitions,paletteLibrary,houseColors,voxelLighting,vehicleRendererCache,weaponVisualAssets,viewport.height,
        argc==3?std::filesystem::path(argv[2]).parent_path():std::filesystem::path{});
      setVehicleRendererCachePalette(vehicleRendererCache,initialTheaterPalettes.unit);
      renderer.setIndexedPalettes(initialTheaterPalettes.unit,initialTheaterPalettes.terrain);
    }
    if(specialVehicleSmokeTest) {
      const auto output=argc==3?std::filesystem::path(argv[2]).parent_path():std::filesystem::path{};
      verifyLaserBeamRendering(renderer,definitions,viewport.height,output);
      auto& model=vehicleRendererForType(definitions.unit("MGTK"),vehicleRendererCache,window,paths,voxelLighting,initialTheaterPalettes.unit);
      verifyMirageRendering(renderer,model,houseColors,paletteLibrary,specialVehicleAssets,paths.projectRoot/"assets",viewport.height,output);
      verifyMirageTimingRendering(renderer,model,houseColors,definitions,specialVehicleAssets,rhinoUiAssets,weaponVisualAssets,viewport.height,output);
      setVehicleRendererCachePalette(vehicleRendererCache,initialTheaterPalettes.unit);
      renderer.setIndexedPalettes(initialTheaterPalettes.unit,initialTheaterPalettes.terrain);
    }
    if(miningSmokeTest) {
      verifyMineralPaletteRendering(renderer,resourceVisualAssets,paletteLibrary,animationPalette,
                                     paths.projectRoot/"assets",viewport.height);
      renderer.setIndexedPalettes(initialTheaterPalettes.unit,initialTheaterPalettes.terrain);
      renderer.setResourcePalettes(initialTheaterPalettes.resource,animationPalette);
      renderer.setRemapColor(activeHouseColorValue(debugPanelState.style,houseColors));
    }

    grandCannonRenderer.initialize(window);
    grandCannonRenderer.loadTurretAssets(paths.buildingRoot / "allied" / "gtgcan", voxelLighting, "gtgcantur", "gtgcanbarl");
    grandCannonRenderer.setPalette(initialTheaterPalettes.unit);
    if(defenseSmokeTest) {
      verifyDefenseRendering(renderer,window,paths,definitions,artIni,paletteLibrary,houseColors,voxelLighting,
        buildingAssetCache,vehicleRendererCache,grandCannonRenderer,weaponVisualAssets,viewport.width,viewport.height,
        argc==3?std::filesystem::path(argv[2]).parent_path():std::filesystem::path{});
      setVehicleRendererCachePalette(vehicleRendererCache,initialTheaterPalettes.unit);
      grandCannonRenderer.setPalette(initialTheaterPalettes.unit);
      renderer.setIndexedPalettes(initialTheaterPalettes.unit,initialTheaterPalettes.terrain);
    }

    ra2::World world(definitions);
    debugPanelState.gameplay.speed = world.gameSpeed();
    const auto initialBuildingId = defaultPowerPlantId(debugPanelState.style.faction);
    if (!world.spawn(initialBuildingId, {18,14}) || !world.spawn("HTNK", {7,10}) ||
        !world.spawn(mcvUnitIdForFaction(debugPanelState.style.faction), {10,14}) || !world.spawn("MTNK", {12,10}, 1))
      throw std::runtime_error("Initial scenario cannot be created");
    std::array<ra2::EntityId,2> serviceTargets{};
    int serviceMoneyBeforeSale=0;
    if(buildingActionsSmokeTest) {
      serviceTargets={world.structureAt({18,14}),world.spawn("NAPOWR",{15,14})};
      const auto first=world.spawn("SREF",{13,14},1),second=world.spawn("SREF",{11,14},1);
      if(!serviceTargets[1] || !first || !second)throw std::runtime_error("Building action scenario setup failed");
      world.enqueue({1,1,1,ra2::CommandKind::AttackEntity,first,serviceTargets[0]});
      world.enqueue({1,1,2,ra2::CommandKind::AttackEntity,second,serviceTargets[1]});
      world.enqueue({3,1,3,ra2::CommandKind::Stop,first});world.enqueue({3,1,4,ra2::CommandKind::Stop,second});
      for(const auto mode:{ra2::BuildingActionMode::Repair,ra2::BuildingActionMode::Sell})for(const bool valid:{false,true}) {
        ra2::updateBuildingActionCursor(buildingActionAssets,mode,valid,0);
        auto* expected=mode==ra2::BuildingActionMode::Repair?(valid?buildingActionAssets.repair[0]:buildingActionAssets.repairBlocked):
          valid?buildingActionAssets.sell[0]:buildingActionAssets.sellBlocked;
        if(SDL_GetCursor()!=expected)throw std::runtime_error("Original repair/sell cursor not applied");
      }
      ra2::updateBuildingActionCursor(buildingActionAssets,ra2::BuildingActionMode::None,false,0);
    }
    const auto selectionSmokeUnit=selectionSmokeTest?world.spawn("MTNK",{8,10}):0;
    if(selectionSmokeTest && !selectionSmokeUnit) throw std::runtime_error("Selection smoke unit placement failed");
    if (!smokeTest || sandboxSmokeTest || miningSmokeTest) {
      if (!world.spawn("GAREFN",{5,4}) || !world.spawn("NAREFN",{14,6}))
        throw std::runtime_error("Mining scenario placement failed");
      // Dense centers and sparse, irregular edges make depletion readable.
      for(int x=2;x<=7;++x) for(int y=11;y<=15;++y) {
        const int dx=2*x-9, dy=2*y-26;
        const int edge=dx*dx+2*dy*dy;
        if(edge<=30) world.setResource({x,y},"Riparius",std::clamp(38-edge,6,32));
      }
      for(int x=12;x<=16;++x) for(int y=12;y<=15;++y) {
        const int dx=2*x-28, dy=2*y-27;
        const int edge=dx*dx+2*dy*dy;
        if(edge<=22) world.setResource({x,y},"Cruentus",std::clamp(28-edge,4,24));
      }
    }
    if(!smokeTest || sandboxSmokeTest) {
      const bool allied=debugPanelState.style.faction==BuildFaction::Allied;
      if(!world.spawn(allied?"GAPILE":"NAHAND",{1,5}))throw std::runtime_error("Initial barracks placement failed");
      for(int i=0;i<3;++i)if(!world.spawn(allied?"E1":"E2",{5,8}))throw std::runtime_error("Initial infantry placement failed");
      if(!world.spawn(allied?"ENGINEER":"SENGINEER",{6,9}) || (allied && !world.spawn("SNIPE",{7,8})) ||
          !world.spawn("GAPOWR",{12,2},1))throw std::runtime_error("Support infantry practice setup failed");
      if(!world.spawn(allied?"FV":"HTK",{9,8}) || !world.spawn("CAHSE01",{0,9},2))throw std::runtime_error("Transport/garrison practice setup failed");
      if(!world.spawn(allied?"JUMPJET":"FLAKT",{6,6}))throw std::runtime_error("Air defense infantry setup failed");
      if(allied && (!world.spawn("SREF",{3,8}) || !world.spawn("MGTK",{3,10}) || !world.spawn("TNKD",{3,12})))throw std::runtime_error("Special vehicle practice setup failed");
    }
    std::array<ra2::EntityId,7> airPractice{};
    bool sawAirSalvo=false,sawAirReturn=false,sawAirRearm=false,sawAirBoard=false,sawAirUnload=false,sawAirBomb=false,sawPlaneClick=false;
    if(!smokeTest || sandboxSmokeTest || aircraftSmokeTest) {
      airPractice[0]=world.spawn("GAAIRC",{10,0});airPractice[3]=world.spawn("SHAD",{8,1});airPractice[4]=world.spawn("ZEP",{14,12});airPractice[5]=world.spawn("E1",{7,1});
      if(!airPractice[0] || !airPractice[3] || !airPractice[4] || !airPractice[5])throw std::runtime_error("Air practice setup failed");
      if(aircraftSmokeTest) {
        airPractice[6]=world.spawn("NAIRON",{16,1},1);if(!airPractice[6])throw std::runtime_error("Air target setup failed");
        world.enqueue({1,0,900001,ra2::CommandKind::Produce,airPractice[0],0,{},"ORCA"});world.enqueue({1,0,900002,ra2::CommandKind::Produce,airPractice[0],0,{},"BEAG"});
        world.enqueue({1,0,900003,ra2::CommandKind::EnterTransport,airPractice[5],airPractice[3]});
      } else {if(!world.spawn("GAPOWR",{14,0}))throw std::runtime_error("Airfield power setup failed");airPractice[1]=world.spawn("ORCA",{10,0});airPractice[2]=world.spawn("BEAG",{10,0});if(!airPractice[1] || !airPractice[2])throw std::runtime_error("Airport initial aircraft setup failed");}
    }
    std::array<ra2::EntityId,10> navy{};
    std::array<ra2::EntityId,9> specialNavy{};
    bool sawHornets=false,sawAsw=false,sawDreadMissiles=false,sawGrapple=false,sawRelease=false,sawSonic=false,sawNavyClick=false,sawReload=false;
    bool sawNavalBoard=false,sawNavalDeepHold=false,sawNavalUnload=false,sawTorpedo=false,sawMedusa=false,sawNavalFlak=false,sawSubmerged=false,sawNavalProduction=false;
    if(!smokeTest || sandboxSmokeTest || navalSmokeTest || navalSpecialSmokeTest) {
      ra2::initializeNavalCoast(world);
      navy[0]=world.spawn("GAYARD",{24,2});navy[1]=world.spawn("NAYARD",{24,15});navy[2]=world.spawn("LCRF",{21,8});navy[3]=world.spawn("SAPC",{21,14});navy[4]=world.spawn("SUB",{30,10});navy[5]=world.spawn("HYD",{30,21});navy[6]=world.spawn("AEGIS",{30,4});navy[7]=world.spawn("MTNK",{19,8});
      if(std::any_of(navy.begin(),navy.begin()+8,[](auto id){return !id;}))throw std::runtime_error("Naval practice setup failed");
      if(navalSmokeTest){navy[8]=world.spawn("HYD",{36,10},1);navy[9]=world.spawn("JUMPJET",{32,5},1);if(!navy[8] || !navy[9])throw std::runtime_error("Naval combat setup failed");}

    }
    if(!smokeTest || sandboxSmokeTest || navalSpecialSmokeTest) {
      specialNavy[0]=world.spawn("DEST",{30,7});specialNavy[1]=world.spawn("CARRIER",{36,14});specialNavy[2]=world.spawn("DRED",{33,12});specialNavy[3]=world.spawn("DLPH",{29,22});specialNavy[4]=world.spawn("SQD",{30,24});
      if(navalSpecialSmokeTest){specialNavy[5]=world.spawn("SUB",{34,7},1);specialNavy[6]=world.spawn("HYD",{31,24},1);specialNavy[7]=world.spawn("GAPOWR",{20,3},1);specialNavy[8]=world.spawn("GAPOWR",{20,9},1);}
      if(std::any_of(specialNavy.begin(),specialNavy.begin()+(navalSpecialSmokeTest?9:5),[](auto id){return !id;}))throw std::runtime_error("Special navy practice setup failed");
    }
    Vec2 cameraOffset=aircraftSmokeTest?Vec2{0,100}:(navalSmokeTest || navalSpecialSmokeTest)?Vec2{-360,-280}:Vec2{};
    const auto& map = world.map();
    const BuildingPlacement initialPlacement{{18,14}, definitions.unit(initialBuildingId).foundationWidth, definitions.unit(initialBuildingId).foundationHeight};
    SidebarState sidebarState{};
    const BuildingAsset* selectedBuildAsset = nullptr;
    BuildingPlacement previewPlacement = initialPlacement;
    ra2::UnitSelection selection;
    if(sidebarSmokeTest)selection.selectOne(world.structureAt({18,14}));
    ra2::SelectionGesture selectionGesture;
    std::uint64_t sequence = aircraftSmokeTest?900003:0;
    std::vector<BuildingRenderCommand> renderQueue;
    auto presentation = ra2::makeWorldView(world, definitions, selection.ids());
    auto lastCounter = SDL_GetPerformanceCounter();
    double fractionalMicroseconds = 0;
    std::uint64_t nextUnlimitedRenderCounter = 0;
    bool swapIntervalEnabled = !smokeTest;
    double metricsMicroseconds = 0, measuredGameFps = 0;
    ra2::Tick metricsStartFrame = 0;
    std::string interactionMessage;
    auto send = [&](ra2::CommandKind kind, ra2::EntityId entity = 0, TileCoord cell = {}, std::string type = {}, ra2::EntityId target = 0) {
      if (!world.enqueue({world.tick() + 1, 0, ++sequence, kind, entity, target, cell, std::move(type)})) interactionMessage = "Command rejected";
    };
    auto sendSelection = [&](ra2::CommandKind kind, TileCoord cell = {}, ra2::EntityId target = 0) {
      return ra2::issueSelectionCommand(world,definitions,selection,kind,cell,target,sequence);
    };
    std::vector<ra2::EntityId> infantrySmokeUnits;
    ra2::EntityId supportCapture=0,supportRepair=0,supportVictim=0,supportEngineer=0,supportSovietEngineer=0;
    ra2::EntityId transportIfv=0,transportTrack=0,transportHouse=0,transportRepairTarget=0;
    bool sawTransportHidden=false,sawGarrison=false,sawGarrisonFire=false,sawMissile=false,sawIfvDamage=false,sawIfvRepair=false;
    unsigned transportModes=0;
    ra2::EntityId airSmokeRock=0;
    Vec2 airSmokeClick{};
    bool sawAirFlight=false,sawAirHover=false,sawAirFire=false,sawFlakGround=false,sawFlakAir=false,sawAirDeath=false,sawAirClick=false;
    ra2::EntityId specialPrism=0,specialMirage=0,specialEnemyMirage=0;
    bool sawSabotExplosion=false,sawPrismBeam=false,sawPrismFragments=false,sawMirageTree=false,sawMirageMove=false,sawMirageFire=false,sawMirageClick=false;
    unsigned chargeStages=0;
    ra2::EntityId teslaCoil=0,teslaInfantry=0,teslaIfv=0;
    unsigned teslaWeapons=0,teslaTheaters=0;
    bool sawTeslaIfv=false,sawTeslaUnload=false,sawElectro=false,sawTeslaClick=false;
    std::array<ra2::EntityId,3> leapActors{},leapVictims{};
    ra2::EntityId droneHost=0,hostDrone=0;unsigned leapSeen=0;
    bool sawInfestation=false,sawDroneReturn=false,sawDogDroneClick=false;
    ra2::EntityId radiationWorker=0,radiationIfv=0,v3Impact=0,v3Intercept=0,v3Victim=0;
    unsigned radiationTheaters=0,v3BodyStates=0;
    bool sawRadiationBeam=false,sawRadiationIfvBeam=false,sawRadDeploy=false,sawRadMove=false,sawRadSite=false,sawRadDeath=false,sawV3Missile=false,sawV3Intercept=false;
    ra2::EntityId demolitionFoot=0,demolitionTruck=0,demolitionIfv=0,demolitionPassenger=0,demolitionSelection=0;
    unsigned demolitionExplosions=0;bool sawDemolitionMode=false,sawDemolitionRadiation=false,sawDemolitionClick=false;
    ra2::EntityId ivanTimed=0,ivanTimedTarget=0,ivanDefuseTarget=0,ivanEngineer=0,ivanIfv=0,ivanIfvPassenger=0,ivanApoc=0;
    unsigned ivanIconFrames=0,ivanWeapons=0;int apocLowestHp=800;ra2::Tick ivanBlastFrame=0;
    bool sawIvanAttach=false,sawIvanDisarmAttach=false,sawIvanDisarm=false,sawIvanSelection=false,sawIvanIfv=false,sawIvanIfvBlast=false,sawApocHeal=false,sawApocMuzzle=false,sawApocImpact=false;
    if(ivanApocalypseSmokeTest) {
      ivanTimed=world.spawn("IVAN",{5,5});ivanTimedTarget=world.spawn("HTNK",{6,5},1);const auto enemyIvan=world.spawn("IVAN",{4,11},1);
      ivanDefuseTarget=world.spawn("HTNK",{5,11});ivanEngineer=world.spawn("ENGINEER",{7,12});ivanIfv=world.spawn("FV",{18,1});ivanIfvPassenger=world.spawn("IVAN",{16,1});
      ivanApoc=world.spawn("APOC",{9,8});const auto ground=world.spawn("HTNK",{13,8},1),air=world.spawn("JUMPJET",{13,6},1),damage=world.spawn("MTNK",{17,12},1);
      if(!ivanTimed || !ivanTimedTarget || !enemyIvan || !ivanDefuseTarget || !ivanEngineer || !ivanIfv || !ivanIfvPassenger || !ivanApoc || !ground || !air || !damage)throw std::runtime_error("Ivan/Apocalypse scene setup failed");
      world.enqueue({1,0,++sequence,ra2::CommandKind::AttackEntity,ivanTimed,ivanTimedTarget});world.enqueue({1,1,1,ra2::CommandKind::AttackEntity,enemyIvan,ivanDefuseTarget});
      world.enqueue({120,1,2,ra2::CommandKind::Move,ivanTimedTarget,0,{8,4}});world.enqueue({1,0,++sequence,ra2::CommandKind::EnterTransport,ivanIfvPassenger,ivanIfv});
      world.enqueue({220,0,++sequence,ra2::CommandKind::AttackGround,ivanIfv,0,{18,1}});
      world.enqueue({1,0,++sequence,ra2::CommandKind::AttackEntity,ivanApoc,ground});world.enqueue({90,0,++sequence,ra2::CommandKind::AttackEntity,ivanApoc,air});world.enqueue({170,0,++sequence,ra2::CommandKind::Stop,ivanApoc});
      world.enqueue({1,1,3,ra2::CommandKind::AttackEntity,damage,ivanApoc});world.enqueue({80,1,4,ra2::CommandKind::Stop,damage});selection.selectOne(ivanTimed);
    } else if(demolitionSmokeTest) {
      demolitionFoot=world.spawn("TERROR",{4,4});demolitionTruck=world.spawn("DTRUCK",{5,11});demolitionIfv=world.spawn("FV",{17,1});
      demolitionPassenger=world.spawn("TERROR",{15,1});demolitionSelection=world.spawn("TERROR",{10,3});
      const auto victim=world.spawn("HTNK",{6,4},1),truckVictim=world.spawn("HTNK",{6,11},1);
      if(!demolitionFoot || !demolitionTruck || !demolitionIfv || !demolitionPassenger || !demolitionSelection || !victim || !truckVictim)throw std::runtime_error("Demolition smoke setup failed");
      world.enqueue({1,0,++sequence,ra2::CommandKind::EnterTransport,demolitionPassenger,demolitionIfv});
      world.enqueue({45,0,++sequence,ra2::CommandKind::AttackEntity,demolitionFoot,victim});
      world.enqueue({100,0,++sequence,ra2::CommandKind::AttackEntity,demolitionTruck,truckVictim});
      world.enqueue({170,0,++sequence,ra2::CommandKind::AttackGround,demolitionIfv,0,{17,1}});
      selection.selectOne(demolitionFoot);
    } else if(dogDroneSmokeTest) {
      for(int i=0;i<3;++i) {
        leapActors[i]=world.spawn(std::array{"DOG","ADOG","DRON"}[i],{3,3+i*3});leapVictims[i]=world.spawn("E2",{4,3+i*3},1);
        if(!leapActors[i] || !leapVictims[i])throw std::runtime_error("Dog/drone smoke placement failed");
        world.enqueue({1,0,++sequence,ra2::CommandKind::AttackEntity,leapActors[i],leapVictims[i]});
      }
      hostDrone=world.spawn("DRON",{7,4});droneHost=world.spawn("HTNK",{8,4},1);if(!hostDrone || !droneHost)throw std::runtime_error("Parasite scene setup failed");
      world.enqueue({1,0,++sequence,ra2::CommandKind::AttackEntity,hostDrone,droneHost});
      world.enqueue({40,1,1,ra2::CommandKind::Move,droneHost,0,{10,4}});
      selection.selectOne(leapActors[0]);
    } else if(radiationV3SmokeTest) {
      radiationWorker=world.spawn("DESO",{4,12});radiationIfv=world.spawn("FV",{1,0});const auto passenger=world.spawn("DESO",{0,0});
      const auto gun=world.spawn("DESO",{3,7}),target=world.spawn("E2",{6,7},1),ifvTarget=world.spawn("HTNK",{4,0},1);
      v3Impact=world.spawn("V3",{6,1});v3Victim=world.spawn("GAPOWR",{14,1},1);v3Intercept=world.spawn("V3",{15,15});
      const auto interceptedTarget=world.spawn("GAPOWR",{23,15},1),flak=world.spawn("FLAKT",{20,15},1);
      if(!radiationWorker || !radiationIfv || !passenger || !gun || !target || !ifvTarget || !v3Impact || !v3Victim || !v3Intercept || !interceptedTarget || !flak)throw std::runtime_error("Radiation/V3 scene setup failed");
      selection.selectOne(radiationWorker);
      send(ra2::CommandKind::AttackEntity,gun,{}, {},target);send(ra2::CommandKind::EnterTransport,passenger,{}, {},radiationIfv);
      send(ra2::CommandKind::AttackEntity,v3Impact,{}, {},v3Victim);send(ra2::CommandKind::AttackEntity,v3Intercept,{}, {},interceptedTarget);
      world.enqueue({100,0,++sequence,ra2::CommandKind::AttackEntity,radiationIfv,ifvTarget});
      world.enqueue({250,0,++sequence,ra2::CommandKind::Stop,v3Impact});world.enqueue({250,0,++sequence,ra2::CommandKind::Stop,v3Intercept});
    } else if(teslaUnitsSmokeTest) {
      teslaCoil=world.spawn("TESLA",{5,5});teslaInfantry=world.spawn("SHK",{4,5});teslaIfv=world.spawn("FV",{2,9});
      const auto second=world.spawn("SHK",{5,4}),third=world.spawn("SHK",{6,5}),passenger=world.spawn("SHK",{1,9});
      const auto foot=world.spawn("SHK",{2,3}),tank=world.spawn("TTNK",{2,7}),victim=world.spawn("E2",{4,7},1),footTarget=world.spawn("HTNK",{4,3},1),ifvTarget=world.spawn("HTNK",{5,9},1),coilTarget=world.spawn("HTNK",{10,5},1);
      if(!teslaCoil || !teslaInfantry || !teslaIfv || !second || !third || !passenger || !foot || !tank || !victim || !footTarget || !ifvTarget || !coilTarget)throw std::runtime_error("Tesla smoke setup failed");
      selection.selectOne(teslaInfantry); // Charge this first trooper by an actual SDL click below.
      send(ra2::CommandKind::AttackEntity,foot,{}, {},footTarget);send(ra2::CommandKind::AttackEntity,tank,{}, {},victim);
      send(ra2::CommandKind::EnterTransport,passenger,{}, {},teslaIfv);send(ra2::CommandKind::AttackEntity,teslaCoil,{}, {},coilTarget);
      world.enqueue({90,0,++sequence,ra2::CommandKind::AttackEntity,second,teslaCoil});
      world.enqueue({150,0,++sequence,ra2::CommandKind::AttackEntity,third,teslaCoil});
      world.enqueue({130,0,++sequence,ra2::CommandKind::AttackEntity,teslaIfv,ifvTarget});
      world.enqueue({225,0,++sequence,ra2::CommandKind::Unload,teslaIfv});
    } else if(specialVehicleSmokeTest) {
      specialPrism=world.spawn("SREF",{3,6});specialMirage=world.spawn("MGTK",{3,3});specialEnemyMirage=world.spawn("MGTK",{6,2},1);
      const auto sabot=world.spawn("TNKD",{3,9}),target=world.spawn("HTNK",{8,6},1),sabotTarget=world.spawn("HTNK",{8,9},1),fragment=world.spawn("E2",{9,7},1);
      if(!specialPrism || !specialMirage || !specialEnemyMirage || !sabot || !target || !sabotTarget || !fragment)throw std::runtime_error("Special vehicle smoke setup failed");
      send(ra2::CommandKind::AttackEntity,sabot,{}, {},sabotTarget);
      world.enqueue({20,0,++sequence,ra2::CommandKind::AttackEntity,specialPrism,target});
      world.enqueue({50,0,++sequence,ra2::CommandKind::Move,specialMirage,0,{3,5}});
      world.enqueue({120,0,++sequence,ra2::CommandKind::AttackEntity,specialMirage,target});
      world.enqueue({220,0,++sequence,ra2::CommandKind::Move,specialMirage,0,{2,4}});
    } else if(airDefenseSmokeTest) {
      const auto flak=world.spawn("FLAKT",{4,6}),targetAir=world.spawn("JUMPJET",{9,7},1),targetGround=world.spawn("E2",{7,4},1);
      airSmokeRock=world.spawn("JUMPJET",{2,7});const auto killer=world.spawn("FLAKT",{2,3},1);
      if(!flak || !targetAir || !targetGround || !airSmokeRock || !killer || !world.spawn("GAPOWR",{3,7}))throw std::runtime_error("Air defense smoke setup failed");
      send(ra2::CommandKind::Move,airSmokeRock,{5,7});send(ra2::CommandKind::AttackEntity,flak,{}, {},targetAir);
      world.enqueue({90,0,++sequence,ra2::CommandKind::Stop,airSmokeRock});
      world.enqueue({120,0,++sequence,ra2::CommandKind::AttackEntity,airSmokeRock,targetGround});
      world.enqueue({160,0,++sequence,ra2::CommandKind::AttackEntity,flak,targetGround});
      world.enqueue({300,1,1,ra2::CommandKind::AttackEntity,killer,airSmokeRock});
    } else if(transportSmokeTest) {
      transportIfv=world.spawn("FV",{7,4});transportTrack=world.spawn("HTK",{2,9});transportHouse=world.spawn("CAHSE01",{0,4},2);
      transportRepairTarget=world.spawn("HTNK",{9,4});
      const auto gi=world.spawn("E1",{6,4}),engineer=world.spawn("ENGINEER",{6,6}),sniper=world.spawn("SNIPE",{6,8});
      const auto trackA=world.spawn("E2",{1,9}),trackB=world.spawn("E1",{1,9}),garrisonA=world.spawn("E1",{-1,4}),garrisonB=world.spawn("E2",{-1,5});
      const auto victim=world.spawn("E2",{4,7},1),gun=world.spawn("E1",{10,6}),missile=world.spawn("FV",{8,9});
      if(!transportIfv || !transportTrack || !transportHouse || !transportRepairTarget || !gi || !engineer || !sniper || !trackA || !trackB || !garrisonA || !garrisonB || !victim || !gun || !missile)
        throw std::runtime_error("Transport smoke setup failed");
      send(ra2::CommandKind::EnterTransport,gi,{}, {},transportIfv);send(ra2::CommandKind::EnterTransport,trackA,{}, {},transportTrack);send(ra2::CommandKind::EnterTransport,trackB,{}, {},transportTrack);
      send(ra2::CommandKind::EnterBuilding,garrisonA,{}, {},transportHouse);send(ra2::CommandKind::EnterBuilding,garrisonB,{}, {},transportHouse);
      send(ra2::CommandKind::AttackEntity,missile,{}, {},victim);
      world.enqueue({120,0,++sequence,ra2::CommandKind::Move,transportTrack,0,{5,10}});
      world.enqueue({140,0,++sequence,ra2::CommandKind::EnterTransport,engineer,transportIfv});
      world.enqueue({250,0,++sequence,ra2::CommandKind::AttackGround,gun,0,{9,4}});
      world.enqueue({275,0,++sequence,ra2::CommandKind::Stop,gun});
      world.enqueue({280,0,++sequence,ra2::CommandKind::AttackEntity,transportIfv,transportRepairTarget});
      world.enqueue({400,0,++sequence,ra2::CommandKind::EnterTransport,sniper,transportIfv});
      world.enqueue({580,0,++sequence,ra2::CommandKind::AttackEntity,transportIfv,victim});
      selection.selectOne(transportIfv);
    } else if(supportInfantrySmokeTest) {
      const auto allied=world.spawn("GAPILE",{0,2}),soviet=world.spawn("NAHAND",{5,2});
      supportCapture=world.spawn("GAPOWR",{3,10},1);supportRepair=world.spawn("GAPOWR",{0,13});
      supportEngineer=world.spawn("ENGINEER",{0,8});supportSovietEngineer=world.spawn("SENGINEER",{-2,10});
      const auto sniper=world.spawn("SNIPE",{2,14}),gun=world.spawn("E1",{0,16});supportVictim=world.spawn("E2",{4,14},1);
      if(!allied || !soviet || !supportCapture || !supportRepair || !supportEngineer || !supportSovietEngineer || !sniper || !gun || !supportVictim)
        throw std::runtime_error("Support infantry smoke setup failed");
      selection.selectOne(supportEngineer);
      send(ra2::CommandKind::EnterBuilding,supportEngineer,{}, {},supportCapture);
      send(ra2::CommandKind::AttackGround,gun,{0,13});
      world.enqueue({25,0,++sequence,ra2::CommandKind::Stop,gun});
      world.enqueue({30,0,++sequence,ra2::CommandKind::EnterBuilding,supportSovietEngineer,supportRepair});
      send(ra2::CommandKind::Produce,allied,{},"ENGINEER");send(ra2::CommandKind::Produce,soviet,{},"SENGINEER");
    } else if(infantrySmokeTest) {
      const auto barracks=world.spawn("GAPILE",{0,2});
      for(int i=0;i<3;++i)infantrySmokeUnits.push_back(world.spawn("E1",{0,6}));
      for(int i=0;i<3;++i)if(!world.spawn("E2",{3,6},1))throw std::runtime_error("Infantry smoke spawn failed");
      if(!barracks || !infantrySmokeUnits.front())throw std::runtime_error("Infantry smoke setup failed");
      selection.selectOne(infantrySmokeUnits.front());
      send(ra2::CommandKind::Deploy,infantrySmokeUnits.front());
      send(ra2::CommandKind::Produce,barracks,{},"E1");
      world.enqueue({40,0,++sequence,ra2::CommandKind::Move,infantrySmokeUnits.back(),0,{1,9}});
      world.enqueue({130,0,++sequence,ra2::CommandKind::Deploy,infantrySmokeUnits.front()});
    }
    ra2::EntityId testMcv = 0;
    ra2::EntityId testSovietMcv = 0;
    if (tankSmokeTest) {
      selection.selectOne(2);
      send(ra2::CommandKind::Move,2,{11,12});
      world.enqueue({1,1,1,ra2::CommandKind::Move,4,0,{12,13}});
    }
    if (buildingSmokeTest) {
      testMcv = 3;
      testSovietMcv = world.spawn("SMCV", {5,5});
      if (!testSovietMcv) throw std::runtime_error("Building smoke scenario placement failed");
      selection.selectOne(1);
      send(ra2::CommandKind::Deploy,testMcv);
      send(ra2::CommandKind::Deploy,testSovietMcv);
    }
    if (simulationSmokeTest) {
      const auto alliedFactory = world.spawn("GAWEAP", {10,4});
      const auto sovietFactory = world.spawn("NAWEAP", {1,4});
      testMcv = world.spawn("AMCV", {5,0});
      if (!alliedFactory || !sovietFactory || !testMcv) throw std::runtime_error("Smoke scenario placement failed");
      send(ra2::CommandKind::Deploy, testMcv);
      send(ra2::CommandKind::Produce, alliedFactory, {}, "AMCV");
      send(ra2::CommandKind::Produce, sovietFactory, {}, "HTNK");
      send(ra2::CommandKind::AttackEntity, 2, {12,10}, {}, 4);
      send(ra2::CommandKind::PlaceBuilding, 0, {0,10}, "GAPILE");
    }
    constexpr std::array<ra2::GameSpeed,6> smokeSpeeds{{
      {ra2::GameSpeedMode::Multiplayer,0}, {ra2::GameSpeedMode::Multiplayer,5},
      {ra2::GameSpeedMode::Singleplayer,5}, {ra2::GameSpeedMode::Singleplayer,6},
      {ra2::GameSpeedMode::Singleplayer,6}, {ra2::GameSpeedMode::Multiplayer,6}}};
    ra2::Tick speedSmokePreviousFrame = 0;
    if (gameSpeedSmokeTest) {
      debugPanelState.gameplay.speed = smokeSpeeds.front();
      world.setGameSpeed(smokeSpeeds.front());
    }
    bool running = true;
    int smokeTestFrames = 0;
    bool sawInternalExit = false, sawCombatDamage = false;
    bool sawInfantryDeployed=false,sawInfantryWalk=false,sawInfantryFire=false;
    bool sawEngineerEntry=false,sawSniperShot=false,sawDamagedRepair=false;
    bool sawMiningCargo = false, sawMiningReturn = false;
    bool sawMovingTurret = false, sawShell = false, sawExplosion = false;
    std::array<bool,2> sawBuildup{}, sawComplete{}, sawPacking{};
    std::array<bool,2> sawDefensePreview{},sawDefenseBuildup{},sawDefenseComplete{},sawDefenseFire{};
    bool sawDefenseOffline=false,sawDefenseRestored=false;
    std::array<bool,2> sawServiceRepair{},sawServiceComplete{},sawServiceSale{};
    bool sawRepairCancel=false,sawActionRightCancel=false,sawActionEscapeCancel=false;
    while (running) {
      const auto counter = SDL_GetPerformanceCounter();
      const double elapsed = static_cast<double>(counter - lastCounter) * 1000000.0 / SDL_GetPerformanceFrequency();
      lastCounter = counter;
      fractionalMicroseconds += elapsed;
      const auto wholeMicroseconds = static_cast<std::uint64_t>(fractionalMicroseconds);
      fractionalMicroseconds -= static_cast<double>(wholeMicroseconds);
      const bool unlimited = !ra2::targetGameFrameRate(world.gameSpeed()) && !simulationSmokeTest && !miningSmokeTest && !tankSmokeTest && !buildingSmokeTest && !selectionSmokeTest && !infantrySmokeTest;
      // Hidden graphics regressions advance explicit game frames. Waiting on
      // macOS' display link can hang them when the display sleeps.
      const bool skipVsync=unlimited || smokeTest;
      if (skipVsync == swapIntervalEnabled) {
        SDL_GL_SetSwapInterval(skipVsync ? 0 : 1);
        swapIntervalEnabled = !skipVsync;
        nextUnlimitedRenderCounter = counter;
      }
      if(defenseSmokeTest) {
        // Arrange outages before snapshots/assets are prepared for this frame.
        if(world.tick()==109 && !world.spawn("GAWEAT",{23,10}))throw std::runtime_error("Defense outage setup failed");
        if(world.tick()==149 && !world.spawn("NANRCT",{24,15}))throw std::runtime_error("Defense power restoration setup failed");
      }
      if(teslaUnitsSmokeTest) {
        if(world.tick()==79 && !world.spawn("GAWEAT",{22,10}))throw std::runtime_error("Tesla outage setup failed");
        if(world.tick()==100)debugPanelState.style.theater=TheaterStyle::Snow;
        if(world.tick()==200)debugPanelState.style.theater=TheaterStyle::Urban;
      }
      if(radiationV3SmokeTest) {
        if(world.tick()==200)debugPanelState.style.theater=TheaterStyle::Snow;
        if(world.tick()==400)debugPanelState.style.theater=TheaterStyle::Urban;
      }
      if(aircraftSmokeTest) {
        if(world.tick()==30) {
          for(const auto& e:world.snapshot()){if(e.type=="ORCA")airPractice[1]=e.id;if(e.type=="BEAG")airPractice[2]=e.id;}
          if(!airPractice[1] || !airPractice[2])throw std::runtime_error("Airport did not produce both fighters");
          const auto view=ra2::makeWorldView(world,definitions,selection.ids());for(const auto& u:view.vehicles)if(u.entityId==airPractice[1]) {
            const auto base=mapOriginForViewport(viewport);SDL_Event event{};event.type=SDL_MOUSEBUTTONDOWN;event.button.button=SDL_BUTTON_LEFT;
            event.button.x=int(base.x+cameraOffset.x+(u.tilePosition.x-u.tilePosition.y)*DemoAppConfig::kTileWidth*.5f);event.button.y=int(base.y+cameraOffset.y+(u.tilePosition.x+u.tilePosition.y)*DemoAppConfig::kTileHeight*.5f-u.heightLeptons*DemoAppConfig::kTileHeight/256-6);
            SDL_PushEvent(&event);event.type=SDL_MOUSEBUTTONUP;SDL_PushEvent(&event);
          }
        }
        if(world.tick()==40){send(ra2::CommandKind::AttackEntity,airPractice[1],{},{},airPractice[6]);send(ra2::CommandKind::AttackEntity,airPractice[2],{},{},airPractice[6]);send(ra2::CommandKind::AttackEntity,airPractice[4],{},{},airPractice[6]);}
        if(world.tick()==180)send(ra2::CommandKind::Move,airPractice[3],{12,7});
        if(world.tick()==350)send(ra2::CommandKind::Unload,airPractice[3]);
      }
      if(navalSpecialSmokeTest) {
        if(world.tick()==35){send(ra2::CommandKind::AttackEntity,specialNavy[0],{},{},specialNavy[5]);send(ra2::CommandKind::AttackEntity,specialNavy[1],{},{},specialNavy[8]);send(ra2::CommandKind::AttackEntity,specialNavy[2],{},{},specialNavy[7]);send(ra2::CommandKind::AttackEntity,specialNavy[4],{},{},specialNavy[6]);}
        if(world.tick()==100)send(ra2::CommandKind::AttackEntity,specialNavy[3],{},{},specialNavy[6]);
        if(world.tick()==42){
          const auto view=ra2::makeWorldView(world,definitions,selection.ids());for(const auto& u:view.infantry)if(u.id==specialNavy[3]){
            const auto base=mapOriginForViewport(viewport);SDL_Event event{};event.type=SDL_MOUSEBUTTONDOWN;event.button.button=SDL_BUTTON_LEFT;
            event.button.x=int(base.x+cameraOffset.x+(u.position.x-u.position.y)*DemoAppConfig::kTileWidth*.5f);event.button.y=int(base.y+cameraOffset.y+(u.position.x+u.position.y)*DemoAppConfig::kTileHeight*.5f-8);SDL_PushEvent(&event);event.type=SDL_MOUSEBUTTONUP;SDL_PushEvent(&event);
          }
        }
      }
      if(navalSmokeTest) {
        if(world.tick()==0){send(ra2::CommandKind::AttackEntity,navy[6],{}, {},navy[9]);send(ra2::CommandKind::Produce,navy[0],{},"LCRF");}
        if(world.tick()==40)send(ra2::CommandKind::AttackEntity,navy[4],{}, {},navy[8]);
        if(world.tick()==150)send(ra2::CommandKind::Move,navy[2],{30,10});
        if(world.tick()==240)send(ra2::CommandKind::Unload,navy[2]);
        if(world.tick()==280)send(ra2::CommandKind::Move,navy[2],{21,10});
        if(world.tick()==400)send(ra2::CommandKind::Unload,navy[2]);
        if(world.tick()==180)send(ra2::CommandKind::AttackEntity,navy[5],{}, {},navy[9]);
        if(world.tick()==2 || world.tick()==3 || world.tick()==5 || world.tick()==6) {
          auto inputOrigin=mapOriginForViewport(viewport);inputOrigin.x+=cameraOffset.x;inputOrigin.y+=cameraOffset.y;
          const auto id=world.tick()<5?navy[7]:navy[2];for(const auto& e:world.snapshot())if(e.id==id) {
            const int x=int(inputOrigin.x+(e.position.x-e.position.y)*60/2000.f),y=int(inputOrigin.y+(e.position.x+e.position.y)*30/2000.f-8);
            SDL_Event move{};move.type=SDL_MOUSEMOTION;move.motion.x=x;move.motion.y=y;SDL_PushEvent(&move);SDL_Event click{};click.type=(world.tick()==2 || world.tick()==5)?SDL_MOUSEBUTTONDOWN:SDL_MOUSEBUTTONUP;click.button.button=SDL_BUTTON_LEFT;click.button.x=x;click.button.y=y;SDL_PushEvent(&click);
          }
        }
        for(const auto& e:world.snapshot()) {
          if(e.id==navy[7]){sawNavalBoard|=e.containedBy==navy[2];sawNavalUnload|=world.tick()>400 && !e.containedBy && e.cell.x<24;}
          if(e.id==navy[2])sawNavalDeepHold|=world.tick()>240 && world.tick()<280 && e.cargo && !e.cargo->passengers.empty();
          if(e.id==navy[4])sawSubmerged|=e.submerged;
          if(e.type=="LCRF" && e.id!=navy[2])sawNavalProduction=true;
        }
        for(const auto& shot:world.shots()){sawTorpedo|=shot.weaponId=="SUBTORPEDO";sawMedusa|=shot.weaponId=="MEDUSA";sawNavalFlak|=shot.weaponId=="FLAKWEAPON";}
      }
      if(ivanApocalypseSmokeTest && (world.tick()==98 || world.tick()==100 || world.tick()==105)) {
        for(const auto& e:world.snapshot())if(e.id==(world.tick()==105?ivanDefuseTarget:ivanEngineer)) {
          const auto origin=mapOriginForViewport(viewport);const int x=int(origin.x+(e.position.x-e.position.y)*DemoAppConfig::kTileWidth/2000.0),y=int(origin.y+(e.position.x+e.position.y)*DemoAppConfig::kTileHeight/2000.0-10);
          SDL_Event down{};down.type=SDL_MOUSEBUTTONDOWN;down.button.button=world.tick()==98?SDL_BUTTON_RIGHT:SDL_BUTTON_LEFT;down.button.x=x;down.button.y=y;SDL_PushEvent(&down);auto up=down;up.type=SDL_MOUSEBUTTONUP;SDL_PushEvent(&up);
        }
      }
      if(demolitionSmokeTest && world.tick()==20) {
        const auto view=ra2::makeWorldView(world,definitions,selection.ids());
        for(const auto& sprite:view.infantry)if(sprite.id==demolitionSelection) {
          const auto origin=mapOriginForViewport(viewport);
          const int x=int(origin.x+(sprite.position.x-sprite.position.y)*DemoAppConfig::kTileWidth*.5f),y=int(origin.y+(sprite.position.x+sprite.position.y)*DemoAppConfig::kTileHeight*.5f-10);
          SDL_Event down{};down.type=SDL_MOUSEBUTTONDOWN;down.button.button=SDL_BUTTON_LEFT;down.button.x=x;down.button.y=y;SDL_PushEvent(&down);
          auto up=down;up.type=SDL_MOUSEBUTTONUP;SDL_PushEvent(&up);
        }
      }
      if(dogDroneSmokeTest && world.tick()==50) {
        const auto view=ra2::makeWorldView(world,definitions,selection.ids());
        for(const auto& sprite:view.infantry)if(sprite.id==leapActors[1]) {
          const auto origin=mapOriginForViewport(viewport);
          const int x=int(origin.x+(sprite.position.x-sprite.position.y)*DemoAppConfig::kTileWidth*.5f);
          const int y=int(origin.y+(sprite.position.x+sprite.position.y)*DemoAppConfig::kTileHeight*.5f-8);
          SDL_Event down{};down.type=SDL_MOUSEBUTTONDOWN;down.button.button=SDL_BUTTON_LEFT;down.button.x=x;down.button.y=y;SDL_PushEvent(&down);
          auto up=down;up.type=SDL_MOUSEBUTTONUP;SDL_PushEvent(&up);
        }
      }
      if (aircraftSmokeTest || navalSpecialSmokeTest || navalSmokeTest || ivanApocalypseSmokeTest || demolitionSmokeTest || dogDroneSmokeTest || buildingSmokeTest || tankSmokeTest || selectionSmokeTest || infantrySmokeTest || defenseSmokeTest || buildingActionsSmokeTest || teslaUnitsSmokeTest || radiationV3SmokeTest) world.step();
      else if (simulationSmokeTest || miningSmokeTest) world.advance(1000000);
      else world.advance(gameSpeedSmokeTest ? 100000 : wholeMicroseconds, unlimited ? 8 : 120);
      metricsMicroseconds += (simulationSmokeTest || miningSmokeTest) ? 1000000 : elapsed;
      if (metricsMicroseconds >= 500000) {
        measuredGameFps = (world.tick() - metricsStartFrame) * 1000000.0 / metricsMicroseconds;
        metricsStartFrame = world.tick(); metricsMicroseconds = 0;
      }
      // In unlimited mode, spend available CPU time on small simulation batches.
      // Only rendering/input uses a 60 Hz deadline; there is no logic FPS cap.
      if (unlimited && counter < nextUnlimitedRenderCounter) continue;
      if (unlimited) nextUnlimitedRenderCounter = counter + SDL_GetPerformanceFrequency() / 60;
      const auto nowTicks = ra2::gameMilliseconds(world.tick());
      if (buildingSmokeTest && world.tick()==2) selection.selectOne(testMcv);
      if(dogDroneSmokeTest && world.tick()>52)sawDogDroneClick|=selection.primary()==leapActors[1];
      selection.prune(world.snapshot());
      Vec2 mapOrigin = mapOriginForViewport(viewport);mapOrigin.x+=cameraOffset.x;mapOrigin.y+=cameraOffset.y;
      presentation = ra2::makeWorldView(world, definitions, selection.ids());
      const auto& buildings = presentation.buildings;
      const auto& rhinoTanks = presentation.vehicles;
      const auto& projectiles = presentation.projectiles;
      if(aircraftSmokeTest) {
        int missiles=0;for(const auto& shot:world.shots()){missiles+=shot.sourceType=="ORCA";sawAirBomb|=shot.sourceType=="ZEP" && shot.image=="ZBOMB";}
        sawAirSalvo|=missiles>=2;sawPlaneClick|=world.tick()>31 && world.tick()<40 && selection.primary()==airPractice[1];
        for(const auto& e:world.snapshot()) {
          if(e.id==airPractice[1] && e.aircraft){sawAirReturn|=e.aircraft->ammo==0;sawAirRearm|=sawAirReturn && e.aircraft->mission==ra2::AircraftMission::Landed && e.aircraft->ammo==1;}
          if(e.id==airPractice[5]){sawAirBoard|=e.containedBy==airPractice[3];sawAirUnload|=sawAirBoard && !e.containedBy;}
        }
      }
      if(navalSpecialSmokeTest) {
        int hornets=0,missiles=0;for(const auto& e:world.snapshot()) {
          hornets+=e.type=="HORNET";missiles+=e.type=="DMISL";sawAsw|=e.type=="ASW";
          if(e.id==specialNavy[6]){sawGrapple|=e.grappled;sawRelease|=sawGrapple && !e.grappled;}
          if(e.id==specialNavy[1])for(const auto& slot:e.spawns)sawReload|=slot.phase==ra2::NavalSpawnPhase::Reloading;
        }
        sawHornets|=hornets>=3;sawDreadMissiles|=missiles>=2;sawNavyClick|=world.tick()>43 && selection.primary()==specialNavy[3];
        for(const auto& b:presentation.beams)sawSonic|=definitions.weapon(b.weapon).sonic;
      }
      if(ivanApocalypseSmokeTest) {
        sawIvanSelection|=world.tick()>102 && selection.primary()==ivanEngineer;
        for(const auto& e:world.snapshot()) {
          if(e.id==ivanTimedTarget)sawIvanAttach|=e.bomb.has_value();
          if(e.id==ivanDefuseTarget){sawIvanDisarmAttach|=e.bomb.has_value();sawIvanDisarm|=world.tick()>120 && sawIvanDisarmAttach && !e.bomb && e.hp==e.maxHp;}
          if(e.id==ivanIfv && e.cargo)sawIvanIfv|=e.cargo->mode==7 && e.cargo->turret==3;
          if(e.id==ivanApoc){sawApocHeal|=apocLowestHp<400 && e.hp>apocLowestHp;apocLowestHp=std::min(apocLowestHp,e.hp);}
        }
        for(const auto& b:presentation.bombs)if(b.target==ivanTimedTarget)ivanIconFrames|=1u<<b.frame;
        for(const auto& s:world.shots())if(s.sourceType=="APOC")ivanWeapons|=s.weaponId=="120MMX"?1:s.weaponId=="MAMMOTHTUSK"?2:0;
        for(const auto& e:world.effects()) {
          sawApocMuzzle|=e.sourceType=="APOC" && e.animation=="APMUZZLE";sawApocImpact|=e.sourceType=="APOC" && e.animation=="APOCEXP";
          sawIvanIfvBlast|=e.sourceType=="FV" && e.animation=="MININUKE";
          if(e.sourceType=="IVAN" && e.animation=="CRIVEXP" && !ivanBlastFrame)ivanBlastFrame=e.started;
        }
      }
      if(demolitionSmokeTest) {
        sawDemolitionClick|=world.tick()>22 && selection.primary()==demolitionSelection;
        for(const auto& e:world.snapshot())if(e.id==demolitionIfv && e.cargo)sawDemolitionMode|=e.cargo->mode==11 && e.cargo->turret==3;
        for(const auto& e:world.effects())if(e.explosion) {
          if(e.sourceType=="TERROR" && e.animation=="MININUKE")demolitionExplosions|=1;
          if(e.sourceType=="DTRUCK" && e.animation=="DEMTEXP")demolitionExplosions|=2;
          if(e.sourceType=="FV" && e.animation=="MININUKE")demolitionExplosions|=4;
        }
        sawDemolitionRadiation|=world.radiationLevel({5,11})==100 && world.radiationLight({5,11})>0;
      }
      if(dogDroneSmokeTest) {
        for(const auto& e:world.snapshot()) {
          for(int i=0;i<3;++i)if(e.id==leapActors[i] && e.leaping)leapSeen|=1u<<i;
          if(e.id==hostDrone) {
            sawInfestation|=e.parasiteHost==droneHost;
            sawDroneReturn|=world.tick()>130 && !e.leaping && !e.parasiteHost;
            if(e.parasiteHost && std::any_of(presentation.infantry.begin(),presentation.infantry.end(),[&](const auto& sprite){return sprite.id==hostDrone;}))throw std::runtime_error("Infesting drone remained visible");
          }
        }
      }
      if(radiationV3SmokeTest) {
        radiationTheaters|=1u<<static_cast<int>(debugPanelState.style.theater);sawRadSite|=!world.radiationSites().empty();
        for(const auto& e:world.snapshot()) {
          if(e.id==radiationWorker && e.infantry){sawRadDeploy|=e.infantry->posture==ra2::InfantryPosture::Deployed;sawRadMove|=world.tick()>100 && e.activity==ra2::Activity::Moving && e.infantry->posture==ra2::InfantryPosture::Standing;}
          if(e.id==v3Impact)v3BodyStates|=e.launcherEmpty?2u:1u;
          sawV3Missile|=e.missile && !e.takingOff;
        }
        for(const auto& b:presentation.beams){sawRadiationBeam|=b.weapon=="RADBEAMWEAPON";sawRadiationIfvBeam|=b.weapon=="CRRADBEAMWEAPON";}
        for(const auto& e:presentation.effects)sawRadDeath|=e.animation=="NUKEDIE";
        for(const auto& shot:world.shots())sawV3Intercept|=shot.weaponId=="FLAKGUYAAGUN";
        if(world.tick()==1 || world.tick()==3 || world.tick()==4) {
          const auto p=isoToScreen(4,12,mapOrigin,60,30);SDL_Event e{};e.type=world.tick()==1?SDL_MOUSEMOTION:world.tick()==3?SDL_MOUSEBUTTONDOWN:SDL_MOUSEBUTTONUP;
          if(world.tick()==1){e.motion.x=int(p.x);e.motion.y=int(p.y-5);}else{e.button.button=SDL_BUTTON_LEFT;e.button.state=world.tick()==3?SDL_PRESSED:SDL_RELEASED;e.button.x=int(p.x);e.button.y=int(p.y-5);}SDL_PushEvent(&e);
        }
        if(world.tick()==10){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_d;e.key.state=SDL_PRESSED;SDL_PushEvent(&e);}
        if(world.tick()==89 || world.tick()==90 || world.tick()==91) {
          const auto p=isoToScreen(12,12,mapOrigin,60,30);SDL_Event e{};e.type=world.tick()==89?SDL_MOUSEMOTION:world.tick()==90?SDL_MOUSEBUTTONDOWN:SDL_MOUSEBUTTONUP;
          if(world.tick()==89){e.motion.x=int(p.x);e.motion.y=int(p.y);}else{e.button.button=SDL_BUTTON_LEFT;e.button.state=world.tick()==90?SDL_PRESSED:SDL_RELEASED;e.button.x=int(p.x);e.button.y=int(p.y);}SDL_PushEvent(&e);
        }
      }
      if(teslaUnitsSmokeTest) {
        teslaTheaters|=1u<<static_cast<int>(debugPanelState.style.theater);
        for(const auto& beam:presentation.beams) {
          constexpr std::array names{"ELECTRICBOLT","TANKBOLT","ASSAULTBOLT","COILBOLT","OPCOILBOLT","CRELECTRICBOLT"};
          for(std::size_t i=0;i<names.size();++i)if(beam.weapon==names[i])teslaWeapons|=1u<<i;
        }
        for(const auto& e:presentation.effects)if(e.animation=="ELECTRO") {
          if(!weaponVisualAssets.animations.contains("ELECTRO") || e.animationFrame>=int(weaponVisualAssets.animations.at("ELECTRO").size()))throw std::runtime_error("Electrocution frame/asset out of bounds");
          sawElectro=true;
        }
        for(const auto& e:world.snapshot()) {
          if(e.id==teslaCoil && e.coilChargers>0)sawTeslaClick=true;
          if(e.id==teslaIfv && e.cargo){sawTeslaIfv|=e.cargo->mode==6 && e.cargo->turret==3;sawTeslaUnload|=world.tick()>230 && e.cargo->used==0;}
        }
        if(world.tick()==1 || world.tick()==3 || world.tick()==4) {
          const auto point=isoToScreen(5,5,mapOrigin,60,30);SDL_Event event{};
          event.type=world.tick()==1?SDL_MOUSEMOTION:world.tick()==3?SDL_MOUSEBUTTONDOWN:SDL_MOUSEBUTTONUP;
          if(world.tick()==1){event.motion.x=int(point.x);event.motion.y=int(point.y-12);}
          else{event.button.button=SDL_BUTTON_LEFT;event.button.state=world.tick()==3?SDL_PRESSED:SDL_RELEASED;event.button.x=int(point.x);event.button.y=int(point.y-12);}
          SDL_PushEvent(&event);
        }
      }
      const auto& combatEffects = presentation.effects;
      if(buildingActionsSmokeTest) {
        for(const auto& entity:world.snapshot())for(int target=0;target<2;++target)if(entity.id==serviceTargets[target]) {
          sawServiceRepair[target]|=entity.repairing;
          sawServiceComplete[target]|=world.tick()>30 && entity.hp==entity.maxHp && !entity.repairing;
          sawServiceSale[target]|=entity.activity==ra2::Activity::Selling;
          if(!target && world.tick()==14)sawRepairCancel=!entity.repairing && entity.hp<entity.maxHp;
        }
        if(world.tick()==27)sawActionRightCancel=!sidebarState.repairSelected && !sidebarState.sellSelected;
        if(world.tick()==34)sawActionEscapeCancel=!sidebarState.repairSelected && !sidebarState.sellSelected;
        if(world.tick()==200)serviceMoneyBeforeSale=world.credits();
      }
      if(defenseSmokeTest) {
        for(int tower=0;tower<2;++tower) {
          const auto id=tower==0?"ATESLA":"TESLA";
          sawDefensePreview[tower]|=selectedBuildAsset && selectedBuildAsset->id==id;
          for(const auto& building:buildings)if(building.assetId==id) {
            sawDefenseBuildup[tower]|=building.state==BuildingState::Constructing;
            sawDefenseComplete[tower]|=building.state==BuildingState::Complete;
            sawDefenseOffline|=!building.powered;
            sawDefenseRestored|=world.tick()>150 && building.powered;
          }
          for(const auto& beam:presentation.beams)sawDefenseFire[tower]|=beam.weapon==(tower==0?"PRISMSHOT":"COILBOLT");
        }
      }
      if(infantrySmokeTest)for(const auto& entity:world.snapshot())if(entity.infantry) {
        sawInfantryDeployed|=entity.infantry->posture==ra2::InfantryPosture::Deployed;
        sawInfantryWalk|=entity.infantry->sequence=="WALK";
        sawInfantryFire|=entity.infantry->sequence=="FIREUP" || entity.infantry->sequence=="DEPLOYEDFIRE";
      }
    if(supportInfantrySmokeTest) {
        for(const auto& entity:world.snapshot()) {
          sawEngineerEntry|=entity.buildingInteraction && entity.buildingInteraction->entering;
          sawDamagedRepair|=entity.id==supportRepair && entity.hp<entity.maxHp;
        }
        for(const auto& shot:world.shots())sawSniperShot|=shot.sourceType=="SNIPE";
      }
      if(airDefenseSmokeTest) {
        for(const auto& entity:world.snapshot())if(entity.id==airSmokeRock && entity.infantry) {
          sawAirFlight|=entity.infantry->sequence=="FLY" && std::any_of(presentation.infantry.begin(),presentation.infantry.end(),[&](const auto& unit){return unit.id==entity.id && unit.airborne && !unit.corpse;});
          sawAirHover|=entity.infantry->sequence=="HOVER" && entity.heightLeptons>=500;
          sawAirFire|=entity.infantry->sequence=="FIREFLY";
          const int phase=static_cast<int>(world.tick())-95;
          if(phase==0 || phase==2 || phase==3) {
            if(phase==2)selection.clear();
            const Vec2 point{mapOrigin.x+(entity.position.x-entity.position.y)*DemoAppConfig::kTileWidth/2000.f,
                             mapOrigin.y+(entity.position.x+entity.position.y)*DemoAppConfig::kTileHeight/2000.f};
            const auto y=static_cast<int>(point.y-entity.heightLeptons*DemoAppConfig::kTileHeight/256.f-12);
            if(phase==0)airSmokeClick={point.x,float(y)};
            SDL_Event event{};event.type=phase==0?SDL_MOUSEMOTION:phase==2?SDL_MOUSEBUTTONDOWN:SDL_MOUSEBUTTONUP;
            if(phase==0){event.motion.x=static_cast<int>(airSmokeClick.x);event.motion.y=static_cast<int>(airSmokeClick.y);}
            else{event.button.button=SDL_BUTTON_LEFT;event.button.state=phase==2?SDL_PRESSED:SDL_RELEASED;event.button.x=static_cast<int>(airSmokeClick.x);event.button.y=static_cast<int>(airSmokeClick.y);}
            SDL_PushEvent(&event);
          }
        }
        if(world.tick()==100)sawAirClick=selection.contains(airSmokeRock);
        for(const auto& shot:world.shots()){sawFlakGround|=shot.weaponId=="FLAKGUYGUN";sawFlakAir|=shot.weaponId=="FLAKGUYAAGUN";}
        sawAirDeath|=!world.infantryCorpses().empty();
      }
      if(specialVehicleSmokeTest) {
        for(const auto& entity:world.snapshot()) {
          if(entity.id==specialPrism)chargeStages|=1u<<entity.chargeTurret;
          if(entity.id==specialMirage) {
            sawMirageMove|=!entity.disguised && entity.activity==ra2::Activity::Moving;
            sawMirageFire|=entity.disguised && world.tick()<entity.disguiseFireUntil;
            const int phase=int(world.tick())-10;
            if(phase==0 || phase==2 || phase==3) {
              if(phase==2)selection.clear();
              const auto point=isoToScreen(entity.cell.x,entity.cell.y,mapOrigin,60,30);
              SDL_Event event{};event.type=phase==0?SDL_MOUSEMOTION:phase==2?SDL_MOUSEBUTTONDOWN:SDL_MOUSEBUTTONUP;
              if(phase==0){event.motion.x=int(point.x);event.motion.y=int(point.y-5);}
              else{event.button.button=SDL_BUTTON_LEFT;event.button.state=phase==2?SDL_PRESSED:SDL_RELEASED;event.button.x=int(point.x);event.button.y=int(point.y-5);}
              SDL_PushEvent(&event);
            }
          }
        }
        if(world.tick()==15)sawMirageClick=selection.contains(specialMirage);
        for(const auto& vehicle:presentation.vehicles)if(vehicle.entityId==specialEnemyMirage)sawMirageTree|=vehicle.disguiseVisible && !vehicle.disguiseTargetable;
        for(const auto& effect:presentation.effects)sawSabotExplosion|=effect.animation=="S_CLSN30" && weaponVisualAssets.animations.contains(effect.animation);
        for(const auto& beam:presentation.beams){sawPrismBeam|=beam.weapon=="COMET";sawPrismFragments|=beam.weapon=="COMETFRAGMENT";}
      }
      if(transportSmokeTest) {
        for(const auto& entity:world.snapshot()) {
          if(entity.id==transportIfv && entity.cargo)transportModes|=1u<<entity.cargo->mode;
          if(entity.containedBy)sawTransportHidden|=std::none_of(presentation.selectables.begin(),presentation.selectables.end(),[&](const auto& unit){return unit.id==entity.id;});
          sawGarrison|=entity.id==transportHouse && entity.cargo && entity.cargo->used==2;
          if(entity.id==transportRepairTarget){sawIfvDamage|=entity.hp<entity.maxHp;sawIfvRepair|=sawIfvDamage && entity.hp==entity.maxHp;}
        }
        for(const auto& shot:world.shots()){sawGarrisonFire|=shot.sourceType=="CAHSE01";sawMissile|=shot.weaponId=="HOVERMISSILE" && !shot.invisible;}
        const auto clickFrame=world.tick()%1000;
        if(clickFrame==95 || clickFrame==97 || clickFrame==98 || clickFrame==235 || clickFrame==237 || clickFrame==238 ||
            clickFrame==355 || clickFrame==357 || clickFrame==358 || clickFrame==605 || clickFrame==607 || clickFrame==608) {
          const auto target=clickFrame>=605?transportHouse:clickFrame>=235 && clickFrame<=238?transportTrack:transportIfv;
          const int phase=clickFrame>=605?clickFrame-605:clickFrame>=355?clickFrame-355:clickFrame>=235?clickFrame-235:clickFrame-95;
          if(phase==2)selection.clear();
          for(const auto& entity:world.snapshot())if(entity.id==target) {
            const auto point=isoToScreen(entity.cell.x,entity.cell.y,mapOrigin,DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight);
            SDL_Event click{};click.type=phase==0?SDL_MOUSEMOTION:phase==2?SDL_MOUSEBUTTONDOWN:SDL_MOUSEBUTTONUP;
            if(phase==0){click.motion.x=static_cast<int>(point.x);click.motion.y=static_cast<int>(point.y-6);}
            else {click.button.button=SDL_BUTTON_LEFT;click.button.state=phase==2?SDL_PRESSED:SDL_RELEASED;click.button.x=static_cast<int>(point.x);click.button.y=static_cast<int>(point.y-6);}
            SDL_PushEvent(&click);
          }
        }
        if(world.tick()==100 || world.tick()==240 || world.tick()==360 || world.tick()==610) {
          SDL_Event event{};event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_u;SDL_PushEvent(&event);
        }
        if(world.tick()==102 || world.tick()==242 || world.tick()==362 || world.tick()==612) {
          SDL_Event event{};event.type=SDL_KEYUP;event.key.keysym.sym=SDLK_u;SDL_PushEvent(&event);
        }
      }
      if (tankSmokeTest) {
        for (const auto& entity:world.snapshot())
          sawMovingTurret=sawMovingTurret || (entity.position!=entity.previous && entity.turretHeading!=entity.heading);
        sawShell=sawShell || !projectiles.empty();
        for (const auto& effect:combatEffects) sawExplosion=sawExplosion || effect.kind==CombatEffectKind::Explosion;
      }
      if (miningSmokeTest) for(const auto& entity:world.snapshot()) if(entity.harvesting) {
        if (!selection.primary()) selection.selectOne(entity.id);
        sawMiningCargo=sawMiningCargo || entity.harvesting->cargo>0;
        sawMiningReturn=sawMiningReturn || entity.harvesting->phase==ra2::HarvestPhase::Unloading;
      }
      if (simulationSmokeTest) for (const auto& entity : world.snapshot())
        if (entity.id == 4 && entity.hp < entity.maxHp) sawCombatDamage = true;
      if (simulationSmokeTest) for (const auto& vehicle : rhinoTanks) {
        if (!vehicle.exitFactoryId) continue;
        const auto parent = std::find_if(buildings.begin(), buildings.end(), [&](const auto& b) { return b.entityId == vehicle.exitFactoryId; });
        if (parent == buildings.end() || !ra2::isFactoryExitVehicle(vehicle, *parent))
          throw std::runtime_error("Factory presentation link lost");
        if (world.structureAt(vehicle.occupiedCell) == vehicle.exitFactoryId) sawInternalExit = true;
      }
      for (const auto& building : buildings) {
        (void)ensureCachedAssetForStyle(buildingAssetCache, paths.buildingRoot, artIni, paletteLibrary, houseColors, debugPanelState.style, building.assetId);
        if (buildingSmokeTest && (building.entityId==testMcv || building.entityId==testSovietMcv)) {
          const auto index = building.entityId==testMcv ? 0 : 1;
          if (activeBuildingAssets->at(building.assetId).buildupTextures.empty()) throw std::runtime_error("Missing construction yard buildup SHP");
          sawBuildup[index] = sawBuildup[index] || building.state==BuildingState::Constructing;
          sawComplete[index] = sawComplete[index] || building.state==BuildingState::Complete;
          sawPacking[index] = sawPacking[index] || building.state==BuildingState::Packing;
        }
      }
      const auto basePower=world.power();sidebarState.powerProduced=basePower.produced;sidebarState.powerConsumed=basePower.consumed;
      sidebarState.powerOutage=basePower.outageUntil!=0;sidebarState.credits=world.credits();
      updateSidebarStateForDemo(sidebarState, sidebarAssets, viewport.height, SDL_GetTicks());
      updateSidebarAvailability(sidebarAssets,definitions);
      const auto clampWorldPoint=[&](float x,float y) {
        return Vec2{std::clamp(x,0.0f,float(viewport.width)-DemoAppConfig::kSidebarWidth-1),
                    std::clamp(y,0.0f,float(viewport.height)-1)};
      };
      if(buildingActionsSmokeTest) {
        const auto frame=world.tick();
        const auto event=[&](Uint32 kind,Vec2 point={},Uint8 button=SDL_BUTTON_LEFT) {
          SDL_Event input{};input.type=kind;
          if(kind==SDL_KEYDOWN)input.key.keysym.sym=SDLK_ESCAPE;
          else if(kind==SDL_MOUSEMOTION){input.motion.x=int(point.x);input.motion.y=int(point.y);}
          else {input.button.button=button;input.button.x=int(point.x);input.button.y=int(point.y);}
          if(SDL_PushEvent(&input)<0)throw std::runtime_error(SDL_GetError());
        };
        const float left=viewport.width-DemoAppConfig::kSidebarWidth;
        const float sideY=sidebarAssets.credits.height+sidebarAssets.top.height+sidebarAssets.radar.height;
        const Vec2 repair{left+(sidebarAssets.repairFrames[0].width<=52?32.f:20.f)+sidebarAssets.repairFrames[0].width/2.f,sideY+8+sidebarAssets.repairFrames[0].height/2.f};
        const Vec2 sell{left+84+sidebarAssets.sellFrames[0].width/2.f,sideY+8+sidebarAssets.sellFrames[0].height/2.f};
        const auto click=[&](int start,Vec2 point) {
          if(frame==static_cast<ra2::Tick>(start))event(SDL_MOUSEBUTTONDOWN,point);
          if(frame==static_cast<ra2::Tick>(start+1))event(SDL_MOUSEBUTTONUP,point);
        };
        click(5,repair);click(30,repair);click(200,sell);
        const auto a=isoToScreen(18,14,mapOrigin,60,30),b=isoToScreen(15,14,mapOrigin,60,30);
        click(8,a);click(12,a);click(16,a);click(20,b);click(204,b);click(210,a);
        if(frame==25 || frame==215)event(SDL_MOUSEBUTTONDOWN,a,SDL_BUTTON_RIGHT);
        if(frame==32)event(SDL_KEYDOWN);
      }
      if(defenseSmokeTest) {
        const auto frame=world.tick();
        const auto mouseEvent=[&](Uint32 kind,Vec2 point) {
          SDL_Event input{};input.type=kind;
          if(kind==SDL_MOUSEMOTION){input.motion.x=int(point.x);input.motion.y=int(point.y);}
          else {input.button.button=SDL_BUTTON_LEFT;input.button.x=int(point.x);input.button.y=int(point.y);}
          if(SDL_PushEvent(&input)<0)throw std::runtime_error(SDL_GetError());
        };
        const int panelY=sidebarAssets.credits.height+sidebarAssets.top.height+sidebarAssets.radar.height+sidebarAssets.side1.height;
        if(frame==1 || frame==2) {
          const bool wide=sidebarAssets.tabFrames[0][0].width>=32;
          mouseEvent(frame==1?SDL_MOUSEBUTTONDOWN:SDL_MOUSEBUTTONUP,
            {float(viewport.width)-DemoAppConfig::kSidebarWidth+(wide?52.f:55.f)+sidebarAssets.tabFrames[1][0].width/2.f,
             float(panelY-sidebarAssets.side1.height+40)+sidebarAssets.tabFrames[1][0].height/2.f});
        }
        for(int tower=0;tower<2;++tower) {
          const auto start=4+tower*6;
          const Vec2 icon{float(viewport.width)-DemoAppConfig::kSidebarWidth+(tower?115.f:52.f),panelY+3*50.f+24};
          const auto cell=TileCoord{14+tower*2,12};const auto target=isoToScreen(cell.x,cell.y,mapOrigin,60,30);
          if(frame==static_cast<ra2::Tick>(start))mouseEvent(SDL_MOUSEBUTTONDOWN,icon);
          if(frame==static_cast<ra2::Tick>(start+1))mouseEvent(SDL_MOUSEBUTTONUP,icon);
          if(frame==static_cast<ra2::Tick>(start+2))mouseEvent(SDL_MOUSEMOTION,target);
          if(frame==static_cast<ra2::Tick>(start+3))mouseEvent(SDL_MOUSEBUTTONDOWN,target);
          if(frame==static_cast<ra2::Tick>(start+4))mouseEvent(SDL_MOUSEBUTTONUP,target);
        }
      }
      if(selectionSmokeTest) {
        const auto origin=mapOriginForViewport(viewport);
        const Vec2 first{origin.x+(7-10)*30,origin.y+(7+10)*15};
        const Vec2 second{origin.x+(8-10)*30,origin.y+(8+10)*15};
        const Vec2 start{first.x-30,first.y-35},end{second.x+30,second.y+25};
        const Vec2 goal{origin.x+(10-8)*30,origin.y+(10+8)*15};
        const auto mouseEvent=[&](Uint32 type,Vec2 point) {
          SDL_Event input{}; input.type=type;
          if(type==SDL_MOUSEMOTION) { input.motion.x=int(point.x); input.motion.y=int(point.y); input.motion.state=SDL_BUTTON_LMASK; }
          else { input.button.button=SDL_BUTTON_LEFT; input.button.state=type==SDL_MOUSEBUTTONDOWN?SDL_PRESSED:SDL_RELEASED;
                 input.button.x=int(point.x); input.button.y=int(point.y); }
          if(SDL_PushEvent(&input)<0) throw std::runtime_error(SDL_GetError());
        };
        if(world.tick()==1) mouseEvent(SDL_MOUSEBUTTONDOWN,start);
        if(world.tick()==2) mouseEvent(SDL_MOUSEMOTION,end);
        if(world.tick()==3) mouseEvent(SDL_MOUSEBUTTONUP,end);
        if(world.tick()==4) mouseEvent(SDL_MOUSEBUTTONDOWN,goal);
        if(world.tick()==5) mouseEvent(SDL_MOUSEBUTTONUP,goal);
      }
      if(buildingSmokeTest && world.tick()>=100 && world.tick()<=107) {
        if(world.tick()==100)selection.clear();
        const auto target=world.tick()<=104?testMcv:testSovietMcv;
        const auto found=std::find(presentation.buildingIds.begin(),presentation.buildingIds.end(),target);
        if(found!=presentation.buildingIds.end() && world.tick()!=100) {
          const auto& building=buildings[found-presentation.buildingIds.begin()];
          const auto& frame=activeBuildingAssets->at(building.assetId).completeFrame;
          const auto topLeft=buildingSpriteTopLeft(buildingDrawCenter(building.placement,mapOrigin,60,30),frame.width,frame.height);
          std::optional<Vec2> roof;
          for(int y=0;y<frame.height && !roof;++y)for(int x=0;x<frame.width && !roof;++x)if(frame.indices[y*frame.width+x]) {
            const Vec2 point{topLeft.x+x+.5f,topLeft.y+y+.5f};const auto tile=screenToIso(point.x,point.y,mapOrigin,60,30);
            const auto cells=foundationTiles(building.placement);
            if(point.x<320 || point.x>=viewport.width-DemoAppConfig::kSidebarWidth || point.y<30 || point.y>=viewport.height ||
                std::find(cells.begin(),cells.end(),tile)!=cells.end())continue;
            roof=point;
          }
          if(!roof)throw std::runtime_error("Building smoke needs a visible roof outside its footprint");
          SDL_Event input{};
          if(world.tick()==107) {input.type=SDL_KEYDOWN;input.key.keysym.sym=SDLK_d;}
          else {
            input.type=world.tick()%2?SDL_MOUSEBUTTONDOWN:SDL_MOUSEBUTTONUP;
            input.button.button=SDL_BUTTON_LEFT;input.button.x=int(roof->x);input.button.y=int(roof->y);
          }
          if(SDL_PushEvent(&input)<0)throw std::runtime_error(SDL_GetError());
        }
      }
      SDL_Event event{};
      while (SDL_PollEvent(&event)) {
        processImGuiDebugPanelEvent(event);
        if (event.type == SDL_QUIT) { running = false; continue; }
        if (event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST) {
          selectionGesture.cancel(); SDL_CaptureMouse(SDL_FALSE); continue;
        }
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
          if(selectedBuildAsset || sidebarState.repairSelected || sidebarState.sellSelected) {
            selectedBuildAsset=nullptr;sidebarState.repairSelected=sidebarState.sellSelected=false;selectionGesture.cancel();
          } else running=false;
          continue;
        }
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_d && (event.key.keysym.mod & KMOD_CTRL)) { debugPanelState.visible = !debugPanelState.visible; continue; }
        if (event.type == SDL_KEYDOWN && imguiDebugPanelWantsKeyboard()) continue;
        if (isStopCommandEvent(event)) { sendSelection(ra2::CommandKind::Stop); continue; }
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_h) {
          for(const auto& entity:world.snapshot()) if(selection.contains(entity.id) && entity.owner==0 && entity.harvesting)
            send(ra2::CommandKind::Harvest,entity.id,entity.cell);
          continue;
        }
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_d) { sendSelection(ra2::CommandKind::Deploy); continue; }
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_u) { sendSelection(ra2::CommandKind::Unload); continue; }
        if (event.type == SDL_MOUSEMOTION) {
          selectionGesture.update(clampWorldPoint(float(event.motion.x),float(event.motion.y)));
          if (selectedBuildAsset) updatePreviewPlacementFromMouse(static_cast<float>(event.motion.x), static_cast<float>(event.motion.y),
              static_cast<float>(viewport.width), DemoAppConfig::kSidebarWidth,
              mapOrigin, DemoAppConfig::kTileWidth, DemoAppConfig::kTileHeight, selectedBuildAsset, previewPlacement);
          continue;
        }
        if (event.type!=SDL_MOUSEBUTTONDOWN && event.type!=SDL_MOUSEBUTTONUP) continue;
        const auto mouseX = static_cast<float>(event.button.x), mouseY = static_cast<float>(event.button.y);
        if (event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_RIGHT) {
          selectionGesture.cancel(); SDL_CaptureMouse(SDL_FALSE);
          if (!imguiDebugPanelContainsPoint(debugPanelState,mouseX,mouseY) && !imguiDebugPanelWantsMouse()) {
            selectedBuildAsset=nullptr;sidebarState.repairSelected=sidebarState.sellSelected=false;selection.clear();
          }
          continue;
        }
        if (event.button.button != SDL_BUTTON_LEFT) continue;
        std::optional<ra2::SelectionRelease> release;
        if (event.type==SDL_MOUSEBUTTONUP) {
          release=selectionGesture.release(clampWorldPoint(mouseX,mouseY));
          SDL_CaptureMouse(SDL_FALSE);
          if (!release) continue;
          if (release->dragged) {
            selection.selectBox(release->rectangle,presentation.selectables,mapOrigin,DemoAppConfig::kTileWidth,
                                DemoAppConfig::kTileHeight,release->additive);
            continue;
          }
        }
        if (imguiDebugPanelContainsPoint(debugPanelState, mouseX, mouseY) || imguiDebugPanelWantsMouse()) continue;
        if (event.type==SDL_MOUSEBUTTONDOWN) {
          const auto click = handleSidebarLeftClick(sidebarState, sidebarAssets, static_cast<float>(viewport.width), static_cast<float>(viewport.height), mouseX, mouseY);
          if (click.consumed) {
            applySidebarClick(sidebarState, sidebarAssets, click);
            if(click.action==SidebarClickAction::ToggleRepair || click.action==SidebarClickAction::ToggleSell) {
              selectedBuildAsset=nullptr;selectionGesture.cancel();selection.clear();
            }
            if (click.action != SidebarClickAction::ClickIcon) continue;
            if (const auto buildingId = buildingIdForIcon(click.iconId, click.faction)) {
              selectedBuildAsset = &ensureCachedAssetForStyle(buildingAssetCache, paths.buildingRoot, artIni, paletteLibrary, houseColors, debugPanelState.style, *buildingId);
              previewPlacement = placementFromAnchor(previewPlacement.topLeft, selectedBuildAsset->foundation);
              selection.clear();
            } else {
              const auto faction = click.faction == BuildFaction::Allied ? "ALLIED" : "SOVIET";
              const auto* requested=definitions.sidebarUnit(click.iconId,faction);
              if (!requested) {interactionMessage="No unit mapping for "+click.iconId;continue;}
              if (requested->support()==ra2::SupportStatus::Unsupported) {
                interactionMessage=requested->id+": unsupported "+(requested->unsupported.empty()?"visual mapping":requested->unsupported.front());continue;
              }
              if(!definitions.profile().sandbox && !definitions.allowedHouse(*requested,definitions.playerHouse(0))) {
                interactionMessage="Unit unavailable for "+definitions.playerHouse(0);continue;
              }
              ra2::EntityId factory = 0;
              for (const auto& entity : world.snapshot()) {
                const auto& type = definitions.unit(entity.type);
                if (entity.owner == 0 && definitions.sidebarFactoryMatches(type,*requested,faction) && entity.activity == ra2::Activity::Idle) {
                  if (!factory || entity.id == selection.primary()) factory = entity.id;
                }
              }
              if (factory) { send(ra2::CommandKind::Produce, factory, {}, requested->id); selection.selectOne(factory); selectedBuildAsset = nullptr; interactionMessage.clear(); }
              else interactionMessage = std::string("Place a compatible ")+(click.faction==BuildFaction::Allied?"Allied ":"Soviet ")+(requested->aircraft && requested->aircraft->fixedWing?"airforce command first":requested->infantry?"barracks first":requested->naval?"shipyard on water first":"war factory first");
            }
            continue;
          }
          const auto cell = screenToIso(mouseX, mouseY, mapOrigin, DemoAppConfig::kTileWidth, DemoAppConfig::kTileHeight);
          if(sidebarState.repairSelected || sidebarState.sellSelected) {
            if(mouseX<0 || mouseY<0 || mouseX>=float(viewport.width)-DemoAppConfig::kSidebarWidth || mouseY>=viewport.height)continue;
            const auto target=hitTestBuildingAtPoint(*activeBuildingAssets,buildings,mouseX,mouseY,mapOrigin,DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight);
            const auto id=target?buildings[*target].entityId:0;
            const bool repair=sidebarState.repairSelected;
            if(repair?world.canRepair(id):world.canSell(id)) {
              send(repair?ra2::CommandKind::ToggleRepair:ra2::CommandKind::SellBuilding,id);interactionMessage.clear();
            } else interactionMessage=repair?"请选择己方已建成、可维修的受损建筑":"请选择己方可售卖建筑；工厂需等待单位驶出";
            continue;
          }
          if (selectedBuildAsset) {
            selectionGesture.cancel();
            previewPlacement = placementFromAnchor(cell, selectedBuildAsset->foundation);
            if (world.canPlaceBuilding(selectedBuildAsset->id,previewPlacement.topLeft)) { send(ra2::CommandKind::PlaceBuilding, 0, cell, selectedBuildAsset->id); selectedBuildAsset = nullptr; }
            else interactionMessage = "Placement blocked";
            continue;
          }
          if (mouseX<0 || mouseY<0 || mouseX>=float(viewport.width)-DemoAppConfig::kSidebarWidth || mouseY>=viewport.height) continue;
          if (isCtrlModifierDown() && selection.primary()) { sendSelection(ra2::CommandKind::AttackGround,cell); continue; }
          selectionGesture.begin({mouseX,mouseY},(SDL_GetModState()&KMOD_SHIFT)!=0);
          SDL_CaptureMouse(SDL_TRUE);
          continue;
        }
        if (mouseX<0 || mouseY<0 || mouseX>=float(viewport.width)-DemoAppConfig::kSidebarWidth || mouseY>=viewport.height) continue;
        const auto cell = screenToIso(mouseX, mouseY, mapOrigin, DemoAppConfig::kTileWidth, DemoAppConfig::kTileHeight);
        ra2::EntityId hit = ra2::hitTestInfantry(presentation.infantry,mouseX,mouseY,mapOrigin,DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight);
        if (!hit) {
          if (const auto vehicle = hitTestRhinoTanks(rhinoTanks, mouseX, mouseY, mapOrigin, DemoAppConfig::kTileWidth, DemoAppConfig::kTileHeight)) hit = rhinoTanks[*vehicle].entityId;
          else if (const auto building = hitTestBuildingAtPoint(*activeBuildingAssets, buildings, mouseX, mouseY, mapOrigin, DemoAppConfig::kTileWidth, DemoAppConfig::kTileHeight)) hit = presentation.buildingIds[*building];
        }
        const auto entities = world.snapshot();
        if (release->additive) {
          selection.toggleUnit(hit,presentation.selectables);
          continue;
        }
        if(hit) {
          const auto target=std::find_if(entities.begin(),entities.end(),[&](const auto& e){return e.id==hit;});
          const bool selectedIvan=std::any_of(entities.begin(),entities.end(),[&](const auto& e){return selection.contains(e.id) && definitions.unit(e.type).ivan;});
          if(target!=entities.end() && !(SDL_GetModState()&KMOD_ALT) && (target->bomb || selectedIvan || std::any_of(entities.begin(),entities.end(),[&](const auto& e){return selection.contains(e.id) && definitions.unit(e.type).canDisguise;})) && sendSelection(ra2::CommandKind::AttackEntity,cell,hit)>0)continue;
          if(target!=entities.end() && definitions.unit(target->type).category==ra2::TypeCategory::Building &&
              sendSelection(ra2::CommandKind::EnterBuilding,cell,hit)>0)continue;
          if(target!=entities.end() && target->owner==0 && definitions.unit(target->type).passengers>0 &&
              sendSelection(ra2::CommandKind::EnterTransport,cell,hit)>0)continue;
          if(target!=entities.end() && target->owner==0 && sendSelection(ra2::CommandKind::AttackEntity,cell,hit)>0)continue;
        }
        const auto selectedView=std::find_if(entities.begin(),entities.end(),[&](const auto& item){return item.id==selection.primary() && item.owner==0;});
        if (selection.size()==1 && selectedView!=entities.end() && selectedView->harvesting && !release->additive) {
          const auto target=std::find_if(entities.begin(),entities.end(),[&](const auto& item){return item.id==hit;});
          if (target!=entities.end() && target->owner==0 && definitions.unit(target->type).refinery) {
            send(ra2::CommandKind::ReturnToRefinery,selection.primary(),cell,{},hit); continue;
          }
          if ((!hit || hit==selection.primary()) && world.resourceAt(cell)) { send(ra2::CommandKind::Harvest,selection.primary(),cell); continue; }
        }
        if (hit) {
          const auto found = std::find_if(entities.begin(), entities.end(), [&](const auto& item) { return item.id == hit; });
          if (found != entities.end() && found->owner != 0) {
            if (sendSelection(ra2::CommandKind::AttackEntity,cell,hit)==0) selection.selectOne(hit);
          }
          else if (found != entities.end() && selection.size()==1 && selection.primary() == hit &&
                   (!definitions.unit(found->type).deploysInto.empty() || !definitions.unit(found->type).undeploysInto.empty() ||
                    found->cargo || (definitions.unit(found->type).infantry && definitions.unit(found->type).infantry->deployer)))
            send(ra2::CommandKind::Deploy, selection.primary());
          else selection.selectOne(hit);
        } else if (selection.primary()) sendSelection(ra2::CommandKind::Move,cell);
      }

      presentation=ra2::makeWorldView(world,definitions,selection.ids());

      std::optional<std::size_t> selectedBuildingIndex;
      for (std::size_t i = 0; i < presentation.buildingIds.size(); ++i) if (presentation.buildingIds[i] == selection.primary()) selectedBuildingIndex = i;
      beginImGuiDebugPanelFrame();
      const ImGuiDebugPanelState previousDebugPanelState = debugPanelState;
      bool debugPanelChanged = drawImGuiDebugPanel(debugPanelState, houseColors);
      if (miningSmokeTest && (smokeTestFrames==15 || smokeTestFrames==30)) {
        debugPanelState.style.theater = smokeTestFrames==15 ? TheaterStyle::Snow : TheaterStyle::Urban;
        debugPanelChanged = true;
      }
      if(specialVehicleSmokeTest && (world.tick()==110 || world.tick()==210)) {
        debugPanelState.style.theater=world.tick()==110?TheaterStyle::Snow:TheaterStyle::Urban;debugPanelChanged=true;
      }
      if (debugPanelChanged) {
        const bool styleChanged = !sameVisualStyle(previousDebugPanelState.style, debugPanelState.style);
        const bool resolutionSelectionChanged =
          previousDebugPanelState.display.resolutionIndex != debugPanelState.display.resolutionIndex;

        if (styleChanged) {
          const bool factionChanged = previousDebugPanelState.style.faction != debugPanelState.style.faction;
          const bool theaterChanged = previousDebugPanelState.style.theater != debugPanelState.style.theater;
          const auto selectedBuildingId =
            selectedBuildAsset != nullptr ? std::optional<std::string>(selectedBuildAsset->id) : std::nullopt;

          switchBuildingAssetsForStyle(buildingAssetCache,
                                       activeBuildingAssets,
                                       paths.buildingRoot,
                                       artIni,
                                       paletteLibrary,
                                       houseColors,
                                       debugPanelState.style,
                                       selectedBuildAsset,
                                       previewPlacement);
          for (const auto& building : buildings) {
            (void)ensureCachedAssetForStyle(buildingAssetCache,
                                      paths.buildingRoot,
                                      artIni,
                                      paletteLibrary,
                                      houseColors,
                                      debugPanelState.style,
                                      building.assetId);
          }
          if (factionChanged) {
            selectedBuildAsset = nullptr;
            previewPlacement = initialPlacement;
            destroySidebarAssets(sidebarAssets);
            sidebarAssets = loadSidebarAssetsForStyle(paths, debugPanelState.style, cameoPalette);
            sidebarState = SidebarState{};
            sidebarState.powerProduced=basePower.produced;sidebarState.powerConsumed=basePower.consumed;
            sidebarState.powerOutage=basePower.outageUntil!=0;sidebarState.credits=world.credits();
            updateSidebarStateForDemo(sidebarState, sidebarAssets, viewport.height, SDL_GetTicks());
          } else if (selectedBuildingId.has_value()) {
            selectedBuildAsset = &ensureCachedAssetForStyle(buildingAssetCache,
                                                            paths.buildingRoot,
                                                            artIni,
                                                            paletteLibrary,
                                                            houseColors,
                                                            debugPanelState.style,
                                                            *selectedBuildingId);
            previewPlacement = placementFromAnchor(previewPlacement.topLeft,
                                                   selectedBuildAsset->foundation);
          }

          const auto& currentTheaterPalettes = palettesForTheater(paletteLibrary, debugPanelState.style.theater);
          if (theaterChanged) {
            renderer.setIndexedPalettes(currentTheaterPalettes.unit, currentTheaterPalettes.terrain);
            renderer.setResourcePalettes(currentTheaterPalettes.resource, animationPalette);
            setVehicleRendererCachePalette(vehicleRendererCache, currentTheaterPalettes.unit);
            grandCannonRenderer.setPalette(currentTheaterPalettes.unit);
          }
          renderer.setRemapColor(activeHouseColorValue(debugPanelState.style, houseColors));
        }

        if (resolutionSelectionChanged) {
          viewport = viewportStateForPanel(debugPanelState);
          applyViewportState(window, renderer, gridRenderer, viewport);
          mapOrigin = mapOriginForViewport(viewport);
        }
      }

      std::optional<std::size_t> hoveredBuildingIndex;
      int mouseXInt = 0;
      int mouseYInt = 0;
      SDL_GetMouseState(&mouseXInt, &mouseYInt);
      const float mouseX = static_cast<float>(mouseXInt);
      const float mouseY = static_cast<float>(mouseYInt);
      const bool mouseInWorld =
        mouseX >= 0.0f &&
        mouseY >= 0.0f &&
        mouseX < static_cast<float>(viewport.width) - DemoAppConfig::kSidebarWidth &&
        mouseY < static_cast<float>(viewport.height);
      const bool mouseCapturedByDebugPanel =
        imguiDebugPanelContainsPoint(debugPanelState, mouseX, mouseY) ||
        imguiDebugPanelWantsMouse();
      const bool buildingActionActive=sidebarState.repairSelected || sidebarState.sellSelected;
      const auto hoveredInfantry=!buildingActionActive && selectedBuildAsset==nullptr && mouseInWorld && !mouseCapturedByDebugPanel?
        ra2::hitTestInfantry(presentation.infantry,mouseX,mouseY,mapOrigin,DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight):0;
      if (selectedBuildAsset == nullptr && mouseInWorld && !mouseCapturedByDebugPanel && !hoveredInfantry) {
        hoveredBuildingIndex = hitTestBuildingAtPoint(*activeBuildingAssets,
                                                      buildings,
                                                      mouseX,
                                                      mouseY,
                                                      mapOrigin,
                                                      DemoAppConfig::kTileWidth,
                                                      DemoAppConfig::kTileHeight);
      }
      const auto actionMode=!mouseInWorld || mouseCapturedByDebugPanel?ra2::BuildingActionMode::None:
        sidebarState.repairSelected?ra2::BuildingActionMode::Repair:sidebarState.sellSelected?ra2::BuildingActionMode::Sell:ra2::BuildingActionMode::None;
      const auto actionTarget=hoveredBuildingIndex?buildings[*hoveredBuildingIndex].entityId:0;
      const bool actionAllowed=actionMode==ra2::BuildingActionMode::Repair?world.canRepair(actionTarget):world.canSell(actionTarget);
      ra2::updateBuildingActionCursor(buildingActionAssets,actionMode,actionAllowed,world.tick());

      renderer.beginFrame(0.58f, 0.78f, 0.97f, 1.0f);
      gridRenderer.draw(mapOrigin, DemoAppConfig::kTileWidth, DemoAppConfig::kTileHeight);
      ra2::drawNavalTerrain(renderer,navalVisualAssets,debugPanelState.style.theater,world.map(),mapOrigin,DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight,viewport.width,viewport.height);
      ra2::drawRadiationField(renderer,world,definitions,mapOrigin,DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight);
      renderer.beginUiPass();
      ra2::drawResources(renderer,resourceVisualAssets,debugPanelState.style.theater,
                         world,definitions,mapOrigin,DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight);

      buildRenderQueue(*activeBuildingAssets,
                       buildings,
                       selectedBuildAsset,
                       previewPlacement,
                       nowTicks,
                       map,
                       renderer,
                       mapOrigin,
                       DemoAppConfig::kTileWidth,
                       DemoAppConfig::kTileHeight,
                       renderQueue,selectedBuildAsset?std::optional<bool>{world.canPlaceBuilding(selectedBuildAsset->id,previewPlacement.topLeft)}:std::nullopt);

      const auto& drawTheaterPalettes = palettesForTheater(paletteLibrary, debugPanelState.style.theater);
      auto drawRhinoTankInstance = [&](const RhinoUnitState& tank) {
        if(tank.disguiseVisible) {
          ra2::drawVehicleDisguise(renderer,specialVehicleAssets,debugPanelState.style.theater,tank,mapOrigin,DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight);
          renderer.beginUiPass();
          drawRhinoHealthBar(renderer,rhinoUiAssets,tank,
            {mapOrigin.x,mapOrigin.y-tank.heightLeptons*DemoAppConfig::kTileHeight/256},
            DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight);
          return;
        }
        VplBoxRenderer& vehicleRenderer = vehicleRendererForType(definitions.unit(tank.typeId), vehicleRendererCache, window, paths, voxelLighting, drawTheaterPalettes.unit,tank.turretVariant);
        auto unitDebug = debugPanelState;
        if (tank.owner != 0) {
          unitDebug.style=opponentVisualStyle(unitDebug.style,houseColors);
        }
        auto renderState = buildRhinoTankRenderState(houseColors,
                                                     unitDebug,
                                                     tank.headingRadians);
        applyRhinoSurfaceTilt(renderState,tank.pitchDegrees,tank.rollDegrees);
        renderState.remapEnabled=definitions.art().boolean(definitions.unit(tank.typeId).image,"Remapable",true);
        renderState.objectOpacity=tank.opacity;renderState.animationFrame=tank.animationFrame;
        renderState.shadowIndex=definitions.art().integer(definitions.unit(tank.typeId).image,"ShadowIndex",-1);
        if(tank.submerged){renderState.objectTint={.55f,.7f,.85f};renderState.shadowAlpha=0;}
        if(tank.frozen)renderState.objectTint={.4f,.7f,1};
        if(definitions.unit(tank.typeId).missileSpawn)renderState.shadowAlpha=0;
        {
          renderState.turretRotationDegrees =
            shortestAngleDelta(tank.headingRadians, tank.turretHeadingRadians) * 180.0f / kPi;
        }
        drawRhinoUnit(renderer,
                      vehicleRenderer,
                      rhinoUiAssets,
                      tank,
                      mapOrigin,
                      DemoAppConfig::kTileWidth,
                      DemoAppConfig::kTileHeight,
                      debugPanelState.rhinoPlacement.footprintK,
                      viewport.width,
                      viewport.height,
                      nowTicks,
                      renderState);
      };

      renderer.beginWorldPass();
      for (const auto& command : renderQueue) {
        renderer.setRemapColor(command.instance.neutral?Rgba{170,170,170,255}:command.instance.owner?Rgba{235,45,45,255}:activeHouseColorValue(debugPanelState.style,houseColors));
        const bool layeredWarFactory = isLayeredWarFactoryInstance(command.instance);
        const bool productionSplit =
          layeredWarFactory && command.instance.productionSplit && command.instance.factoryExitActive;

        if (productionSplit) {
          drawWarFactoryProductionUnderUnitLayers(renderer,
                                                  *command.asset,
                                                  command.instance,
                                                  mapOrigin,
                                                  DemoAppConfig::kTileWidth,
                                                  DemoAppConfig::kTileHeight,
                                                  nowTicks,
                                                  command.depth01);
        } else {
          renderer.beginWorldPass();
          drawBuildingInstance(renderer,
                               *command.asset,
                               command.instance,
                               mapOrigin,
                               DemoAppConfig::kTileWidth,
                               DemoAppConfig::kTileHeight,
                               nowTicks,
                               command.depth01,
                               command.tintR,
                               command.tintG,
                               command.tintB,
                               command.tintA);
        }

        const auto& buildingType=definitions.unit(command.instance.assetId);
        if (!buildingType.buildingTurret.empty() && command.instance.state!=BuildingState::Constructing && command.instance.state!=BuildingState::Packing) {
          auto& turretRenderer=buildingType.id=="GTGCAN"?grandCannonRenderer:
            buildingTurretRenderer(buildingType,vehicleRendererCache,window,paths,voxelLighting,drawTheaterPalettes.unit);
          const auto anchor = buildingTurretAnchor(command.instance, definitions.rules(),
                                                      mapOrigin,
                                                      DemoAppConfig::kTileWidth,
                                                      DemoAppConfig::kTileHeight);
          const float turretDepth = buildingTurretDepth(command.instance.placement,
                                                          DemoAppConfig::kTileWidth,
                                                          DemoAppConfig::kTileHeight);
          turretRenderer.renderInWorld(buildingTurretRenderState(houseColors,
                                                                    debugPanelState,
                                                                    command.instance),
                                            viewport.width,
                                            viewport.height,
                                            anchor.x,
                                            anchor.y,
                                            turretDepth,
                                            0.0f);
          renderer.beginWorldPass();
        }

        if (layeredWarFactory) {
          for (const auto& tank : rhinoTanks) {
            if (ra2::isFactoryExitVehicle(tank, command.instance)) {
              drawRhinoTankInstance(tank);
            }
          }
          if (command.instance.deferOverUnit || productionSplit) {
            drawWarFactoryOverUnitLayers(renderer,
                                         *command.asset,
                                         command.instance,
                                         mapOrigin,
                                         DemoAppConfig::kTileWidth,
                                         DemoAppConfig::kTileHeight,
                                         nowTicks,
                                         command.depth01,
                                         productionSplit);
          }
        }
      }



      for (const auto& tank : rhinoTanks) {
        if (drawnWithFactory(tank, buildings)) {
          continue;
        }
        drawRhinoTankInstance(tank);
      }

      for(const auto& projectile:presentation.projectiles)if(ra2::canonicalId(projectile.image)=="SUBT" || ra2::canonicalId(projectile.image)=="ZBOMB") {
        auto& model=navalTorpedoRenderer(vehicleRendererCache,window,paths,voxelLighting,drawTheaterPalettes.unit,ra2::canonicalId(projectile.image)=="ZBOMB"?"zbomb":"subt");
        auto state=buildRhinoTankRenderState(houseColors,debugPanelState,std::atan2(projectile.velocityLogical.y,projectile.velocityLogical.x));state.remapEnabled=false;state.shadowAlpha=0;
        if(ra2::canonicalId(projectile.image)=="ZBOMB")applyRhinoSurfaceTilt(state,-90,0);
        drawDirectVoxelUnitInstance(model,projectile.logicalPosition,{mapOrigin.x,mapOrigin.y-projectile.heightLeptons*DemoAppConfig::kTileHeight/256},DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight,1,viewport.width,viewport.height,state);
      }
      ra2::drawInfantry(renderer,infantryVisualAssets,presentation.infantry,mapOrigin,
                        DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight,
                        activeHouseColorValue(debugPanelState.style,houseColors),Rgba{235,45,45,255},debugPanelState.style.theater==TheaterStyle::Snow);
      renderer.beginWorldPass();
      ra2::drawGathering(renderer,resourceVisualAssets,world,definitions,
                         mapOrigin,DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight);
      drawWeaponProjectilesAndEffects(renderer,
                                      weaponVisualAssets,
                                      projectiles,
                                      combatEffects,
                                      mapOrigin,
                                      DemoAppConfig::kTileWidth,
                                      DemoAppConfig::kTileHeight,
                                      activeHouseColorValue(debugPanelState.style,houseColors),
                                      activeHouseColorValue(opponentVisualStyle(debugPanelState.style,houseColors),houseColors));
      ra2::drawLaserBeams(renderer,presentation.beams,definitions,world.tick(),mapOrigin,
                          DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight,
                          activeHouseColorValue(debugPanelState.style,houseColors),
                          activeHouseColorValue(opponentVisualStyle(debugPanelState.style,houseColors),houseColors));
      ra2::drawElectricBolts(renderer,presentation.beams,definitions,world.tick(),mapOrigin,
                            DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight,overlayPalette);
      ra2::drawSonicWaves(renderer,presentation.beams,definitions,world.tick(),mapOrigin,DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight);
      ra2::drawRadiationBeams(renderer,presentation.beams,definitions,world.tick(),mapOrigin,DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight);
      for (const auto& tank : rhinoTanks) {
        if (drawnWithFactory(tank, buildings)) {
          continue;
        }
        if(tank.airborne || (definitions.unit(tank.typeId).aircraft && definitions.unit(tank.typeId).aircraft->fixedWing))continue;
        redrawBuildingOccludersForRhino(renderer,
                                        *activeBuildingAssets,
                                        buildings,
                                        tank,
                                        mapOrigin,
                                        DemoAppConfig::kTileWidth,
                                        DemoAppConfig::kTileHeight,
                                        viewport.width,
                                        viewport.height,
                                        debugPanelState.rhinoPlacement.footprintK,
                                        nowTicks);
      }

      renderer.beginUiPass();
      ra2::drawInfantrySelection(renderer,infantryVisualAssets,presentation.infantry,mapOrigin,
                                DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight,hoveredInfantry);
      ra2::drawCargoSelection(renderer,infantryVisualAssets,world.snapshot(),selection.ids(),mapOrigin,DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight);
      ra2::drawIvanBombIcons(renderer,infantryVisualAssets,presentation.bombs,mapOrigin,DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight);
      const bool hoveredBuildingIsSelected =
        hoveredBuildingIndex.has_value() &&
        selectedBuildingIndex.has_value() &&
        *hoveredBuildingIndex == *selectedBuildingIndex;
      if (!hoveredBuildingIsSelected) {
        drawBuildingHealthOverlay(renderer,
                                  *activeBuildingAssets,
                                  buildings,
                                  hoveredBuildingIndex,
                                  false,
                                  mapOrigin,
                                  DemoAppConfig::kTileWidth,
                                  DemoAppConfig::kTileHeight);
      }
      drawBuildingHealthOverlay(renderer,
                                *activeBuildingAssets,
                                buildings,
                                selectedBuildingIndex,
                                true,
                                mapOrigin,
                                DemoAppConfig::kTileWidth,
                                DemoAppConfig::kTileHeight);
      for(std::size_t i=0;i<buildings.size();++i)if(buildings[i].repairing && (!selectedBuildingIndex || i!=*selectedBuildingIndex) && (!hoveredBuildingIndex || i!=*hoveredBuildingIndex))
        drawBuildingHealthOverlay(renderer,*activeBuildingAssets,buildings,i,false,mapOrigin,DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight);
      ra2::drawBuildingRepairs(renderer,buildingActionAssets,*activeBuildingAssets,buildings,mapOrigin,DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight);
      if(selectedBuildingIndex) {
        const auto& plant=buildings[*selectedBuildingIndex];
        if(plant.owner==0 && plant.hp>0 && plant.state==BuildingState::Complete && !plant.selling && definitions.unit(plant.assetId).power>0) {
          const auto center=buildingDrawCenter(plant.placement,mapOrigin,DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight);
          drawPowerPlantReadout(sidebarState,center.x,center.y,viewport.width,viewport.height);
        }
      }
      if(buildingActionActive && mouseInWorld && !mouseCapturedByDebugPanel) {
        ImGui::BeginTooltip();
        if(!actionAllowed)ImGui::TextUnformatted(sidebarState.repairSelected?"不可维修此目标":"不可售卖此目标");
        else if(sidebarState.sellSelected)ImGui::Text("售卖返还：$%d",world.saleRefund(actionTarget));
        else {
          const auto& building=buildings[*hoveredBuildingIndex];
          ImGui::TextUnformatted(building.repairing?"点击停止维修":"点击开始维修");
          ImGui::Text("每次恢复最多 %d 生命，当前费用 $%d",definitions.profile().repairStep,world.repairCost(actionTarget));
          if(building.repairPaused)ImGui::TextUnformatted("资金不足，等待资金后继续");
        }
        ImGui::TextUnformatted("右键或 Esc 退出当前模式");ImGui::EndTooltip();
      }
      ra2::drawHarvesterCargo(renderer,world,selection.primary(),mapOrigin,DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight);
      drawSidebar(renderer,
                  static_cast<float>(viewport.width),
                  static_cast<float>(viewport.height),
                  sidebarAssets,
                  sidebarState);
      if(!mouseCapturedByDebugPanel && !mouseInWorld) {
        if(mouseX<viewport.width-DemoAppConfig::kSidebarWidth+17) {
          ImGui::BeginTooltip();ImGui::Text("电力：产出 %d / 消耗 %d",basePower.produced,basePower.consumed);
          ImGui::TextUnformatted("彩色条为发电量，银色横线为负载");
          if(basePower.outageUntil)ImGui::TextUnformatted("电网中断：需要供电的防御建筑已停火");
          else if(!basePower.online())ImGui::TextUnformatted("电力不足：需要供电的防御建筑已停火");
          ImGui::EndTooltip();
        } else if(const auto* icon=hoveredSidebarIcon(sidebarState,sidebarAssets,viewport.width,viewport.height,mouseX,mouseY);icon && !icon->detail.empty()) {
          ImGui::BeginTooltip();ImGui::TextUnformatted(icon->detail.c_str());ImGui::EndTooltip();
        }
      }
      if (mouseInWorld && !mouseCapturedByDebugPanel && !hoveredBuildingIndex && !hoveredInfantry && !selectedBuildAsset) {
        const auto cell=screenToIso(mouseX,mouseY,mapOrigin,DemoAppConfig::kTileWidth,DemoAppConfig::kTileHeight);
        if (const auto resource=world.resourceAt(cell)) {
          const auto& mineral=definitions.resource(resource->type);
          ImGui::BeginTooltip();
          ImGui::TextUnformatted(mineral.image==2 ? "宝石" : "矿石");
          ImGui::Text("剩余 %d 单位  ·  每单位 $%d",resource->amount,mineral.value);
          ImGui::Text("本格总价值 $%lld",static_cast<long long>(resource->amount)*mineral.value);
          ImGui::EndTooltip();
        }
      }
      ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
      ImGui::SetNextWindowBgAlpha(0.82f);
      ImGui::SetNextWindowSizeConstraints(ImVec2(280,0),ImVec2(300,float(viewport.height)-20));
      if (ImGui::Begin("Simulation", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("左键框选/移动/攻击，Shift 增选，Ctrl+左键强制攻击");
        ImGui::TextUnformatted("S 整组停止，D 部署，右键取消选择");
        if(ImGui::Button("查看海岸"))cameraOffset={-360,-280};ImGui::SameLine();if(ImGui::Button("查看机场"))cameraOffset={0,100};ImGui::SameLine();if(ImGui::Button("返回基地"))cameraOffset={};
        ImGui::TextUnformatted("飞机点己方机场返航补给；夜鹰 U 落地卸载");
        ImGui::TextUnformatted("舰船点己方船厂维修；运输艇可装步兵/车辆，U 卸载");
        if (selection.size()>1) ImGui::Text("已选中 %zu 个单位",selection.size());
        ImGui::TextUnformatted("矿车：点矿区采集，点己方矿场卸货，H 自动采矿");
        ImGui::TextUnformatted("工程师：点敌方建筑占领，点己方受损建筑修复");
        ImGui::TextUnformatted("海豹/谭雅：点敌方建筑安放 C4，可游泳");
        ImGui::TextUnformatted("间谍：点步兵伪装，点敌方建筑潜入");
        ImGui::TextUnformatted("超时空兵：点地面传送，点目标持续抹除");
        ImGui::TextUnformatted("伊文：点击目标挂弹；Alt+点击友方载具登车");
        ImGui::TextUnformatted("工程师点击挂弹目标拆弹；右键取消选择");
        ImGui::TextUnformatted("步兵：点己方运输车登车，GI/动员兵点民居驻军；U/D 卸载");
        drawGameSpeedControls(debugPanelState.gameplay);
        ImGui::Text("实际逻辑：%.1f 帧/秒    绘制：%.1f FPS", measuredGameFps, ImGui::GetIO().Framerate);
        ImGui::Text("Tick: %llu  Credits: %d  Entities: %zu", static_cast<unsigned long long>(world.tick()), world.credits(), world.snapshot().size());
        if (!interactionMessage.empty()) ImGui::TextWrapped("%s", interactionMessage.c_str());
        if (!world.lastMessage().empty()) ImGui::TextWrapped("%s", world.lastMessage().c_str());
        for (const auto& entity : world.snapshot()) if (entity.id == selection.primary()) {
          ImGui::Text("%s  HP %d / %d", entity.type.c_str(), entity.hp, entity.maxHp);
          if(entity.buildingInteraction)ImGui::Text("%s #%llu",entity.buildingInteraction->entering?"Entering":"Approaching",
            static_cast<unsigned long long>(entity.buildingInteraction->target));
          if(entity.aircraft && definitions.unit(entity.type).aircraft->fixedWing) {
            ImGui::Text("弹药 %d / %d",entity.aircraft->ammo,definitions.unit(entity.type).aircraft->ammo);
            if(entity.aircraft->reloadRemaining)ImGui::Text("补给剩余 %.1f 秒",entity.aircraft->reloadRemaining/15.0);
            if(entity.owner==0 && ImGui::Button("返航补给 (S)"))send(ra2::CommandKind::Stop,entity.id);
          }
          if(entity.cargo) {
            ImGui::Text("Passengers %d / %d%s",entity.cargo->used,entity.cargo->capacity,entity.cargo->unloading?" (unloading)":"");
            if(!entity.activeWeapon.empty())ImGui::Text("Weapon: %s",entity.activeWeapon.c_str());
            if(entity.owner==0 && !entity.cargo->passengers.empty() && ImGui::Button("Unload (U)"))send(ra2::CommandKind::Unload,entity.id);
          }
          if(entity.harvesting) {
            const auto& cargo=*entity.harvesting;
            ImGui::Text("货舱 %d / %d    价值 $%d",cargo.cargo,cargo.capacity,cargo.value);
            ImGui::ProgressBar(static_cast<float>(cargo.cargo)/cargo.capacity,ImVec2(-1,0),ra2::harvestPhaseName(cargo.phase));
            if(ImGui::Button("自动采矿")) send(ra2::CommandKind::Harvest,selection.primary(),entity.cell);
          }
          switch (entity.factoryPhase) {
            case ra2::FactoryExitPhase::Blocked: ImGui::TextUnformatted("出口堵塞，等待通道空闲"); break;
            case ra2::FactoryExitPhase::Preparing: ImGui::TextUnformatted("出车准备中"); break;
            case ra2::FactoryExitPhase::Driving: ImGui::TextUnformatted("车辆正在驶出"); break;
            case ra2::FactoryExitPhase::Clearing: ImGui::TextUnformatted("出车收尾中"); break;
            default: break;
          }
          if (entity.activity == ra2::Activity::LeavingFactory)
            ImGui::TextUnformatted(entity.hasDeferredCommand ? "正在出厂，驶离后执行新命令" : "正在出厂");
          if (entity.queueSize) {
            ImGui::Text("%s  Queue: %zu", entity.productionType.c_str(), entity.queueSize);
            ImGui::ProgressBar(1.0f - static_cast<float>(entity.productionRemaining) / std::max(1, entity.productionTotal));
            if (ImGui::Button("取消最后一项")) send(ra2::CommandKind::CancelProduction, selection.primary());
          }
        }
        ImGui::PopTextWrapPos();
      }
      ImGui::End();
      if (gameSpeedSmokeTest) {
        const auto advanced = world.tick() - speedSmokePreviousFrame;
        const auto rate = ra2::targetGameFrameRate(smokeSpeeds[smokeTestFrames]);
        if ((rate && advanced != static_cast<ra2::Tick>(*rate / 10)) || (!rate && (advanced < 8 || advanced % 8 != 0)))
          throw std::runtime_error("Game speed render-loop smoke assertion failed");
        std::cout << "Speed smoke render " << smokeTestFrames << ": " << advanced << " game frames\n";
        speedSmokePreviousFrame = world.tick();
        if (smokeTestFrames + 1 < static_cast<int>(smokeSpeeds.size()))
          debugPanelState.gameplay.speed = smokeSpeeds[smokeTestFrames + 1];
      }
      world.setGameSpeed(debugPanelState.gameplay.speed);
      if (selectionGesture.dragging()) {
        const auto box=selectionGesture.rectangle();
        auto* overlay=ImGui::GetForegroundDrawList();
        overlay->AddRectFilled(ImVec2(box.left,box.top),ImVec2(box.right,box.bottom),IM_COL32(64,255,100,20));
        overlay->AddRect(ImVec2(box.left,box.top),ImVec2(box.right,box.bottom),IM_COL32(64,255,100,255),0,0,1.5f);
      }
      renderImGuiDebugPanel();
      const std::array<ra2::Tick,7> buildingCaptureFrames{1,20,47,73,74,145,191};
      const bool captureInfantry=infantrySmokeTest && (world.tick()==1 || world.tick()==20 || world.tick()==70 || world.tick()==150 || (supportInfantrySmokeTest && world.tick()==300) ||
        (transportSmokeTest && (world.tick()==220 || world.tick()==320 || world.tick()==560 || world.tick()==660)) ||
        (airDefenseSmokeTest && (world.tick()==100 || world.tick()==220 || world.tick()==320 || world.tick()==360)) ||
        (specialVehicleSmokeTest && (world.tick()==25 || world.tick()==40 || world.tick()==55 || world.tick()==85 || world.tick()==115 || world.tick()==125 || world.tick()==225 || world.tick()==325)));
      const bool captureBuilding = buildingSmokeTest && std::find(buildingCaptureFrames.begin(),buildingCaptureFrames.end(),world.tick())!=buildingCaptureFrames.end();
      const bool captureBuildingActions=buildingActionsSmokeTest && (world.tick()==10 || world.tick()==22 || world.tick()==170 || world.tick()==213 || world.tick()==270);
      const bool captureTesla=teslaUnitsSmokeTest && (world.tick()==20 || world.tick()==60 || world.tick()==115 || world.tick()==175 || world.tick()==215 || world.tick()==270);
      const bool captureAir=aircraftSmokeTest && (world.tick()==32 || world.tick()==120 || world.tick()==250 || world.tick()==450 || world.tick()==850 || world.tick()==1200);
      const bool captureNaval=(navalSmokeTest && (world.tick()==40 || world.tick()==130 || world.tick()==210 || world.tick()==320 || world.tick()==430)) || (navalSpecialSmokeTest && (world.tick()==60 || world.tick()==110 || world.tick()==180 || world.tick()==320 || world.tick()==620));
      const bool captureIvan=ivanApocalypseSmokeTest && (world.tick()==30 || world.tick()==100 || world.tick()==130 || world.tick()==200 || world.tick()==225 || world.tick()==400 || (ivanBlastFrame && (world.tick()==ivanBlastFrame+4 || world.tick()==ivanBlastFrame+10)));
      const bool captureDemolition=demolitionSmokeTest && (world.tick()==20 || world.tick()==60 || world.tick()==90 || world.tick()==104 || world.tick()==112 || world.tick()==174 || world.tick()==182);
      const bool captureDogDrone=dogDroneSmokeTest && (world.tick()==10 || world.tick()==15 || world.tick()==50 || world.tick()==95 || world.tick()==155);
      const bool captureRadiation=radiationV3SmokeTest && (world.tick()==20 || world.tick()==40 || world.tick()==70 || world.tick()==100 || world.tick()==150 || world.tick()==220 || world.tick()==470 || world.tick()==620);
      const std::array<ra2::Tick,10> tankCaptureFrames{1,8,16,24,30,34,38,48,80,120};
      const bool captureTank=tankSmokeTest && std::find(tankCaptureFrames.begin(),tankCaptureFrames.end(),world.tick())!=tankCaptureFrames.end();
      const std::array<ra2::Tick,5> selectionCaptureFrames{2,3,7,60,140};
      const bool captureSelection=selectionSmokeTest && std::find(selectionCaptureFrames.begin(),selectionCaptureFrames.end(),world.tick())!=selectionCaptureFrames.end();
      if (((sidebarSmokeTest && smokeTestFrames==2) || (simulationSmokeTest && (smokeTestFrames == 31 || smokeTestFrames == 35)) || captureAir || captureNaval || captureIvan || captureDemolition || captureDogDrone || captureRadiation || captureTesla || captureInfantry || captureBuilding || captureBuildingActions || captureTank || captureSelection || (miningSmokeTest && (smokeTestFrames==9 || smokeTestFrames==24 || smokeTestFrames==44))) && argc == 3) {
        std::vector<unsigned char> pixels(static_cast<std::size_t>(viewport.width) * viewport.height * 4);
        glReadPixels(0, 0, viewport.width, viewport.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        const auto pitch = static_cast<std::size_t>(viewport.width) * 4;
        for (int y = 0; y < viewport.height / 2; ++y) std::swap_ranges(pixels.begin() + y * pitch, pixels.begin() + (y + 1) * pitch, pixels.begin() + (viewport.height - y - 1) * pitch);
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(), viewport.width, viewport.height, 32, static_cast<int>(pitch), SDL_PIXELFORMAT_RGBA32);
        if (!surface) throw std::runtime_error(SDL_GetError());
        auto output = std::filesystem::path(argv[2]);
        if (captureAir || captureNaval || captureIvan || captureDemolition || captureDogDrone || captureRadiation || captureTesla || captureInfantry || captureBuilding || captureBuildingActions || captureTank || captureSelection) output.replace_filename(output.stem().string() + "-frame-" + std::to_string(world.tick()) + ".bmp");
        else if (miningSmokeTest) output.replace_filename(output.stem().string() + "-frame-" + std::to_string(world.tick()) + ".bmp");
        else if (smokeTestFrames == 31) output.replace_filename(output.stem().string() + "-egress.bmp");
        const int saved = SDL_SaveBMP(surface, output.string().c_str());
        SDL_FreeSurface(surface);
        if (saved != 0) throw std::runtime_error(SDL_GetError());
      }
      if(smokeTest)glFlush();else SDL_GL_SwapWindow(window);
      if (smokeTest && ++smokeTestFrames >= (aircraftSmokeTest?1200:navalSpecialSmokeTest?900:navalSmokeTest?480:ivanApocalypseSmokeTest?600:demolitionSmokeTest?220:dogDroneSmokeTest?190:radiationV3SmokeTest?650:teslaUnitsSmokeTest?300:buildingActionsSmokeTest?300:defenseSmokeTest?240:specialVehicleSmokeTest?360:airDefenseSmokeTest?420:transportSmokeTest?700:supportInfantrySmokeTest ? 320 : infantrySmokeTest ? 180 : selectionSmokeTest ? 140 : tankSmokeTest ? 120 : miningSmokeTest ? 45 : buildingSmokeTest ? 208 : simulationSmokeTest ? 36 : gameSpeedSmokeTest ? static_cast<int>(smokeSpeeds.size()) : 3)) {
        if(sandboxSmokeTest && (!world.power(0).online() || world.snapshot().size()<25))throw std::runtime_error("Full sandbox startup lacks power or practice units");
        if(aircraftSmokeTest) {
          if(!sawAirSalvo || !sawAirReturn || !sawAirRearm || !sawAirBoard || !sawAirUnload || !sawAirBomb || !sawPlaneClick || glGetError()!=GL_NO_ERROR)
            throw std::runtime_error("Aircraft SDL failed: salvo="+std::to_string(sawAirSalvo)+" return="+std::to_string(sawAirReturn)+" rearm="+std::to_string(sawAirRearm)+" board="+std::to_string(sawAirBoard)+" unload="+std::to_string(sawAirUnload)+" bomb="+std::to_string(sawAirBomb)+" click="+std::to_string(sawPlaneClick));
          std::cout<<"Aircraft SDL passed: both fighters produced, actual selection click, missile salvo, return/rearm, helicopter boarding/unload and Kirov voxel bombs\n";
        }
        if(navalSpecialSmokeTest) {
          if(!sawHornets || !sawAsw || !sawDreadMissiles || !sawGrapple || !sawRelease || !sawSonic || !sawNavyClick || !sawReload || glGetError()!=GL_NO_ERROR)throw std::runtime_error("Special navy SDL failed: hornets="+std::to_string(sawHornets)+" ASW="+std::to_string(sawAsw)+" missiles="+std::to_string(sawDreadMissiles)+" grapple="+std::to_string(sawGrapple)+" release="+std::to_string(sawRelease)+" sonic="+std::to_string(sawSonic)+" click="+std::to_string(sawNavyClick)+" reload="+std::to_string(sawReload));
          std::cout<<"Special navy SDL passed: actual creature selection, carrier squadron/reload, ASW, dual DMISL, squid grapple and dolphin rescue\n";
        }
        if(navalSmokeTest) {
          if(!sawNavalBoard || !sawNavalDeepHold || !sawNavalUnload || !sawTorpedo || !sawMedusa || !sawNavalFlak || !sawSubmerged || !sawNavalProduction || glGetError()!=GL_NO_ERROR)
            throw std::runtime_error("Naval SDL smoke failed: board="+std::to_string(sawNavalBoard)+" deep="+std::to_string(sawNavalDeepHold)+" unload="+std::to_string(sawNavalUnload)+" torpedo="+std::to_string(sawTorpedo)+" medusa="+std::to_string(sawMedusa)+" flak="+std::to_string(sawNavalFlak)+" submerged="+std::to_string(sawSubmerged)+" production="+std::to_string(sawNavalProduction));
          std::cout<<"Naval SDL passed: actual vehicle/transport clicks, loaded sailing, blocked deep-water unload, return/unload, shipyard production, underwater torpedo, Medusa and naval AA\n";
        }
        if(ivanApocalypseSmokeTest) {
          const auto entities=world.snapshot();
          if(std::any_of(entities.begin(),entities.end(),[&](const auto& e){return e.id==ivanIfv || e.id==ivanIfvPassenger;}))throw std::runtime_error("Ivan IFV/cargo survived detonation");
          if(!sawIvanAttach || !sawIvanDisarm || !sawIvanSelection || !sawIvanIfv || !sawIvanIfvBlast || !ivanBlastFrame || ivanIconFrames!=4095 || ivanWeapons!=3 || !sawApocHeal || !sawApocMuzzle || !sawApocImpact || glGetError()!=GL_NO_ERROR)
            throw std::runtime_error("Ivan/Apocalypse SDL failed: attach="+std::to_string(sawIvanAttach)+" disarm="+std::to_string(sawIvanDisarm)+" click="+std::to_string(sawIvanSelection)+" IFV="+std::to_string(sawIvanIfv)+std::to_string(sawIvanIfvBlast)+" icons="+std::to_string(ivanIconFrames)+" weapons="+std::to_string(ivanWeapons)+" heal="+std::to_string(sawApocHeal)+" effects="+std::to_string(sawApocMuzzle)+std::to_string(sawApocImpact));
          std::cout<<"Ivan/Apocalypse SDL passed: moving sticky bomb/countdown, real engineer selection/disarm, Ivan IFV, native cannon/AA effects and free self-healing\n";
        }
        if(demolitionSmokeTest) {
          for(const auto& e:world.snapshot())if(e.id==demolitionFoot || e.id==demolitionTruck || e.id==demolitionIfv || e.id==demolitionPassenger)throw std::runtime_error("Demolition actor/passenger survived detonation");
          if(demolitionExplosions!=7 || !sawDemolitionMode || !sawDemolitionRadiation || !sawDemolitionClick || glGetError()!=GL_NO_ERROR)
            throw std::runtime_error("Demolition SDL failed: explosions="+std::to_string(demolitionExplosions)+" mode="+std::to_string(sawDemolitionMode)+" radiation="+std::to_string(sawDemolitionRadiation)+" click="+std::to_string(sawDemolitionClick));
          std::cout<<"Demolition SDL passed: native units/explosions, real infantry selection, suicide IFV and residual radiation\n";
        }
        if(dogDroneSmokeTest) {
          const auto entities=world.snapshot();
          for(const auto id:leapVictims)if(std::any_of(entities.begin(),entities.end(),[&](const auto& e){return e.id==id;}))throw std::runtime_error("Leap failed to kill infantry");
          if(std::any_of(entities.begin(),entities.end(),[&](const auto& e){return e.id==droneHost;}) || leapSeen!=7 || !sawInfestation || !sawDroneReturn || !sawDogDroneClick || glGetError()!=GL_NO_ERROR)
            throw std::runtime_error("Dog/drone SDL failed: leaps="+std::to_string(leapSeen)+" infestation="+std::to_string(sawInfestation)+" return="+std::to_string(sawDroneReturn)+" click="+std::to_string(sawDogDroneClick));
          std::cout<<"Dog/drone SDL passed: original SHP facings, walking/jump sprites, shadows, three theaters, real selection, infestation and return\n";
        }
        if(radiationV3SmokeTest) {
          bool impact=false,safe=false,moved=false;
          for(const auto& e:world.snapshot()){if(e.id==v3Victim)impact=e.hp==e.maxHp-200;if(e.cell==TileCoord{23,15} && e.type=="GAPOWR")safe=e.hp==e.maxHp;if(e.id==radiationWorker)moved=e.cell==TileCoord{12,12};}
          if(!sawRadiationBeam || !sawRadiationIfvBeam || !sawRadDeploy || !sawRadMove || !sawRadSite || !sawRadDeath || !sawV3Missile || !sawV3Intercept || radiationTheaters!=7 || v3BodyStates!=3 || !impact || !safe || !moved || !world.radiationSites().empty() || glGetError()!=GL_NO_ERROR)
            throw std::runtime_error("Radiation/V3 SDL failed: deploy="+std::to_string(sawRadDeploy)+" move="+std::to_string(sawRadMove)+" site="+std::to_string(sawRadSite)+" death="+std::to_string(sawRadDeath)+" beams="+std::to_string(sawRadiationBeam)+std::to_string(sawRadiationIfvBeam)+" V3="+std::to_string(sawV3Missile)+std::to_string(sawV3Intercept)+" impact="+std::to_string(impact)+" safe="+std::to_string(safe)+" moved="+std::to_string(moved)+" bodies="+std::to_string(v3BodyStates));
          std::cout<<"Radiation/V3 SDL passed: original infantry/effects/voxels, D deploy, click to undeploy/move, persistent radiation expiry, radiation IFV, V3 launch/reload/impact/interception, three theaters\n";
        }
        if(teslaUnitsSmokeTest) {
          if(teslaWeapons!=63 || teslaTheaters!=7 || !sawTeslaIfv || !sawTeslaUnload || !sawElectro || !sawTeslaClick || glGetError()!=GL_NO_ERROR)
            throw std::runtime_error("Tesla GPU smoke failed: weapons="+std::to_string(teslaWeapons)+" theaters="+std::to_string(teslaTheaters)+" IFV="+std::to_string(sawTeslaIfv)+" unload="+std::to_string(sawTeslaUnload)+" electro="+std::to_string(sawElectro)+" click="+std::to_string(sawTeslaClick));
          std::cout<<"Tesla GPU passed: infantry/tank, six bolt weapons, SDL friendly-coil order, electrocution, shock IFV/unload, three theaters\n";
        }
        if(buildingActionsSmokeTest) {
          for(int target=0;target<2;++target)if(!sawServiceRepair[target] || !sawServiceComplete[target] || !sawServiceSale[target])
            throw std::runtime_error("Building action SDL repair/sale failed for target "+std::to_string(target));
          if(world.structureAt({18,14}) || world.structureAt({15,14}) || !sawRepairCancel || !sawActionRightCancel || !sawActionEscapeCancel ||
             world.credits()!=serviceMoneyBeforeSale+(definitions.unit("GAPOWR").cost+definitions.unit("NAPOWR").cost)/2 || glGetError()!=GL_NO_ERROR)
            throw std::runtime_error("Building action SDL cancellation/refund/occupancy failed");
          std::cout<<"Building action SDL passed: original cursors, real repair/sell buttons, both faction buildings, cancel/resume/full HP, reverse buildup, refunds, right-click/Escape cancellation\n";
        }
        if(defenseSmokeTest) {
          for(int tower=0;tower<2;++tower)if(!sawDefensePreview[tower] || !sawDefenseBuildup[tower] || !sawDefenseComplete[tower] || !sawDefenseFire[tower])
            throw std::runtime_error("Defense SDL placement/preview/buildup/combat smoke failed for "+std::string(tower==0?"ATESLA":"TESLA"));
          if(!sawDefenseOffline || !sawDefenseRestored || glGetError()!=GL_NO_ERROR)
            throw std::runtime_error("Defense live outage/restoration rendering failed");
          std::cout<<"Defense SDL passed: actual sidebar clicks, both tower previews, placement, construction, firing, outage and restoration\n";
        }
        if(infantrySmokeTest && !specialVehicleSmokeTest && !supportInfantrySmokeTest && !transportSmokeTest && !airDefenseSmokeTest && (!sawInfantryDeployed || !sawInfantryWalk || !sawInfantryFire || glGetError()!=GL_NO_ERROR))
          throw std::runtime_error("Infantry animation/deployment/movement OpenGL smoke failed");
        if(specialVehicleSmokeTest && (!sawSabotExplosion || !sawPrismBeam || !sawPrismFragments || !sawMirageTree || !sawMirageMove || !sawMirageFire || !sawMirageClick || chargeStages!=15 || glGetError()!=GL_NO_ERROR))
          throw std::runtime_error("Special vehicle smoke failed: sabot="+std::to_string(sawSabotExplosion)+" beam="+std::to_string(sawPrismBeam)+" fragments="+std::to_string(sawPrismFragments)+" tree="+std::to_string(sawMirageTree)+" move="+std::to_string(sawMirageMove)+" fire="+std::to_string(sawMirageFire)+" click="+std::to_string(sawMirageClick)+" charge="+std::to_string(chargeStages));
        if(airDefenseSmokeTest && (!sawAirFlight || !sawAirHover || !sawAirFire || !sawFlakGround || !sawFlakAir || !sawAirDeath || !sawAirClick || glGetError()!=GL_NO_ERROR))
          throw std::runtime_error("Air defense rendering/selection smoke failed: fly="+std::to_string(sawAirFlight)+" hover="+std::to_string(sawAirHover)+" fire="+std::to_string(sawAirFire)+" ground="+std::to_string(sawFlakGround)+" air="+std::to_string(sawFlakAir)+" death="+std::to_string(sawAirDeath)+" click="+std::to_string(sawAirClick));
        if(transportSmokeTest) {
          const unsigned required=(1u<<1)|(1u<<2)|(1u<<5);
          if((transportModes&required)!=required || !sawTransportHidden || !sawGarrison || !sawGarrisonFire || !sawMissile || !sawIfvRepair || glGetError()!=GL_NO_ERROR)
            throw std::runtime_error("Transport/IFV/garrison OpenGL smoke failed: modes="+std::to_string(transportModes)+" repair="+std::to_string(sawIfvRepair));
          for(const auto& entity:world.snapshot())if((entity.id==transportTrack || entity.id==transportHouse) && entity.cargo && !entity.cargo->passengers.empty())
            throw std::runtime_error("Actual U hotkey did not unload "+entity.type+" (selected entity "+std::to_string(selection.primary())+")");
        }
    if(supportInfantrySmokeTest) {
          bool captured=false,repaired=false;int engineers=0;
          for(const auto& entity:world.snapshot()) {
            captured|=entity.id==supportCapture && entity.owner==0;
            repaired|=entity.id==supportRepair && entity.hp==entity.maxHp;
            engineers+=entity.type=="ENGINEER" || entity.type=="SENGINEER";
            if(entity.id==supportEngineer || entity.id==supportSovietEngineer || entity.id==supportVictim)
              throw std::runtime_error("Support infantry interaction did not finish");
          }
          if(!captured || !repaired || !sawDamagedRepair || !sawEngineerEntry || !sawSniperShot || engineers!=2 || glGetError()!=GL_NO_ERROR)
            throw std::runtime_error("Engineer/sniper production/combat/presentation smoke failed");
        }
        if(selectionSmokeTest) {
          const auto entities=world.snapshot();
          std::vector<TileCoord> destinations;
          for(const auto& entity:entities) if(selection.contains(entity.id)) {
            if(entity.cell!=entity.destination || std::abs(entity.cell.x-10)>1 || std::abs(entity.cell.y-8)>1)
              throw std::runtime_error("Box-selected tank failed to arrive at group destination");
            destinations.push_back(entity.cell);
          }
          if(selection.size()!=2 || !selection.contains(2) || !selection.contains(selectionSmokeUnit) ||
              destinations.size()!=2 || destinations[0]==destinations[1] || glGetError()!=GL_NO_ERROR)
            throw std::runtime_error("Box selection/group command presentation smoke assertions failed");
        }
        if (tankSmokeTest && (!sawMovingTurret || !sawShell || !sawExplosion || glGetError()!=GL_NO_ERROR))
          throw std::runtime_error("Tank movement/turret/projectile presentation smoke assertions failed");
        if (miningSmokeTest && glGetError()!=GL_NO_ERROR)
          throw std::runtime_error("OpenGL error while rendering original mining assets");
        if(miningSmokeTest && (!sawMiningCargo || !sawMiningReturn || world.credits()<=definitions.profile().startingCredits))
          throw std::runtime_error("Mining presentation/income smoke assertions failed");
        if (buildingSmokeTest) {
          if (!sawBuildup[0] || !sawBuildup[1] || !sawComplete[0] || !sawComplete[1] || !sawPacking[0] || !sawPacking[1])
            throw std::runtime_error("Construction yard animation phase missing");
          for (const auto& entity : world.snapshot()) if ((entity.id==testMcv && entity.type!="AMCV") ||
                                                         (entity.id==testSovietMcv && entity.type!="SMCV"))
            throw std::runtime_error("Construction yard did not finish packing");
        }
        if (simulationSmokeTest) {
          bool deployed = false, enemyAlive = false;
          for (const auto& entity : world.snapshot()) {
            if (entity.id == testMcv && entity.type == "GACNST") deployed = true;
            if (entity.id == 4) enemyAlive = true;
          }
          if (!deployed || enemyAlive || !sawCombatDamage || !sawInternalExit || world.snapshot().size() != 9) throw std::runtime_error("Simulation/presentation smoke assertions failed");
        }
        running = false;
      }
    }

    cleanupApp(window,
               glContext,
               renderer,
               gridRenderer,
               rhinoRenderer,
               grizzlyRenderer,
               vehicleRendererCache,
               grandCannonRenderer,
               mcvRenderer,
               sidebarAssets,
               buildingAssetCache,
               rhinoUiAssets,
               weaponVisualAssets,
               resourceVisualAssets,infantryVisualAssets,specialVehicleAssets,buildingActionAssets,navalVisualAssets);
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    cleanupApp(window,
               glContext,
               renderer,
               gridRenderer,
               rhinoRenderer,
               grizzlyRenderer,
               vehicleRendererCache,
               grandCannonRenderer,
               mcvRenderer,
               sidebarAssets,
               buildingAssetCache,
               rhinoUiAssets,
               weaponVisualAssets,
               resourceVisualAssets,infantryVisualAssets,specialVehicleAssets,buildingActionAssets,navalVisualAssets);
    return 1;
  }
}
