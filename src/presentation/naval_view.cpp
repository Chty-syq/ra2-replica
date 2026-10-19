#include "naval_view.h"
#include "tmp_tile.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace ra2 {
void destroyNavalVisualAssets(NavalVisualAssets& assets){for(auto* collection:{&assets.water,&assets.shore})for(auto& theater:*collection)for(auto& tile:theater){if(tile.texture)glDeleteTextures(1,&tile.texture);tile={};}}
void loadNavalVisualAssets(NavalVisualAssets& assets,const std::filesystem::path& root){
 destroyNavalVisualAssets(assets);
 try {
  for(int theater=0;theater<3;++theater){const auto folder=root/"terrain/naval"/std::array{"temperate","snow","urban"}[theater];const auto palette=Palette::load(folder/"iso.pal");const auto ext=std::array{".tem",".sno",".urb"}[theater];
   for(int kind=0;kind<2;++kind){const auto cells=TmpFile::load(folder/(std::string(kind?"shore09":"water01")+ext)).decodeCells(palette);if(cells.size()!=4)throw std::runtime_error("Expected original 2x2 naval TMP template");
    for(int i=0;i<4;++i){const auto& cell=cells[i];const int land=kind?(i%2?10:14):9;if(cell.width!=60 || cell.height!=30 || cell.landType!=land)throw std::runtime_error("Naval map terrain does not match original TMP LandType");
     auto& texture=(kind?assets.shore:assets.water)[theater][i];texture.width=60;texture.height=30;texture.rgba=cell.rgba;
     glGenTextures(1,&texture.texture);glBindTexture(GL_TEXTURE_2D,texture.texture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,60,30,0,GL_RGBA,GL_UNSIGNED_BYTE,texture.rgba.data());
    }
   }
  }
 }catch(...){destroyNavalVisualAssets(assets);throw;}
}
void initializeNavalCoast(World& world,int shoreX,int top,int bottom,int waterRight){
 if(shoreX<-1000 || top<-1000 || bottom<top || bottom-top>256 || waterRight<shoreX+2 || waterRight-shoreX>256)throw std::invalid_argument("Invalid naval training coast bounds");
 for(int x=shoreX;x<=waterRight;++x)for(int y=top;y<=bottom;++y){TerrainSurface surface;surface.land=x==shoreX?"ROUGH":x==shoreX+1?"BEACH":"WATER";world.setTerrain({x,y},surface);}
}
void drawNavalTerrain(Renderer2D& renderer,const NavalVisualAssets& assets,TheaterStyle style,const MapGrid& map,Vec2 origin,float w,float h,int viewportWidth,int viewportHeight){
 const auto logical=[&](float sx,float sy){const auto a=(sx-origin.x)/(w*.5f),b=(sy-origin.y)/(h*.5f);return Vec2{(a+b)*.5f,(b-a)*.5f};};
 const std::array corners{logical(0,0),logical(float(viewportWidth),0),logical(0,float(viewportHeight)),logical(float(viewportWidth),float(viewportHeight))};
 float minX=corners[0].x,maxX=minX,minY=corners[0].y,maxY=minY;for(auto c:corners){minX=std::min(minX,c.x);maxX=std::max(maxX,c.x);minY=std::min(minY,c.y);maxY=std::max(maxY,c.y);}
 renderer.beginWorldPass();
 for(int x=int(minX)-2;x<=int(maxX)+2;++x)for(int y=int(minY)-2;y<=int(maxY)+2;++y){const auto land=canonicalId(map.cell({x,y}).surface.land);const UiTexture* texture=nullptr;const auto theater=int(style);const int row=((y%2)+2)%2;
  if(land=="WATER")texture=&assets.water[theater][((x%2)+2)%2+2*row];
  else if(land=="ROUGH" || land=="BEACH")texture=&assets.shore[theater][(land=="BEACH"?1:0)+2*row];
  if(!texture || !texture->texture)continue;
  const float cx=origin.x+(x-y)*w*.5f,cy=origin.y+(x+y)*h*.5f-map.cell({x,y}).surface.heightLeptons*h/256;
  const float left=cx-w*.5f,top=cy-h*.5f,z=depthFromLogicalCenter({float(x),float(y)},w,h)+.00005f;
  const std::array<RenderVertex,6> vertices{{{left,top,z,0,0},{left+w,top,z,1,0},{left+w,top+h,z,1,1},{left,top,z,0,0},{left+w,top+h,z,1,1},{left,top+h,z,0,1}}};
  renderer.draw(GL_TRIANGLES,*texture,vertices.data(),vertices.size());
 }
}
void drawSonicWaves(Renderer2D& renderer,std::span<const BeamSnapshot> beams,const DefinitionRegistry& definitions,Tick now,Vec2 origin,float w,float h) {
 renderer.beginWorldPass();glDepthMask(GL_FALSE);
 for(const auto& beam:beams) {
  const auto& weapon=definitions.weapon(beam.weapon);if(!weapon.sonic || now<beam.started || now-beam.started>=beam.duration)continue;
  const float dx=(beam.end.x-beam.start.x)/1000.f,dy=(beam.end.y-beam.start.y)/1000.f,length=std::hypot(dx,dy);if(length<.01f)continue;
  const float ux=dx/length,uy=dy/length,front=std::min(length,float(now-beam.started+1)*weapon.speed/256.f);
  const auto vertex=[&](float forward,float side,float alpha) {
    const float x=beam.start.x/1000.f+ux*forward-uy*side,y=beam.start.y/1000.f+uy*forward+ux*side;
    return RenderVertex{origin.x+(x-y)*w*.5f,origin.y+(x+y)*h*.5f-beam.startHeight*h/256,
      depthFromLogicalCenter({x,y},w,h)-.00012f,0,0,.31f,.96f,1.f,alpha};
  };
  // The original Sonic weapon is a procedural WaveClass, not a SHP or laser.
  // Advancing curved fronts preserve its cyan, translucent fan silhouette.
  for(int ring=0;ring<5;++ring) {
    const float position=front-ring*.22f;if(position<=0)continue;
    const float radius=.15f+position*.10f,opacity=.5f*(1.f-ring/5.f);
    for(int step=0;step<12;++step) {
      const float side0=(step/6.f-1)*radius,side1=((step+1)/6.f-1)*radius;
      const auto a=vertex(position-side0*side0/(radius*3),side0,opacity),b=vertex(position-side1*side1/(radius*3),side1,opacity);
      renderer.drawLaserLine(a,b,std::max(.75f,w/60.f),opacity);
    }
  }
 }
 glDepthMask(GL_TRUE);
}

}
