#include "building_action_view.h"
#include "iso_world.h"
#include "shp_ts.h"
#include <algorithm>
#include <stdexcept>

namespace ra2 {
namespace {
SDL_Cursor* cursor(const ShpTsFile& shp,int frame,const Palette& palette) {
  const auto decoded=shp.decodeFrame(frame);auto pixels=shp.decodeFrameRgba(frame,palette);
  auto* surface=SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(),decoded.width,decoded.height,32,decoded.width*4,SDL_PIXELFORMAT_RGBA32);
  if(!surface)throw std::runtime_error(SDL_GetError());
  auto* result=SDL_CreateColorCursor(surface,decoded.width/2,decoded.height/2);SDL_FreeSurface(surface);
  if(!result)throw std::runtime_error(SDL_GetError());return result;
}
}
void loadBuildingActionVisualAssets(BuildingActionVisualAssets& assets,const std::filesystem::path& root) {
  destroyBuildingActionVisualAssets(assets);
  try {
    const auto folder=root/"ui"/"building_actions";
    const auto mouse=ShpTsFile::load(folder/"mouse.shp");const auto palette=Palette::load(root/"palettes"/"ui"/"mousepal.pal");
    for(int i=0;i<20;++i)assets.repair[i]=cursor(mouse,170+i,palette);
    for(int i=0;i<10;++i)assets.sell[i]=cursor(mouse,129+i,palette);
    assets.repairBlocked=cursor(mouse,190,palette);assets.sellBlocked=cursor(mouse,149,palette);
    const auto wrench=ShpTsFile::load(folder/"wrench.shp");
    if(wrench.frameCount()!=assets.wrench.size())throw std::runtime_error("Unexpected original wrench animation");
    for(std::size_t i=0;i<assets.wrench.size();++i) {
      const auto frame=wrench.decodeFrame(i);auto& texture=assets.wrench[i];texture.width=frame.width;texture.height=frame.height;
      // This UI SHP uses mousepal, like the cursors; the unit/player palette
      // turns the silver wrench into green/blue speckles.
      texture.colorMode=UiTextureColorMode::Rgba;texture.paletteKind=UiTexturePaletteKind::None;
      texture.rgba=wrench.decodeFrameRgba(i,palette);
      glGenTextures(1,&texture.texture);glBindTexture(GL_TEXTURE_2D,texture.texture);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
      glPixelStorei(GL_UNPACK_ALIGNMENT,1);
      glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,frame.width,frame.height,0,GL_RGBA,GL_UNSIGNED_BYTE,texture.rgba.data());
      glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    }
  }catch(...){destroyBuildingActionVisualAssets(assets);throw;}
}
void destroyBuildingActionVisualAssets(BuildingActionVisualAssets& assets) {
  if(SDL_WasInit(SDL_INIT_VIDEO))SDL_SetCursor(SDL_GetDefaultCursor());
  for(auto& c:assets.repair)if(c){SDL_FreeCursor(c);c=nullptr;}
  for(auto& c:assets.sell)if(c){SDL_FreeCursor(c);c=nullptr;}
  for(auto* c:{&assets.repairBlocked,&assets.sellBlocked})if(*c){SDL_FreeCursor(*c);*c=nullptr;}
  for(auto& texture:assets.wrench)if(texture.texture){glDeleteTextures(1,&texture.texture);texture={};}
}
void updateBuildingActionCursor(const BuildingActionVisualAssets& assets,BuildingActionMode mode,bool allowed,GameFrame frame) {
  auto* cursor=SDL_GetDefaultCursor();
  if(mode==BuildingActionMode::Repair)cursor=allowed?assets.repair[(frame/2)%assets.repair.size()]:assets.repairBlocked;
  if(mode==BuildingActionMode::Sell)cursor=allowed?assets.sell[(frame/2)%assets.sell.size()]:assets.sellBlocked;
  if(cursor && SDL_GetCursor()!=cursor)SDL_SetCursor(cursor);
}
void drawBuildingRepairs(Renderer2D& renderer,const BuildingActionVisualAssets& assets,const BuildingAssetMap& buildings,
                        std::span<const BuildingInstance> instances,Vec2 origin,float w,float h) {
  renderer.beginUiPass();
  for(const auto& building:instances) {
    if(!building.repairing || building.state!=BuildingState::Complete)continue;
    const auto& asset=assetForInstance(buildings,building);
    const auto frame=building.visualFrame>=building.repairStartedFrame?building.visualFrame-building.repairStartedFrame:0;
    const auto& texture=assets.wrench[(frame/2)%assets.wrench.size()];if(!texture.texture)continue;
    const float scale=w/60;
    auto point=buildingDrawCenter(building.placement,origin,w,h);
    point.y-=asset.art.height?std::clamp(*asset.art.height*h*.5f,18.f,190.f):asset.completeFrame.height*.5f;
    const float x=point.x-texture.width*scale/2,y=point.y-texture.height*scale;
    const float right=x+texture.width*scale,bottom=y+texture.height*scale;
    const float alpha=building.repairPaused && (frame/5)%2?.3f:1;
    const std::array<RenderVertex,6> quad{{{x,y,0,0,0,1,1,1,alpha},{right,y,0,1,0,1,1,1,alpha},{right,bottom,0,1,1,1,1,1,alpha},
      {x,y,0,0,0,1,1,1,alpha},{right,bottom,0,1,1,1,1,1,alpha},{x,bottom,0,0,1,1,1,1,alpha}}};
    renderer.draw(GL_TRIANGLES,texture,quad.data(),quad.size());
  }
}
}
