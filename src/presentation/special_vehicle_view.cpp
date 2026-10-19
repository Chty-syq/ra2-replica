#include "special_vehicle_view.h"
#include "shp_ts.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <set>
namespace ra2 {
namespace {
UiTexture upload(const DecodedFrame& frame,bool shadow) {
  UiTexture texture;texture.width=frame.width;texture.height=frame.height;
  texture.colorMode=shadow?UiTextureColorMode::Rgba:UiTextureColorMode::IndexedPalette;
  texture.paletteKind=shadow?UiTexturePaletteKind::None:UiTexturePaletteKind::Terrain;
  std::vector<std::uint8_t> pixels;
  if(shadow)for(auto index:frame.indices)pixels.insert(pixels.end(),{0,0,0,static_cast<std::uint8_t>(index?128:0)});
  glGenTextures(1,&texture.texture);glBindTexture(GL_TEXTURE_2D,texture.texture);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT,1);
  glTexImage2D(GL_TEXTURE_2D,0,shadow?GL_RGBA8:GL_R8,frame.width,frame.height,0,shadow?GL_RGBA:GL_RED,GL_UNSIGNED_BYTE,
               shadow?pixels.data():frame.indices.data());
  glPixelStorei(GL_UNPACK_ALIGNMENT,4);return texture;
}
void quad(Renderer2D& renderer,const UiTexture& texture,Vec2 p,float scale,float z) {
  const float w=texture.width*scale,h=texture.height*scale,x=p.x-w/2,y=p.y-h/2;
  const std::array<RenderVertex,6> vertices{{{x,y,z,0,0},{x+w,y,z,1,0},{x+w,y+h,z,1,1},
    {x,y,z,0,0},{x+w,y+h,z,1,1},{x,y+h,z,0,1}}};
  renderer.draw(GL_TRIANGLES,texture,vertices.data(),vertices.size());
}
}
void loadSpecialVehicleAssets(SpecialVehicleAssets& assets,const DefinitionRegistry& definitions,const std::filesystem::path& root) {
  destroySpecialVehicleAssets(assets);std::set<std::string> names;
  for(const auto& [id,type]:definitions.units()){(void)id;for(const auto& name:type.disguises)names.insert(name);}
  try {
    constexpr std::array<const char*,3> folders{"temperate","snow","urban"},extensions{".tem",".sno",".urb"};
    for(int theater=0;theater<3;++theater)for(const auto& name:names) {
      auto filename=name;std::transform(filename.begin(),filename.end(),filename.begin(),[](unsigned char c){return std::tolower(c);});
      const auto shp=ShpTsFile::load(root/"terrain"/"disguises"/folders[theater]/(filename+extensions[theater]));
      auto& sprite=assets.trees[theater][name];sprite.body=upload(shp.decodeFrame(0),false);
      if(shp.frameCount()>1)sprite.shadow=upload(shp.decodeFrame(shp.frameCount()/2),true);
    }
  }catch(...){destroySpecialVehicleAssets(assets);throw;}
}
void destroySpecialVehicleAssets(SpecialVehicleAssets& assets) {
  for(auto& theater:assets.trees){for(auto& [name,sprite]:theater){(void)name;for(auto* t:{&sprite.body,&sprite.shadow})if(t->texture)glDeleteTextures(1,&t->texture);}theater.clear();}
}
void drawVehicleDisguise(Renderer2D& renderer,const SpecialVehicleAssets& assets,TheaterStyle theater,const RhinoUnitState& unit,Vec2 origin,float w,float h) {
  const auto& sprite=assets.trees[static_cast<std::size_t>(theater)].at(unit.disguiseImage);
  const float scale=w/60;
  // OpenRA RA2 ^Tree idle sequence: Offset=-1,-16,16, ShadowStart=1.
  // Keep the SHP canvas and its per-frame offsets intact for both frames.
  const Vec2 point{origin.x+(unit.tilePosition.x-unit.tilePosition.y)*w/2-scale,
    origin.y+(unit.tilePosition.x+unit.tilePosition.y)*h/2-unit.heightLeptons*h/256-16*scale};
  const auto groundDepth=depthFromLogicalCenter(unit.tilePosition,w,h);
  const auto depth=groundDepth-16*scale/16384.f;
  renderer.beginWorldPass();
  glDepthMask(GL_FALSE);
  if(sprite.shadow.texture)quad(renderer,sprite.shadow,point,scale,groundDepth+.00001f);
  glDepthMask(GL_TRUE);
  quad(renderer,sprite.body,point,scale,depth);
}
void drawLaserBeams(Renderer2D& renderer,std::span<const BeamSnapshot> beams,const DefinitionRegistry& definitions,Tick now,
                    Vec2 origin,float w,float h,Rgba friendly,Rgba enemy) {
  renderer.beginWorldPass();
  // Classic lines blend selected RGB channels toward the beam color; adding
  // light to every channel overexposes terrain and turns crossings white.
  // Test world depth without letting a beam occlude a subsequent beam.
  glDepthMask(GL_FALSE);
  for(const auto& beam:beams) {
    if(now<beam.started || now-beam.started>=beam.duration)continue;
    const auto& weapon=definitions.weapon(beam.weapon);
    if(weapon.electricBolt || weapon.radiationBeam || weapon.sonic)continue;
    const Vec2 a{origin.x+(beam.start.x-beam.start.y)*w/2000.f,origin.y+(beam.start.x+beam.start.y)*h/2000.f-beam.startHeight*h/256},
               b{origin.x+(beam.end.x-beam.end.y)*w/2000.f,origin.y+(beam.end.x+beam.end.y)*h/2000.f-beam.endHeight*h/256};
    const auto dx=b.x-a.x,dy=b.y-a.y,length=std::hypot(dx,dy);if(length<.1f)continue;
    const auto color=weapon.houseColor?(beam.owner?enemy:friendly):Rgba{static_cast<std::uint8_t>(weapon.laserColor[0]),static_cast<std::uint8_t>(weapon.laserColor[1]),static_cast<std::uint8_t>(weapon.laserColor[2]),255};
    const float intensity=std::clamp(1.f-float(now-beam.started)/std::max<Tick>(1,beam.duration),0.f,1.f);
    const auto z0=depthFromLogicalCenter({beam.start.x/1000.f,beam.start.y/1000.f},w,h)-.0005f,
               z1=depthFromLogicalCenter({beam.end.x/1000.f,beam.end.y/1000.f},w,h)-.0005f;
    // Use the house source color directly, retaining 5/6-bit precision.
    // Voxel map lighting and prism-support brightness are separate effects.
    const float red=(color.r/8)/31.f,green=(color.g/4)/63.f,blue=(color.b/8)/31.f;
    const RenderVertex start{a.x,a.y,z0,0,0,red,green,blue,1},end{b.x,b.y,z1,0,0,red,green,blue,1};
    glColorMask(color.r>0,color.g>0,color.b>0,GL_TRUE);
    // OpenRA RA2 Comet Width=50; its isometric TileScale is 1448, so this
    // projects to 2.072 pixels at a 60-pixel cell. This is an explicit
    // compatibility baseline, not Phobos Thickness mapped to screen pixels.
    renderer.drawLaserLine(start,end,50*w/1448.f,intensity);
  }
  glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  glDepthMask(GL_TRUE);
  glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
}
void drawElectricBolts(Renderer2D& renderer,std::span<const BeamSnapshot> beams,const DefinitionRegistry& definitions,Tick now,
                       Vec2 origin,float w,float h,const Palette& palette) {
  renderer.beginWorldPass();glDepthMask(GL_FALSE);
  // Phobos EBolt defaults: three strands, eight arcs, palette.pal 10/10/15
  // (5/5/15 for alternate color). The jagged path is a visual compatibility
  // approximation; a local hash never advances simulation random state.
  for(const auto& beam:beams) {
    if(now<beam.started || now-beam.started>=beam.duration)continue;
    const auto& weapon=definitions.weapon(beam.weapon);if(!weapon.electricBolt)continue;
    const Vec2 a{origin.x+(beam.start.x-beam.start.y)*w/2000.f,origin.y+(beam.start.x+beam.start.y)*h/2000.f-beam.startHeight*h/256},
               b{origin.x+(beam.end.x-beam.end.y)*w/2000.f,origin.y+(beam.end.x+beam.end.y)*h/2000.f-beam.endHeight*h/256};
    const auto dx=b.x-a.x,dy=b.y-a.y,length=std::hypot(dx,dy);if(length<.1f)continue;
    const float z0=depthFromLogicalCenter({beam.start.x/1000.f,beam.start.y/1000.f},w,h)-.0005f,
                z1=depthFromLogicalCenter({beam.end.x/1000.f,beam.end.y/1000.f},w,h)-.0005f;
    for(unsigned strand=0;strand<3;++strand) {
      const auto color=palette.color(strand==2?15:weapon.alternateBoltColor?5:10);
      const auto vertex=[&](float x,float y,float z) {return RenderVertex{x,y,z,0,0,color.r/255.f,color.g/255.f,color.b/255.f,1};};
      auto previous=vertex(a.x,a.y,z0);
      for(unsigned arc=1;arc<=8;++arc) {
        const float t=arc/8.f;
        std::uint32_t noise=static_cast<std::uint32_t>(beam.started*2654435761ULL+now*2246822519ULL+
          beam.start.x*3266489917ULL+beam.start.y*668265263ULL+strand*374761393U+arc*1274126177U);
        noise^=noise>>16;noise*=2246822519U;noise^=noise>>13;
        const float jitter=arc==8?0.f:(int(noise%2001)-1000)/1000.f*std::min(9*w/60.f,length/10);
        const auto next=vertex(a.x+dx*t-dy/length*jitter,a.y+dy*t+dx/length*jitter,z0+(z1-z0)*t);
        renderer.drawLaserLine(previous,next,std::max(.5f,w/60.f),1);previous=next;
      }
    }
  }
  glDepthMask(GL_TRUE);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
}
void drawRadiationBeams(Renderer2D& renderer,std::span<const BeamSnapshot> beams,const DefinitionRegistry& definitions,Tick now,Vec2 origin,float w,float h) {
  renderer.beginWorldPass();glDepthMask(GL_FALSE);
  for(const auto& beam:beams) {
    if(!definitions.weapon(beam.weapon).radiationBeam || now<beam.started || now-beam.started>=beam.duration)continue;
    const bool temporal=definitions.weapon(beam.weapon).temporal;
    const auto color=temporal?std::array<int,3>{80,150,255}:definitions.radiation().color;
    const float fade=1-float(now-beam.started)/std::max<Tick>(1,beam.duration);
    const auto point=[&](Position p,int height) {
      return RenderVertex{origin.x+(p.x-p.y)*w/2000.f,origin.y+(p.x+p.y)*h/2000.f-height*h/256.f,
        depthFromLogicalCenter({p.x/1000.f,p.y/1000.f},w,h)-.0005f,0,0,color[0]/255.f,color[1]/255.f,color[2]/255.f,1};
    };
    auto a=point(beam.start,beam.startHeight),b=point(beam.end,beam.endHeight);
    renderer.drawLaserLine(a,b,4*w/60.f,fade*.65f);
    a.r=b.r=.7f;a.g=b.g=temporal?.85f:1;a.b=b.b=temporal?1:.7f;
    renderer.drawLaserLine(a,b,std::max(.7f,w/60.f),fade);
  }
  glDepthMask(GL_TRUE);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
}
void drawRadiationField(Renderer2D& renderer,const World& world,const DefinitionRegistry& definitions,Vec2 origin,float w,float h) {
  if(world.radiationSites().empty())return;
  const auto& rules=definitions.radiation();std::set<std::pair<int,int>> cells;
  for(const auto& site:world.radiationSites()) {
    const int radius=(site.spread+999)/1000;
    for(int y=-radius;y<=radius;++y)for(int x=-radius;x<=radius;++x)if(std::int64_t(x)*x+std::int64_t(y)*y<=std::int64_t(radius)*radius)
      cells.emplace(site.cell.x+x,site.cell.y+y);
  }
  renderer.beginUiPass();
  for(const auto [x,y]:cells) {
    const auto level=world.radiationLight({x,y});if(!level)continue;
    const float alpha=std::clamp(float(std::int64_t(level)*rules.lightFactorMillionths/1000000)*rules.tintFactorMillionths/100000000.f,0.f,.6f);
    const auto v=[&](float px,float py) {
      const auto height=world.map().heightAt(static_cast<std::int64_t>(px*1000),static_cast<std::int64_t>(py*1000));
      return RenderVertex{origin.x+(px-py)*w/2,origin.y+(px+py)*h/2-height*h/256.f,0,0,0,
        rules.color[0]/255.f,rules.color[1]/255.f,rules.color[2]/255.f,alpha};
    };
    const auto a=v(x-.5f,y-.5f),b=v(x+.5f,y-.5f),c=v(x+.5f,y+.5f),d=v(x-.5f,y+.5f);
    const std::array vertices{a,b,c,a,c,d};renderer.draw(GL_TRIANGLES,renderer.whiteTexture(),vertices.data(),vertices.size());
  }
}
}
