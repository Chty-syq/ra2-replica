#include "resource_view.h"
#include "resource_art.h"
#include <algorithm>
#include <array>

namespace ra2 {
namespace {
void uploadAtlas(UiTexture& texture, const ResourceAtlasPixels& atlas, UiTexturePaletteKind paletteKind) {
  texture.width = atlas.frameWidth * atlas.columns;
  texture.height = atlas.frameHeight * atlas.rows;
  texture.colorMode = UiTextureColorMode::IndexedPalette;
  // RA2 minerals use temperat/snow/urban.pal; OREGATH uses anim.pal.
  // A theater filename suffix does not imply a unit or ISO palette.
  texture.paletteKind = paletteKind;
  texture.paletteRemap = false;
  glGenTextures(1, &texture.texture);
  glBindTexture(GL_TEXTURE_2D, texture.texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, texture.width, texture.height, 0,
               GL_RED, GL_UNSIGNED_BYTE, atlas.indices.data());
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}
void sprite(std::vector<RenderVertex>& vertices, Vec2 topLeft, float width, float height,
            int column, int row, int columns, int rows, float depth = 0) {
  const float u0 = static_cast<float>(column) / columns, u1 = static_cast<float>(column+1) / columns;
  const float v0 = static_cast<float>(row) / rows, v1 = static_cast<float>(row+1) / rows;
  const float x = topLeft.x, y = topLeft.y;
  const RenderVertex a{x,y,depth,u0,v0}, b{x+width,y,depth,u1,v0};
  const RenderVertex c{x+width,y+height,depth,u1,v1}, d{x,y+height,depth,u0,v1};
  vertices.insert(vertices.end(), {a,b,c,a,c,d});
}
void rectangle(std::vector<RenderVertex>& vertices,float x,float y,float w,float h,std::array<float,3> color) {
  for (const auto offset:std::array<Vec2,6>{{{0,0},{w,0},{w,h},{0,0},{w,h},{0,h}}})
    vertices.push_back({x+offset.x,y+offset.y,0,0,0,color[0],color[1],color[2],1});
}
}
void loadResourceVisualAssets(ResourceVisualAssets& assets, const std::filesystem::path& assetRoot) {
  destroyResourceVisualAssets(assets);
  try {
    constexpr std::array<const char*,3> theaters{"temperate", "snow", "urban"};
    constexpr std::array<const char*,3> extensions{".tem", ".sno", ".urb"};
    for (std::size_t theater = 0; theater < theaters.size(); ++theater) {
      const auto root = assetRoot / "terrain" / "resources" / theaters[theater];
      uploadAtlas(assets.overlays[theater][0], loadResourceAtlas(root, "tib", extensions[theater]), UiTexturePaletteKind::Resource);
      uploadAtlas(assets.overlays[theater][1], loadResourceAtlas(root, "gem", extensions[theater]), UiTexturePaletteKind::Resource);
    }
    uploadAtlas(assets.gathering, loadGatherAtlas(assetRoot / "effects" / "harvesting" / "oregath.shp"), UiTexturePaletteKind::Animation);
  } catch (...) {
    destroyResourceVisualAssets(assets);
    throw;
  }
}
void destroyResourceVisualAssets(ResourceVisualAssets& assets) {
  const auto destroy = [](UiTexture& texture) {
    if (texture.texture) glDeleteTextures(1, &texture.texture);
    texture = {};
  };
  for (auto& theater : assets.overlays) for (auto& texture : theater) destroy(texture);
  destroy(assets.gathering);
}
void drawResources(Renderer2D& renderer, const ResourceVisualAssets& assets, TheaterStyle theater,
                   const World& world,const DefinitionRegistry& definitions,
                   Vec2 origin,float tileWidth,float tileHeight) {
  std::array<std::vector<RenderVertex>,2> batches;
  for (const auto& resource:world.resources()) {
    const auto center=isoToScreen(resource.cell.x,resource.cell.y,origin,tileWidth,tileHeight);
    const bool gems=definitions.resource(resource.type).image==2;
    const auto full = definitions.replica().integer("ResourceVisual." + resource.type, "FullDensityAmount", 32);
    const auto density = resourceDensityFrame(resource.amount, full);
    if (density < 0) continue;
    // Pixels live in the bottom half of a 60x60 canvas. Anchor its ground
    // diamond at (30,45), not the canvas center; cropping is already decoded.
    sprite(batches[gems ? 1 : 0], {center.x-tileWidth*.5f, center.y-tileHeight*1.5f},
           tileWidth, tileHeight*2, density, resourceVariant(resource.cell),
           kResourceDensities, kResourceVariants);
  }
  for (std::size_t kind = 0; kind < batches.size(); ++kind) {
    const auto& vertices = batches[kind];
    if (!vertices.empty()) renderer.draw(GL_TRIANGLES,assets.overlays[static_cast<std::size_t>(theater)][kind],vertices.data(),vertices.size());
  }
}
void drawGathering(Renderer2D& renderer, const ResourceVisualAssets& assets,
                   const World& world, const DefinitionRegistry& definitions,
                   Vec2 origin, float tileWidth, float tileHeight) {
  std::vector<RenderVertex> vertices;
  for (const auto& entity : world.snapshot()) {
    if (!entity.harvesting || entity.harvesting->phase != HarvestPhase::Harvesting ||
        entity.activity != Activity::Idle || !world.resourceAt(entity.cell)) continue;
    const auto& harvester = definitions.unit(entity.type).harvester;
    if (!harvester) continue;
    const auto elapsed = world.tick() - entity.harvesting->cycleStarted;
    const auto frame = static_cast<int>(std::clamp<GameFrame>(elapsed * 15 / harvester->harvestFrames, 0, 14));
    const auto center = isoToScreen(entity.cell.x, entity.cell.y, origin, tileWidth, tileHeight);
    const float scaleX = tileWidth / 60, scaleY = tileHeight / 30;
    const auto depth = depthFromLogicalCenter({static_cast<float>(entity.cell.x), static_cast<float>(entity.cell.y)},tileWidth,tileHeight) - .00001f;
    sprite(vertices, {center.x-72*scaleX,center.y-40*scaleY}, 144*scaleX,80*scaleY,
           frame,gatherDirection(entity.heading),15,8,depth);
  }
  if (!vertices.empty()) renderer.draw(GL_TRIANGLES,assets.gathering,vertices.data(),vertices.size());
}
void drawHarvesterCargo(Renderer2D& renderer,const World& world,EntityId selected,
                        Vec2 origin,float tileWidth,float tileHeight) {
  std::vector<RenderVertex> vertices;
  for (const auto& entity:world.snapshot()) if(entity.id==selected && entity.harvesting) {
    const auto alpha=world.interpolation();
    const auto x=(entity.previous.x+(entity.position.x-entity.previous.x)*alpha)/1000.;
    const auto y=(entity.previous.y+(entity.position.y-entity.previous.y)*alpha)/1000.;
    const float screenX=origin.x+static_cast<float>(x-y)*tileWidth*.5f;
    const float screenY=origin.y+static_cast<float>(x+y)*tileHeight*.5f+17;
    const auto& cargo=*entity.harvesting;
    const int filled=(cargo.cargo*5+cargo.capacity-1)/cargo.capacity;
    for(int i=0;i<5;++i) {
      rectangle(vertices,screenX-18+i*7,screenY,6,5,{.08f,.09f,.07f});
      if(i<filled) rectangle(vertices,screenX-17+i*7,screenY+1,4,3,{.96f,.77f,.12f});
    }
  }
  if(!vertices.empty()) renderer.draw(GL_TRIANGLES,renderer.whiteTexture(),vertices.data(),vertices.size());
}
}
