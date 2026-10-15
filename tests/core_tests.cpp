#include "pathfinding.h"
#include "shp_ts.h"
#include "tmp_tile.h"
#include "vpl_file.h"
#include "vxl_file.h"
#include "test_support.h"

#include <iostream>
#include <limits>

namespace {
std::vector<std::uint8_t> shpBytes(const std::uint16_t width,
                                 const std::uint16_t height,
                                 const std::uint32_t flags,
                                 const std::vector<std::uint8_t>& payload) {
  std::vector<std::uint8_t> bytes(32, 0);
  put16(bytes, 2, width);
  put16(bytes, 4, height);
  put16(bytes, 6, 1);
  put16(bytes, 12, width);
  put16(bytes, 14, height);
  put32(bytes, 16, flags);
  put32(bytes, 28, 32);
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes;
}

DecodedFrame decode(const std::vector<std::uint8_t>& bytes) {
  const BinaryFixture fixture(bytes);
  return ShpTsFile::load(fixture.path()).decodeFrame(0);
}

void shortRleRow() {
  const auto frame = decode(shpBytes(2, 1, 3, {4, 0, 7, 8}));
  require(frame.indices == std::vector<std::uint8_t>({7, 8}),
          "Four-byte SHP row lost its two visible pixels");
  require(frame.rowSpans[0].hasPixels && frame.rowSpans[0].endX == 1, "Incorrect short-row span");
}

void parserRegressions() {
  shortRleRow();
  require(decode(shpBytes(2, 1, 0xcccc0003U, {4, 0, 7, 8})).indices ==
          std::vector<std::uint8_t>({7, 8}), "SHP header padding treated as compression flags");
  const auto transparent = decode(shpBytes(4, 1, 3, {4, 0, 0, 4}));
  require(!transparent.rowSpans[0].hasPixels, "Transparent SHP row gained pixels");
  const auto runs = decode(shpBytes(8, 1, 3, {11, 0, 0, 1, 5, 6, 0, 2, 9, 0, 2}));
  require(runs.indices == std::vector<std::uint8_t>({0, 5, 6, 0, 0, 9, 0, 0}), "Incorrect zero-run decoding");
  require(runs.rowSpans[0].segments.size() == 2, "Separated SHP spans were merged");
  const auto raw = decode(shpBytes(3, 1, 1, {1, 0, 2}));
  require(raw.rowSpans[0].segments.size() == 2, "Raw transparent pixel included in visible span");
  auto cropped = shpBytes(4, 2, 1, {5, 6});
  put16(cropped, 8, 1);
  put16(cropped, 10, 1);
  put16(cropped, 12, 2);
  put16(cropped, 14, 1);
  require(decode(cropped).indices == std::vector<std::uint8_t>({0, 0, 0, 0, 0, 5, 6, 0}),
          "SHP frame offset was not preserved");
  put16(cropped, 8, 3);
  requireRejected([&] { decode(cropped); }, "Out-of-canvas SHP frame accepted");
  auto badHeight = shpBytes(2, 1, 0, {1, 2});
  put16(badHeight, 10, 1);
  requireRejected([&] { decode(badHeight); }, "Out-of-canvas SHP height accepted");
  requireRejected([] { decode(shpBytes(3, 1, 0, {1, 2})); }, "Truncated raw SHP accepted");
  requireRejected([] { decode(shpBytes(2, 1, 3, {3, 0, 0})); }, "Truncated zero run accepted");
  requireRejected([] { decode(shpBytes(2, 1, 3, {5, 0, 0, 3, 7})); }, "Oversized internal zero run accepted");
  const auto padded = decode(shpBytes(3, 1, 3, {5, 0, 7, 0, 3}));
  require(padded.indices == std::vector<std::uint8_t>({7, 0, 0}), "Padded trailing transparency rejected");
  requireRejected([] { decode(shpBytes(1, 1, 3, {4, 0, 1, 2})); }, "Oversized literal row accepted");
  requireRejected([] { decode(shpBytes(1, 1, 3, {5, 0, 1})); }, "Truncated row accepted");
  auto empty = shpBytes(2, 1, 0, {});
  put32(empty, 28, 0);
  require(decode(empty).indices == std::vector<std::uint8_t>({0, 0}), "Empty SHP frame failed");

  std::vector<std::uint8_t> tmp(58, 1);
  put32(tmp, 0, 1);
  put32(tmp, 4, 1);
  put32(tmp, 8, 2);
  put32(tmp, 12, 4);
  put32(tmp, 16, 20);
  const BinaryFixture valid(tmp);
  const auto tile = TmpFile::load(valid.path()).decodeSingleTile(Palette{});
  require(tile.rgba.size() == 32 && tile.rgba[3] == 255 && tile.rgba[7] == 0,
          "TMP diamond shape changed");
  auto checkBadTmp = [](const std::vector<std::uint8_t>& bytes) {
    const BinaryFixture fixture(bytes);
    requireRejected([&] { (void)TmpFile::load(fixture.path()).decodeSingleTile(Palette{}); },
                    "Malformed TMP accepted");
  };
  put32(tmp, 16, 0xfffffff0U);
  checkBadTmp(tmp);
  put32(tmp, 16, 20);
  put32(tmp, 8, 0x40000000U);
  checkBadTmp(tmp);
  put32(tmp, 8, 0);
  checkBadTmp(tmp);
  put32(tmp, 8, 2);
  tmp.pop_back();
  checkBadTmp(tmp);
  std::cout << "Parser regression cases passed\n";
}

bool same(const TileCoord a, const TileCoord b) { return a.x == b.x && a.y == b.y; }

void pathfindingRegressions() {
  MapGrid map(64, 64);
  map.setOccupied(std::vector<TileCoord>{{0, 0}}, true);
  const auto diagonal = findTilePath(map, {0, 0}, {3, 3});
  require(diagonal && diagonal->cells.size() == 3, "Eight-way shortest path failed");
  const auto stationary = findTilePath(map, {0, 0}, {0, 0});
  require(stationary && stationary->cells.empty(), "Moving to own occupied cell failed");
  map.setOccupied(std::vector<TileCoord>{{4, 0}}, true);
  const auto occupied = findTilePath(map, {0, 0}, {4, 0});
  require(occupied && !same(occupied->target, {4, 0}), "Occupied destination accepted as walkable");
  for (const auto cell : occupied->cells) require(map.isBuildable(cell), "Path crosses an occupied cell");
  const auto negative = findTilePath(map, {0, 0}, {-3, -2});
  require(negative && same(negative->target, {-3, -2}), "Unbounded negative map coordinates rejected");
  const auto outside = findTilePath(map, {0, 0}, {66, 66});
  require(outside.has_value(), "Nominal grid dimensions incorrectly limit sandbox");

  MapGrid enclosed(64, 64);
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      if (dx != 0 || dy != 0) enclosed.setBuildable({5 + dx, 5 + dy}, false);
    }
  }
  require(!findTilePath(enclosed, {0, 0}, {5, 5}, 512), "Enclosed target unexpectedly reached");
  require(!findTilePath(map, {0, 0}, {3, 3}, 0), "Zero search budget ignored");
  require(!findTilePath(map, {0, 0}, {1000000, 1000000}, 16), "Long search budget ignored");
  const int max = std::numeric_limits<int>::max();
  const auto extreme = findTilePath(map, {max, max}, {max - 1, max - 1});
  require(extreme && extreme->cells.size() == 1, "Neighbor coordinates overflow at integer limit");
  std::cout << "Pathfinding regression cases passed\n";
}

