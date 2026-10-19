#include "tmp_tile.h"

#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>

TmpFile TmpFile::load(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Failed to open TMP tile: " + path.string());
  }

  TmpFile file;
  file.bytes_ = std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                          std::istreambuf_iterator<char>());

  if (file.bytes_.size() < 20) {
    throw std::runtime_error("TMP file too small: " + path.string());
  }

  file.blockCountX_ = readU32(file.bytes_, 0);
  file.blockCountY_ = readU32(file.bytes_, 4);
  file.imageWidth_ = readU32(file.bytes_, 8);
  file.imageHeight_ = readU32(file.bytes_, 12);

  return file;
}

TmpTile TmpFile::decodeSingleTile(const Palette& palette) const {
  if (blockCountX_ != 1 || blockCountY_ != 1) {
    throw std::runtime_error("This demo only supports single-cell TMP tiles for now");
  }

  const auto cellHeaderOffset = readU32(bytes_, 16);
  if (cellHeaderOffset < 20 || cellHeaderOffset > bytes_.size() ||
      bytes_.size() - cellHeaderOffset < 32) {
    throw std::runtime_error("TMP image data offset is out of range");
  }
  const std::size_t imageDataOffset = static_cast<std::size_t>(cellHeaderOffset) + 32;

  if (imageWidth_ == 0 || imageHeight_ == 0 || (imageHeight_ % 2) != 0) {
    throw std::runtime_error("Unexpected TMP dimensions");
  }

  const auto halfRows = imageHeight_ / 2;
  if (imageWidth_ % halfRows != 0) {
    throw std::runtime_error("Unexpected TMP diamond dimensions");
  }
  const auto rowStep = imageWidth_ / halfRows;
  const auto payloadSize = static_cast<std::uint64_t>(imageWidth_) * (halfRows + 1);
  const auto pixelCount = static_cast<std::uint64_t>(imageWidth_) * imageHeight_;
  if (payloadSize > bytes_.size() - imageDataOffset ||
      pixelCount > std::numeric_limits<std::size_t>::max() / 4) {
    throw std::runtime_error("TMP image payload overruns file or dimensions are too large");
  }

  TmpTile tile;
  tile.width = imageWidth_;
  tile.height = imageHeight_;
  tile.rgba.assign(static_cast<std::size_t>(pixelCount) * 4, 0);

  std::size_t src = imageDataOffset;
  for (std::uint32_t y = 0; y < imageHeight_; ++y) {
    const auto rowWidth = y < halfRows
      ? rowStep * (y + 1)
      : rowStep * (imageHeight_ - y);
    const auto rowStartX = (imageWidth_ - rowWidth) / 2;

    for (std::uint32_t x = 0; x < rowWidth; ++x) {
      if (src >= bytes_.size()) {
        throw std::runtime_error("TMP image payload overruns file");
      }

      const auto color = palette.color(bytes_[src++]);
      const std::size_t dst = (static_cast<std::size_t>(y) * imageWidth_ + rowStartX + x) * 4;
      tile.rgba[dst + 0] = color.r;
      tile.rgba[dst + 1] = color.g;
      tile.rgba[dst + 2] = color.b;
      tile.rgba[dst + 3] = 255;
    }
  }

  return tile;
}

std::vector<TmpTile> TmpFile::decodeCells(const Palette& palette) const {
  const auto count=std::uint64_t(blockCountX_)*blockCountY_;
  if(!count || count>100 || imageWidth_!=60 || imageHeight_!=30 || 16+count*4>bytes_.size())
    throw std::runtime_error("Unsupported native TMP template dimensions");
  std::vector<TmpTile> cells;cells.reserve(std::size_t(count));
  for(std::size_t index=0;index<count;++index) {
    TmpTile tile;const auto offset=readU32(bytes_,16+index*4);
    if(!offset){cells.push_back(std::move(tile));continue;}
    if(offset<16+count*4 || offset>bytes_.size() || bytes_.size()-offset<52+900)
      throw std::runtime_error("Native TMP cell header/pixels outside file");
    if(bytes_[offset+40] || bytes_[offset+42])
      throw std::runtime_error("Naval TMP cells must be flat");
    tile.width=60;tile.height=30;tile.landType=bytes_[offset+41];tile.heightLevel=bytes_[offset+40];tile.rgba.assign(60*30*4,0);
    std::size_t source=offset+52;int width=4;
    for(int y=0;y<30;++y) {
      const int left=(60-width)/2;
      for(int x=0;x<width;++x) {
        const auto color=palette.color(bytes_.at(source++));const auto dest=(y*60+left+x)*4;
        tile.rgba[dest]=color.r;tile.rgba[dest+1]=color.g;tile.rgba[dest+2]=color.b;tile.rgba[dest+3]=255;
      }
      width+=y<14?4:-4;
    }
    if(bytes_[offset+36]&1) {
      const auto ex=int(readU32(bytes_,offset+20))-(int(index%blockCountX_)-int(index/blockCountX_))*30;
      const auto ey=int(readU32(bytes_,offset+24))-(int(index%blockCountX_)+int(index/blockCountX_))*15;
      const auto ew=readU32(bytes_,offset+28),eh=readU32(bytes_,offset+32),extra=readU32(bytes_,offset+8);
      const auto count=std::uint64_t(ew)*eh;
      if(ex<0 || ey<0 || ew>60 || eh>30 || ex+ew>60 || ey+eh>30 || extra>bytes_.size()-offset || count>bytes_.size()-offset-extra)
        throw std::runtime_error("Native TMP extra image outside supported flat canvas/file");
      for(unsigned y=0;y<eh;++y)for(unsigned x=0;x<ew;++x)if(const auto index=bytes_[offset+extra+y*ew+x]) {
        const auto color=palette.color(index);const auto dest=((ey+y)*60+ex+x)*4;
        tile.rgba[dest]=color.r;tile.rgba[dest+1]=color.g;tile.rgba[dest+2]=color.b;tile.rgba[dest+3]=255;
      }
    }
    cells.push_back(std::move(tile));
  }
  return cells;
}

std::uint32_t TmpFile::readU32(const std::vector<std::uint8_t>& bytes, const std::size_t offset) {
  return static_cast<std::uint32_t>(bytes.at(offset)) |
         (static_cast<std::uint32_t>(bytes.at(offset + 1)) << 8) |
         (static_cast<std::uint32_t>(bytes.at(offset + 2)) << 16) |
         (static_cast<std::uint32_t>(bytes.at(offset + 3)) << 24);
}
