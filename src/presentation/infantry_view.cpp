#include "infantry_view.h"
#include "shp_ts.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
namespace ra2 {
namespace {
Vec2 anchor(const InfantryVisual& unit,Vec2 origin,float w,float h) {
  return {origin.x+(unit.position.x-unit.position.y)*w*.5f,
          origin.y+(unit.position.x+unit.position.y)*h*.5f-unit.heightLeptons*h/256};
}
UiTexture croppedOverlay(const ShpTsFile& shp,std::size_t index,const Palette& palette,bool preserveCanvas=false) {
  const auto frame=shp.decodeFrame(index);
  int left=frame.width,top=frame.height,right=-1,bottom=-1;
  for(int y=0;y<frame.height;++y)for(int x=0;x<frame.width;++x)if(frame.indices[y*frame.width+x]) {
    left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);
  }
  if(preserveCanvas){left=top=0;right=frame.width-1;bottom=frame.height-1;}
  if(right<left)throw std::runtime_error("Empty infantry overlay frame");
  UiTexture texture;texture.width=right-left+1;texture.height=bottom-top+1;
  texture.rgba.reserve(static_cast<std::size_t>(texture.width)*texture.height*4);
  for(int y=top;y<=bottom;++y)for(int x=left;x<=right;++x) {
    const auto color=palette.color(frame.indices[y*frame.width+x]);
    texture.rgba.insert(texture.rgba.end(),{color.r,color.g,color.b,color.a});
  }
  glGenTextures(1,&texture.texture);glBindTexture(GL_TEXTURE_2D,texture.texture);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,texture.width,texture.height,0,GL_RGBA,GL_UNSIGNED_BYTE,texture.rgba.data());
  return texture;
}
void overlayQuad(Renderer2D& renderer,const UiTexture& texture,float x,float y,float scale) {
  const float w=texture.width*scale,h=texture.height*scale;
  const std::array<RenderVertex,6> vertices{{{x,y,0,0,0},{x+w,y,0,1,0},{x+w,y+h,0,1,1},
    {x,y,0,0,0},{x+w,y+h,0,1,1},{x,y+h,0,0,1}}};
  renderer.draw(GL_TRIANGLES,texture,vertices.data(),vertices.size());
}
int healthCondition(const IniDocument& rules,std::string_view name,double fallback) {
  auto text=rules.string("AudioVisual",name,std::to_string(fallback));
  const bool percent=text.ends_with('%');if(percent)text.pop_back();
  const auto value=IniDocument::parse("[Value]\nRatio="+text).number("Value","Ratio")/(percent?100:1);
  if(value<0 || value>1)rules.invalid("AudioVisual",name,"expected health ratio 0..1 or 0..100%");
  return static_cast<int>(std::lround(value*10000));
}
}
void loadInfantryVisualAssets(InfantryVisualAssets& assets,const DefinitionRegistry& definitions,const std::filesystem::path& root) {
  destroyInfantryVisualAssets(assets);
  try {
    const auto overlays=root/"ui"/"unit_overlays"/"common";
    const auto palette=Palette::load(root/"palettes"/"ui"/"palette.pal");
    // pipbrd frame 0 is the vehicle bar. Frame 1 contains the native 18x4
    // infantry bar in a 36x4 canvas: crop its transparent right half, not scale it.
    assets.vehicleHealthBorder=croppedOverlay(ShpTsFile::load(overlays/"pipbrd.shp"),0,palette);
    assets.healthBorder=croppedOverlay(ShpTsFile::load(overlays/"pipbrd.shp"),1,palette);
    const auto pips=ShpTsFile::load(overlays/"pips.shp");
    assets.veteranPip=croppedOverlay(pips,13,palette);
    assets.ammoPip=croppedOverlay(ShpTsFile::load(overlays/"pips2.shp"),13,palette);
    for(std::size_t i=0;i<assets.healthPips.size();++i)assets.healthPips[i]=croppedOverlay(pips,15+i,palette);
    for(std::size_t i=0;i<assets.passengerPips.size();++i)assets.passengerPips[i]=croppedOverlay(pips,std::array{6,9,11}[i],palette);
    assets.conditionRed=healthCondition(definitions.rules(),"ConditionRed",.25);
    assets.conditionYellow=healthCondition(definitions.rules(),"ConditionYellow",.5);
    if(assets.conditionRed>assets.conditionYellow)throw std::runtime_error("ConditionRed exceeds ConditionYellow");
    const auto bombs=ShpTsFile::load(root/"ui"/"ivan_bombs"/"bombcurs.shp");
    if(bombs.frameCount()!=13)throw std::runtime_error("Expected 13 original bomb cursor frames");
    const auto mousePalette=Palette::load(root/"palettes"/"ui"/"mousepal.pal");
    for(int i=0;i<13;++i)assets.bombIcons.push_back(croppedOverlay(bombs,i,mousePalette,true));
    assets.enemyHealth=definitions.rules().boolean("AudioVisual","EnemyHealth",true);
    const auto loadAtlas=[&](const std::string& id,const std::filesystem::path& path,bool noShadow=false) {
      const auto shp=ShpTsFile::load(path);
      const auto first=shp.decodeFrame(0);auto& atlas=assets.atlases[id];
      atlas.frameWidth=first.width;atlas.frameHeight=first.height;atlas.frames=static_cast<int>(shp.frameCount()/(noShadow?1:2));
      atlas.rows=(atlas.frames+atlas.columns-1)/atlas.columns;
      auto& texture=atlas.texture;texture.width=atlas.columns*first.width;texture.height=atlas.rows*first.height;
      texture.colorMode=UiTextureColorMode::IndexedPalette;texture.paletteKind=UiTexturePaletteKind::Unit;texture.paletteRemap=true;
      std::vector<std::uint8_t> pixels(static_cast<std::size_t>(texture.width)*texture.height);
      for(int i=0;i<atlas.frames;++i) {
        const auto frame=shp.decodeFrame(i);
        for(int y=0;y<first.height;++y)std::copy_n(frame.indices.data()+y*first.width,first.width,
          pixels.data()+(i/atlas.columns*first.height+y)*texture.width+i%atlas.columns*first.width);
      }
      glGenTextures(1,&texture.texture);glBindTexture(GL_TEXTURE_2D,texture.texture);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
      glPixelStorei(GL_UNPACK_ALIGNMENT,1);
      glTexImage2D(GL_TEXTURE_2D,0,GL_R8,texture.width,texture.height,0,GL_RED,GL_UNSIGNED_BYTE,pixels.data());
      glPixelStorei(GL_UNPACK_ALIGNMENT,4);
      std::vector<std::uint8_t> shadows(static_cast<std::size_t>(texture.width)*texture.height*4);bool hasShadow=false;
      for(int i=0;!noShadow && i<atlas.frames;++i) {
        const auto frame=shp.decodeFrame(i+atlas.frames);
        for(int y=0;y<first.height;++y)for(int x=0;x<first.width;++x)if(frame.indices[y*first.width+x]) {
          hasShadow=true;shadows[((i/atlas.columns*first.height+y)*texture.width+i%atlas.columns*first.width+x)*4+3]=96;
        }
      }
      if(hasShadow) {
        auto& shadow=atlas.shadow;shadow.width=texture.width;shadow.height=texture.height;
        glGenTextures(1,&shadow.texture);glBindTexture(GL_TEXTURE_2D,shadow.texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,shadow.width,shadow.height,0,GL_RGBA,GL_UNSIGNED_BYTE,shadows.data());
      }
    };
    for(const auto& [id,type]:definitions.units()) {
      if((!type.infantry && !type.spriteVehicle) || !type.unsupported.empty())continue;
      const auto folder=root/(type.infantry?"infantry":"vehicles")/type.visual.folder;
      loadAtlas(id,folder/type.visual.body,definitions.rules().boolean(id,"NoShadow"));
      if(!type.arcticBody.empty())loadAtlas(id+"@ARCTIC",folder/type.arcticBody);
      if(type.leap && !type.leap->naval)loadAtlas(id+"@JUMP",folder/type.leap->image);
    }
  } catch(...) {destroyInfantryVisualAssets(assets);throw;}
}
void destroyInfantryVisualAssets(InfantryVisualAssets& assets) {
  for(auto& [id,atlas]:assets.atlases){(void)id;if(atlas.texture.texture)glDeleteTextures(1,&atlas.texture.texture);if(atlas.shadow.texture)glDeleteTextures(1,&atlas.shadow.texture);}
  assets.atlases.clear();
  const auto destroy=[](UiTexture& texture){if(texture.texture)glDeleteTextures(1,&texture.texture);texture={};};
  destroy(assets.healthBorder);destroy(assets.vehicleHealthBorder);destroy(assets.veteranPip);destroy(assets.ammoPip);
  for(auto& icon:assets.bombIcons)destroy(icon);assets.bombIcons.clear();
  for(auto& pip:assets.healthPips)destroy(pip);
  for(auto& pip:assets.passengerPips)destroy(pip);
}
void drawCargoSelection(Renderer2D& renderer,const InfantryVisualAssets& assets,std::span<const EntitySnapshot> entities,
                        std::span<const EntityId> selected,Vec2 origin,float w,float h) {
  for(const auto& host:entities)if(host.veteran && !host.infantry && !host.containedBy && host.owner==0 && std::find(selected.begin(),selected.end(),host.id)!=selected.end()) {
    const float x=origin.x+(host.position.x-host.position.y)*w/2000.f+20,
                y=origin.y+(host.position.x+host.position.y)*h/2000.f-host.heightLeptons*h/256-30;
    overlayQuad(renderer,assets.veteranPip,std::round(x),std::round(y),1);
  }
  for(const auto& host:entities)if(host.aircraft && host.aircraft->ammo>0 && host.owner==0 && std::find(selected.begin(),selected.end(),host.id)!=selected.end()) {
    const float x=std::round(origin.x+(host.position.x-host.position.y)*w/2000.f),
                y=std::round(origin.y+(host.position.x+host.position.y)*h/2000.f-host.heightLeptons*h/256+5);
    for(int slot=0;slot<host.aircraft->ammo;++slot)overlayQuad(renderer,assets.ammoPip,x+slot*6,y,1);
  }
  for(const auto& host:entities)if(host.cargo && host.owner==0 && std::find(selected.begin(),selected.end(),host.id)!=selected.end()) {
    const auto& cargo=*host.cargo;
    const float x=std::round(origin.x+(host.position.x-host.position.y)*w/2000.f),
                y=std::round(origin.y+(host.position.x+host.position.y)*h/2000.f-host.heightLeptons*h/256+5);
    const int columns=std::min(10,cargo.capacity);
    for(int slot=0;slot<cargo.capacity;++slot) {
      const auto& texture=assets.passengerPips[slot<cargo.used?1:0];
      overlayQuad(renderer,texture,x-columns*3+(slot%10)*6,y+(slot/10)*13,1);
    }
  }
}
void drawInfantry(Renderer2D& renderer,const InfantryVisualAssets& assets,std::span<const InfantryVisual> units,
                  Vec2 origin,float w,float h,Rgba friendly,Rgba enemy,bool arctic) {
  renderer.beginWorldPass();
  // Native SHP shadow frames occupy the second half. Draw them at terrain
  // height, independently of the airborne body and its depth.
  for(const auto& unit:units) {
    const auto& atlas=assets.atlases.at(unit.type+(unit.leaping?"@JUMP":arctic && assets.atlases.contains(unit.type+"@ARCTIC")?"@ARCTIC":""));if(!atlas.shadow.texture)continue;
    auto ground=unit;ground.heightLeptons=unit.groundHeightLeptons;const auto p=anchor(ground,origin,w,h);
    const float width=atlas.frameWidth*w/60,height=atlas.frameHeight*h/30,x=p.x-width*.5f,y=p.y-height*.5f;
    const float z=std::clamp(depthFromLogicalCenter(unit.position,w,h)-.000005f,.001f,.999f);
    const float u=float(unit.frame%atlas.columns)/atlas.columns,v=float(unit.frame/atlas.columns)/atlas.rows;
    const float u1=u+1.f/atlas.columns,v1=v+1.f/atlas.rows;
    const std::array<RenderVertex,6> vertices{{{x,y,z,u,v},{x+width,y,z,u1,v},{x+width,y+height,z,u1,v1},
      {x,y,z,u,v},{x+width,y+height,z,u1,v1},{x,y+height,z,u,v1}}};
    glDepthMask(GL_FALSE);renderer.draw(GL_TRIANGLES,atlas.shadow,vertices.data(),vertices.size());glDepthMask(GL_TRUE);
  }
  for(const auto& unit:units) {
    const auto& atlas=assets.atlases.at(unit.type+(unit.leaping?"@JUMP":arctic && assets.atlases.contains(unit.type+"@ARCTIC")?"@ARCTIC":""));if(unit.frame<0 || unit.frame>=atlas.frames)throw std::runtime_error("Infantry frame out of range");
    const auto p=anchor(unit,origin,w,h);const float width=atlas.frameWidth*w/60,height=atlas.frameHeight*h/30;
    const float x=p.x-width*.5f,y=p.y-height*.5f,z=std::clamp(depthFromLogicalCenter(unit.position,w,h)-.00001f-(unit.airborne?unit.heightLeptons*h/256/16384.f:0),.001f,.999f);
    const float u=float(unit.frame%atlas.columns)/atlas.columns,v=float(unit.frame/atlas.columns)/atlas.rows;
    const float u1=u+1.f/atlas.columns,v1=v+1.f/atlas.rows;
    const std::array<RenderVertex,6> vertices{{{x,y,z,u,v},{x+width,y,z,u1,v},{x+width,y+height,z,u1,v1},
      {x,y,z,u,v},{x+width,y+height,z,u1,v1},{x,y+height,z,u,v1}}};
    const float alpha=(unit.reforming?.45f:1.f)*unit.opacity;
    auto tinted=vertices;
    for(auto& v:tinted){if(unit.frozen || unit.reforming){v.r=.4f;v.g=.7f;v.b=1;}v.a=alpha;}
    renderer.setRemapColor(unit.owner?enemy:friendly);renderer.draw(GL_TRIANGLES,atlas.texture,tinted.data(),tinted.size());
  }
  renderer.setRemapColor(friendly);
}
void drawInfantrySelection(Renderer2D& renderer,const InfantryVisualAssets& assets,
                          std::span<const InfantryVisual> units,Vec2 origin,float w,float h,EntityId hovered) {
  const float scale=std::max(1.f,std::round(w/60.f));
  for(const auto& unit:units)if((unit.selected || (hovered && unit.id==hovered)) && !unit.corpse && !unit.leaping && unit.hp>0 && unit.maxHp>0) {
    const auto p=anchor(unit,origin,w,h);
    // Keep the native small bar tied to the ground anchor across animation frames.
    const auto& border=unit.vehicleSprite?assets.vehicleHealthBorder:assets.healthBorder;
    if(!border.texture || (unit.owner!=0 && !assets.enemyHealth))continue;
    const float x=std::round(p.x-border.width*scale*.5f),y=std::round(p.y-27*scale);
    overlayQuad(renderer,border,x,y,scale);
    if(unit.veteran && unit.owner==0)overlayQuad(renderer,assets.veteranPip,x+border.width*scale+scale,y-scale,scale);
    const auto hp=std::min(unit.hp,unit.maxHp);
    const auto band=std::int64_t(hp)*10000<=std::int64_t(unit.maxHp)*assets.conditionRed?2:
                    std::int64_t(hp)*10000<=std::int64_t(unit.maxHp)*assets.conditionYellow?1:0;
    const auto& pip=assets.healthPips[band];if(!pip.texture || pip.width<=0)continue;
    const int count=std::max(0,(border.width-2)/pip.width);
    const int lit=static_cast<int>((std::int64_t(hp)*count+unit.maxHp-1)/unit.maxHp);
    for(int i=0;i<lit;++i)overlayQuad(renderer,pip,x+(1+i*pip.width)*scale,y+scale,scale);
  }
}
void drawIvanBombIcons(Renderer2D& renderer,const InfantryVisualAssets& assets,std::span<const BombVisual> bombs,Vec2 origin,float w,float h) {
  for(const auto& bomb:bombs) {
    if(bomb.frame<0 || bomb.frame>=int(assets.bombIcons.size()))continue;
    const auto& icon=assets.bombIcons[bomb.frame];
    const float x=origin.x+(bomb.position.x-bomb.position.y)*w*.5f,y=origin.y+(bomb.position.x+bomb.position.y)*h*.5f-bomb.heightLeptons*h/256-30;
    overlayQuad(renderer,icon,x-icon.width*.5f,y-icon.height,1);
  }
}
EntityId hitTestInfantry(std::span<const InfantryVisual> units,float x,float y,Vec2 origin,float w,float h) {
  EntityId hit=0;float nearest=1e10f;
  for(const auto& unit:units)if(!unit.corpse && !unit.leaping && unit.hp>0) {
    const auto p=anchor(unit,origin,w,h);
    if(x<p.x-12 || x>p.x+12 || y<p.y-28 || y>p.y+7)continue;
    const float d=(x-p.x)*(x-p.x)+(y-p.y+12)*(y-p.y+12);
    if(d<nearest){nearest=d;hit=unit.id;}
  }
  return hit;
}
}
