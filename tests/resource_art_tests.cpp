#include "resource_art.h"
#include "shp_ts.h"
#include "test_support.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <set>

namespace {
void selection() {
  require(ra2::resourceDensityFrame(0,32)==-1, "Depleted cells must not draw a sprite");
  require(ra2::resourceDensityFrame(1,32)==0, "Last ore must remain visible");
  require(ra2::resourceDensityFrame(std::numeric_limits<int>::max(),32)==11, "Overfull deposit indexed a shadow frame");
  for (const int full : {1,12,24,32,1000000}) {
    int previous=-1;
    for (int amount=1;amount<=full;++amount) {
      const int frame=ra2::resourceDensityFrame(amount,full);
      require(frame>=previous && frame<12, "Density must increase monotonically within visible frames");
      previous=frame;
    }
    require(previous==11, "Full deposits must reach the richest frame");
  }
  std::set<int> variants;
  for (int x=-20;x<=20;++x) for(int y=-20;y<=20;++y) {
    const auto variant=ra2::resourceVariant({x,y});
    require(variant>=0 && variant<12, "Cell variant out of range");
    variants.insert(variant);
  }
  require(variants.size()==12, "Map variation does not use all original flat overlays");
  // Logical +X, +Y, -X, -Y point SE, SW, NW, NE on screen.
  require(ra2::gatherDirection(0)==5 && ra2::gatherDirection(16384)==3 &&
          ra2::gatherDirection(32768)==1 && ra2::gatherDirection(49152)==7,
          "Gathering sprite does not face the miner's heading");
  require(ra2::gatherDirection(-1)==ra2::gatherDirection(65535), "Heading wrap changed gathering direction");
}
void matchesFrame(const ra2::ResourceAtlasPixels& atlas, int column, int row, const DecodedFrame& source) {
  const int width=atlas.columns*atlas.frameWidth;
  for(int y=0;y<source.height;++y)
    require(std::equal(source.indices.begin()+y*source.width,source.indices.begin()+(y+1)*source.width,
                       atlas.indices.begin()+(row*atlas.frameHeight+y)*width+column*atlas.frameWidth),
            "Atlas changed original indices, transparency, or frame offsets");
}
void originalArt(const std::filesystem::path& root) {
  constexpr std::array<const char*,3> theaters{"temperate","snow","urban"};
  constexpr std::array<const char*,3> extensions{".tem",".sno",".urb"};
  for(std::size_t theater=0;theater<theaters.size();++theater) for(const char* family:{"tib","gem"}) {
    const auto directory=root/"terrain"/"resources"/theaters[theater];
    const auto atlas=ra2::loadResourceAtlas(directory,family,extensions[theater]);
    require(atlas.indices.size()==720*720, "Unexpected overlay atlas dimensions");
    for(int variant=0;variant<12;++variant) {
      const auto name=std::string(family)+(variant<9?"0":"")+std::to_string(variant+1)+extensions[theater];
      const auto shp=ShpTsFile::load(directory/name);
      const bool shifted=std::string_view(family)=="tib" && variant==1;
      const auto visible=[](const auto& frame){return std::count_if(frame.indices.begin(),frame.indices.end(),[](auto index){return index!=0;});};
      std::ptrdiff_t previous=0;
      for(int density=0;density<12;++density) {
        const auto source=shp.decodeFrame(shifted?(density+1)%12:density);
        matchesFrame(atlas,density,variant,source);
        require(visible(source)>previous, "Density does not progressively thin: " + (directory/name).string());
        previous=visible(source);
      }
    }
  }
  const auto gathering=root/"effects"/"harvesting"/"oregath.shp";
  const auto atlas=ra2::loadGatherAtlas(gathering);
  const auto shp=ShpTsFile::load(gathering);
  for(int direction=0;direction<8;++direction) for(int frame=0;frame<15;++frame)
    matchesFrame(atlas,frame,direction,shp.decodeFrame(direction*15+frame));
}
}
int main(int argc,char** argv) {
  try {
    require(argc==2,"Expected assets root");
    selection();
    originalArt(argv[1]);
    std::cout<<"Original resource atlases, densities and gathering directions passed\n";
    return 0;
  } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