void decodeAssets(const std::filesystem::path& root) {
  require(std::filesystem::is_directory(root), "Project assets directory is missing");
  std::size_t files = 0;
  std::size_t frames = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) continue;
    const auto extension = entry.path().extension().string();
    try {
      if (extension == ".shp" ||
          ((extension == ".tem" || extension == ".sno" || extension == ".urb") &&
           entry.path().parent_path().parent_path().filename() == "resources")) {
        const auto shp = ShpTsFile::load(entry.path());
        for (std::size_t i = 0; i < shp.frameCount(); ++i) {
          const auto frame = shp.decodeFrame(i);
          require(frame.indices.size() == static_cast<std::size_t>(frame.width) * frame.height,
                  "Incorrect decoded canvas size");
          ++frames;
        }
      } else if (extension == ".vxl") {
        (void)VxlFile::load(entry.path());
      } else if (extension == ".hva") {
        (void)HvaFile::load(entry.path());
      } else if (extension == ".vpl") {
        (void)VplFile::load(entry.path());
      } else if (extension == ".pal") {
        (void)Palette::load(entry.path());
      } else {
        continue;
      }
      ++files;
    } catch (const std::exception& error) {
      throw std::runtime_error(entry.path().string() + ": " + error.what());
    }
  }
  require(files > 0 && frames > 0, "Asset sweep decoded no files");
  std::cout << "Decoded " << files << " asset files and " << frames << " SHP frames\n";
}
}  // namespace

int main(int argc, char** argv) {
  try {
    require(argc >= 2, "Expected parsers, pathfinding, assets, or short-rle mode");
    const std::string mode = argv[1];
    if (mode == "parsers") parserRegressions();
    else if (mode == "pathfinding") pathfindingRegressions();
    else if (mode == "short-rle") shortRleRow();
    else if (mode == "assets" && argc == 3) decodeAssets(argv[2]);
    else throw std::runtime_error("Unknown test mode");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
