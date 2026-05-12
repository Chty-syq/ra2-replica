#include "building_system.h"
#include "iso_world.h"

#include <SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace {
constexpr std::array<std::pair<std::string_view, std::string_view>, 16> kBuildableIconToBuilding{{
  {"powricon", "GAPOWR"},
  {"reficon", "GAREFN"},
  {"brrkicon", "GAPILE"},
  {"gwepicon", "GAWEAP"},
  {"heliicon", "GAAIRC"},
  {"ayaricon", "GAYARD"},
  {"fixicon", "GADEPT"},
  {"techicon", "GATECH"},
  {"gorep", "GAOREP"},
  {"wallicon", "GAWALL"},
  {"pillicon", "GAPILL"},
  {"samicon", "NASAM"},
  {"prisicon", "GAPRIS"},
  {"gapicon", "GAGAP"},
  {"csphicon", "GACSPH"},
  {"wethicon", "GAWETH"}
}};

[[nodiscard]] GLuint uploadIndexedTexture(const int width,
                                          const int height,
                                          const std::vector<std::uint8_t>& indices) {
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, indices.data());
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  return texture;
}

[[nodiscard]] GLuint uploadLogicalCoordTexture(const int width,
                                               const int height,
                                               const std::vector<float>& logicalCoords) {
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, width, height, 0, GL_RG, GL_FLOAT, logicalCoords.data());
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  return texture;
}

template <typename T>
[[nodiscard]] std::vector<T> firstHalfOrSingle(std::vector<T> values) {
  if (values.size() <= 1) {
    return values;
  }

  values.resize(values.size() / 2);
  return values;
}

using FileIndex = std::unordered_map<std::string, std::filesystem::path>;
using DecodedFrameCache = std::vector<DecodedFrame>;

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

[[nodiscard]] const DecodedFrameCache& decodedFramesForPath(const std::filesystem::path& path) {
  static std::unordered_map<std::string, DecodedFrameCache> cache;

  const auto key = path.lexically_normal().string();
  if (const auto it = cache.find(key); it != cache.end()) {
    return it->second;
  }

  // SHP 解析和逐帧解码是切换风格时最重的 CPU 工作之一。
  // 这里把“文件 -> 已解码帧”缓存下来，避免每次切换都重新读盘和重解。
  const auto shp = ShpTsFile::load(path);
  DecodedFrameCache frames;
  frames.reserve(shp.frameCount());
  for (std::size_t i = 0; i < shp.frameCount(); ++i) {
    frames.push_back(shp.decodeFrame(i));
  }

  return cache.emplace(key, std::move(frames)).first->second;
}

[[nodiscard]] std::optional<std::filesystem::path> findIndexedFile(const std::filesystem::path& root,
                                                                   const std::string& fileName) {
  const auto& index = fileIndexForRoot(root);
  if (const auto it = index.find(toLowerAscii(fileName)); it != index.end()) {
    return it->second;
  }
  return std::nullopt;
}

[[nodiscard]] bool shpExists(const std::filesystem::path& root, const std::string& shpName) {
  return findIndexedFile(root, shpName + ".shp").has_value();
}

[[nodiscard]] std::filesystem::path requireShpPath(const std::filesystem::path& root, const std::string& shpName) {
  const auto resolved = findIndexedFile(root, shpName + ".shp");
  if (!resolved.has_value()) {
    throw std::runtime_error("Missing SHP resource: " + shpName);
  }
  return *resolved;
}

[[nodiscard]] std::vector<UiTexture> loadAllShpTextures(const std::filesystem::path& path,
                                                        const UiTexturePaletteKind paletteKind,
                                                        const bool paletteRemap) {
  const auto& frames = decodedFramesForPath(path);
  std::vector<UiTexture> textures;
  textures.reserve(frames.size());
  for (const auto& frame : frames) {
    textures.push_back(UiTexture{
      uploadIndexedTexture(frame.width, frame.height, frame.indices),
      0,
      frame.width,
      frame.height,
      {},
      UiTextureColorMode::IndexedPalette,
      paletteKind,
      paletteRemap
    });
  }
  return textures;
}

[[nodiscard]] std::vector<float> buildLogicalCoordPixels(const DecodedFrame& frame,
                                                         const Vec2 anchorInFrame) {
  // 对每个非透明像素，记录它相对于“建筑地图锚点”的屏幕偏移。
  // 后面 fragment shader 会把这个屏幕偏移再反解回逻辑坐标，
  // 从而按像素决定深度，而不是整栋建筑共用一个中心深度。
  std::vector<float> values(static_cast<std::size_t>(frame.width * frame.height * 2), 0.0f);
  for (int y = 0; y < frame.height; ++y) {
    for (int x = 0; x < frame.width; ++x) {
      const std::size_t pixelIndex = static_cast<std::size_t>(y * frame.width + x);
      if (frame.indices[pixelIndex] == 0) {
        continue;
      }

      const std::size_t dstIndex = pixelIndex * 2;
      values[dstIndex + 0] = (static_cast<float>(x) + 0.5f) - anchorInFrame.x;
      values[dstIndex + 1] = (static_cast<float>(y) + 0.5f) - anchorInFrame.y;
    }
  }
  return values;
}

void attachLogicalCoordTexture(UiTexture& texture,
                               const DecodedFrame& frame,
                               const Vec2 anchorInFrame) {
  texture.logicalCoordTexture =
    uploadLogicalCoordTexture(frame.width, frame.height, buildLogicalCoordPixels(frame, anchorInFrame));
}

[[nodiscard]] std::vector<DecodedFrame> loadAllDecodedFrames(const std::filesystem::path& path) {
  return decodedFramesForPath(path);
}

struct LayerSpec {
  std::string spriteName;
  std::optional<std::string> animationId;
};

[[nodiscard]] std::string resolveTheaterVariant(const std::filesystem::path& spriteRoot,
                                                std::string name,
                                                const TheaterStyle theater,
                                                const bool newTheater,
                                                const bool buildup) {
  name = toLowerAscii(std::move(name));
  if (!newTheater || name.size() < 2) {
    return name;
  }

  const char owner = name[0];
  const char type = name[1];
  if ((owner != 'g' && owner != 'n') ||
      (type != 'a' && type != 'g' && type != 'u' && type != 't')) {
    return name;
  }

  const auto stem = name.substr(2);
  std::vector<std::string> candidates;
  auto pushCandidate = [&](const char variant) {
    std::string candidate;
    candidate.reserve(name.size());
    candidate.push_back(owner);
    candidate.push_back(variant);
    candidate += stem;
    if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end()) {
      candidates.push_back(std::move(candidate));
    }
  };

  if (buildup) {
    switch (theater) {
      case TheaterStyle::Temperate:
        pushCandidate('t');
        pushCandidate('a');
        pushCandidate('u');
        pushCandidate('g');
        break;
      case TheaterStyle::Snow:
        pushCandidate('a');
        pushCandidate('t');
        pushCandidate('g');
        pushCandidate('u');
        break;
      case TheaterStyle::Urban:
        pushCandidate('u');
        pushCandidate('t');
        pushCandidate('g');
        pushCandidate('a');
        break;
    }
  } else {
    switch (theater) {
      case TheaterStyle::Temperate:
        pushCandidate('g');
        pushCandidate('a');
        pushCandidate('u');
        pushCandidate('t');
        break;
      case TheaterStyle::Snow:
        pushCandidate('a');
        pushCandidate('g');
        pushCandidate('u');
        pushCandidate('t');
        break;
      case TheaterStyle::Urban:
        pushCandidate('u');
        pushCandidate('g');
        pushCandidate('a');
        pushCandidate('t');
        break;
    }
  }

  for (const auto& candidate : candidates) {
    if (shpExists(spriteRoot, candidate)) {
      return candidate;
    }
  }

  return name;
}

[[nodiscard]] std::vector<LayerSpec> completeLayerSpecsForBuilding(const std::string& buildingId,
                                                                   const ArtDefinition& art,
                                                                   const std::string& spriteName) {
  const auto baseName = toLowerAscii(spriteName);
  const auto bibName = art.bibShape.has_value() ? toLowerAscii(*art.bibShape) : std::string{};

  std::vector<LayerSpec> specs;
  auto addStatic = [&](std::string layerName) {
    layerName = toLowerAscii(std::move(layerName));
    if (!layerName.empty()) {
      specs.push_back(LayerSpec{std::move(layerName), std::nullopt});
    }
  };
  auto addAnimated = [&](const std::optional<std::string>& animationId) {
    if (animationId.has_value() && !animationId->empty()) {
      specs.push_back(LayerSpec{"", *animationId});
    }
  };

  if (buildingId == "GAPOWR") {
    addStatic(baseName);
    addAnimated(art.activeAnim);
    return specs;
  }
  if (buildingId == "GAPILE") {
    addStatic(baseName);
    addAnimated(art.activeAnim);
    return specs;
  }
  if (buildingId == "GATECH") {
    addStatic(baseName);
    addAnimated(art.activeAnim);
    return specs;
  }
  if (buildingId == "GAOREP") {
    addStatic(baseName);
    addAnimated(art.activeAnim);
    return specs;
  }
  if (buildingId == "GAGAP") {
    addStatic(baseName);
    addAnimated(art.activeAnim);
    return specs;
  }
  if (buildingId == "GAPRIS") {
    addStatic(baseName);
    addAnimated(art.activeAnim);
    return specs;
  }
  if (buildingId == "GAREFN") {
    addStatic(baseName);
    addStatic(bibName);
    addAnimated(art.activeAnimThree);
    addAnimated(art.activeAnimTwo);
    addAnimated(art.activeAnim);
    return specs;
  }
  if (buildingId == "GAWEAP") {
    addStatic(baseName);
    addStatic(baseName + "_2");
    addStatic(baseName + "_1");
    addAnimated(art.activeAnimTwo);
    addAnimated(art.activeAnim);
    addStatic(bibName);
    return specs;
  }
  if (buildingId == "GAAIRC") {
    addStatic(baseName);
    addAnimated(art.activeAnimThree);
    addStatic(bibName);
    addAnimated(art.activeAnimTwo);
    addAnimated(art.activeAnim);
    return specs;
  }
  if (buildingId == "GAYARD") {
    addStatic(baseName);
    addAnimated(art.activeAnimThree);
    addAnimated(art.productionAnim);
    return specs;
  }
  if (buildingId == "GADEPT") {
    addStatic(baseName);
    addStatic(bibName);
    addAnimated(art.idleAnim);
    return specs;
  }
  if (buildingId == "GACSPH") {
    addStatic(baseName);
    addAnimated(art.superAnim);
    return specs;
  }
  if (buildingId == "GAWETH") {
    addStatic(baseName);
    addAnimated(art.superAnim);
    return specs;
  }

  addStatic(baseName);
  addStatic(bibName);
  addAnimated(art.idleAnim);
  addAnimated(art.activeAnim);
  addAnimated(art.activeAnimTwo);
  addAnimated(art.activeAnimThree);
  addAnimated(art.activeAnimFour);
  addAnimated(art.specialAnim);
  addAnimated(art.specialAnimTwo);
  addAnimated(art.specialAnimThree);
  addAnimated(art.specialAnimFour);
  addAnimated(art.superAnim);
  addAnimated(art.superAnimTwo);
  addAnimated(art.superAnimThree);
  addAnimated(art.superAnimFour);
  addAnimated(art.productionAnim);
  return specs;
}

[[nodiscard]] std::string resolveAnimatedLayerName(const std::filesystem::path& spriteRoot,
                                                   const ArtIni& artIni,
                                                   const std::string& animationId,
                                                   const TheaterStyle theater,
                                                   const bool newTheater) {
  const auto animation = artIni.animation(animationId);
  const auto spriteName = animation.image.empty() ? animation.id : animation.image;
  return resolveTheaterVariant(spriteRoot,
                               spriteName,
                               theater,
                               animation.newTheater || newTheater,
                               false);
}

[[nodiscard]] std::vector<std::size_t> animationFrameRange(const std::size_t frameCount,
                                                           const AnimationDefinition& animation) {
  if (frameCount == 0) {
    return {};
  }

  std::size_t start = 0;
  if (animation.loopStart.has_value()) {
    start = static_cast<std::size_t>(std::max(0, *animation.loopStart));
  } else if (animation.start.has_value()) {
    start = static_cast<std::size_t>(std::max(0, *animation.start));
  }

  std::size_t end = frameCount - 1;
  if (animation.loopEnd.has_value()) {
    end = static_cast<std::size_t>(std::max(0, *animation.loopEnd));
  } else if (animation.end.has_value()) {
    end = static_cast<std::size_t>(std::max(0, *animation.end));
  }

  start = std::min(start, frameCount - 1);
  end = std::min(end, frameCount - 1);
  if (end < start) {
    end = start;
  }

  std::vector<std::size_t> indices;
  indices.reserve(end - start + 1);
  for (std::size_t frameIndex = start; frameIndex <= end; ++frameIndex) {
    indices.push_back(frameIndex);
  }
  return indices;
}

[[nodiscard]] BuildingAsset::RenderLayer loadStaticLayer(const std::filesystem::path& spriteRoot,
                                                         const std::string& spriteName,
                                                         const UiTexturePaletteKind paletteKind,
                                                         const bool paletteRemap,
                                                         const TheaterStyle theater,
                                                         const bool newTheater) {
  const auto resolvedName = resolveTheaterVariant(spriteRoot, spriteName, theater, newTheater, false);
  const auto& frames = decodedFramesForPath(requireShpPath(spriteRoot, resolvedName));
  const auto& frame = frames.front();
  return BuildingAsset::RenderLayer{
    resolvedName,
    {UiTexture{
      uploadIndexedTexture(frame.width, frame.height, frame.indices),
      0,
      frame.width,
      frame.height,
      {},
      UiTextureColorMode::IndexedPalette,
      paletteKind,
      paletteRemap
    }},
    0
  };
}

[[nodiscard]] BuildingAsset::RenderLayer loadAnimatedLayer(const std::filesystem::path& spriteRoot,
                                                           const ArtIni& artIni,
                                                           const UiTexturePaletteKind paletteKind,
                                                           const bool paletteRemap,
                                                           const std::string& buildingId,
                                                           const std::string& animationId,
                                                           const TheaterStyle theater,
                                                           const bool newTheater) {
  const auto animation = artIni.animation(animationId);
  const auto normalizedAnimationId = toLowerAscii(animationId);
  const auto spriteName = resolveAnimatedLayerName(spriteRoot, artIni, animationId, theater, newTheater);
  const auto path = findIndexedFile(spriteRoot, spriteName + ".shp");
  if (!path.has_value()) {
    return {};
  }

  const auto& frames = decodedFramesForPath(*path);
  auto frameIndices = animationFrameRange(frames.size(), animation);
  if (frameIndices.empty()) {
    frameIndices.push_back(0);
  }

  if (buildingId == "GAPOWR" && frameIndices.size() > 8) {
    frameIndices.resize(8);
  }
  if (normalizedAnimationId == "gaairc_b" && frameIndices.size() > 6) {
    frameIndices.resize(6);
  }
  if (normalizedAnimationId == "gaairc_a" && frameIndices.size() > 4) {
    frameIndices.resize(4);
  }
  if (normalizedAnimationId == "gadept_d" && frameIndices.size() > 1) {
    frameIndices.resize(1);
  }
  if (normalizedAnimationId == "gaorep_a" && frameIndices.size() > 8) {
    frameIndices.resize(8);
  }
  if (normalizedAnimationId == "gaweap_a" && frameIndices.size() > 15) {
    frameIndices.resize(15);
  }

  std::vector<UiTexture> textures;
  textures.reserve(frameIndices.size());
  for (const auto frameIndex : frameIndices) {
    const auto& frame = frames[frameIndex];
    textures.push_back(UiTexture{
      uploadIndexedTexture(frame.width, frame.height, frame.indices),
      0,
      frame.width,
      frame.height,
      {},
      UiTextureColorMode::IndexedPalette,
      paletteKind,
      paletteRemap
    });
  }

  const auto rate = animation.rate.value_or(220);
  return BuildingAsset::RenderLayer{
    spriteName,
    std::move(textures),
    rate > 0 ? static_cast<std::uint32_t>(rate) : 0
  };
}

[[nodiscard]] std::vector<BuildingAsset::RenderLayer> loadCompleteLayers(const std::filesystem::path& spriteRoot,
                                                                         const ArtIni& artIni,
                                                                         const UiTexturePaletteKind paletteKind,
                                                                         const bool paletteRemap,
                                                                         const std::string& buildingId,
                                                                         const ArtDefinition& art,
                                                                         const std::string& spriteName,
                                                                         const TheaterStyle theater) {
  std::vector<BuildingAsset::RenderLayer> layers;
  for (const auto& spec : completeLayerSpecsForBuilding(buildingId, art, spriteName)) {
    if (spec.animationId.has_value()) {
      auto layer = loadAnimatedLayer(spriteRoot,
                                     artIni,
                                     paletteKind,
                                     paletteRemap,
                                     buildingId,
                                     *spec.animationId,
                                     theater,
                                     art.newTheater);
      if (!layer.textures.empty()) {
        layers.push_back(std::move(layer));
      }
      continue;
    }

    if (spec.spriteName.empty()) {
      continue;
    }

    const auto resolvedName = resolveTheaterVariant(spriteRoot, spec.spriteName, theater, art.newTheater, false);
    if (!shpExists(spriteRoot, resolvedName)) {
      continue;
    }

    layers.push_back(loadStaticLayer(spriteRoot,
                                     spec.spriteName,
                                     paletteKind,
                                     paletteRemap,
                                     theater,
                                     art.newTheater));
  }

  return layers;
}

[[nodiscard]] DecodedFrame loadFirstDecodedFrame(const std::filesystem::path& spriteRoot,
                                                 const std::string& spriteName) {
  const auto& frames = decodedFramesForPath(requireShpPath(spriteRoot, spriteName));
  return frames.front();
}

[[nodiscard]] BuildingAsset loadBuildingAsset(const std::filesystem::path& spriteRoot,
                                              const ArtIni& artIni,
                                              const Palette& unitPalette,
                                              const Palette& terrainPalette,
                                              const TheaterStyle theater,
                                              const std::string& buildingId) {
  const auto art = artIni.building(buildingId);
  (void)unitPalette;
  (void)terrainPalette;
  const auto paletteKind = art.terrainPalette ? UiTexturePaletteKind::Terrain
                                              : UiTexturePaletteKind::Unit;
  const bool paletteRemap = !art.terrainPalette;

  const auto spriteName = art.image.empty() ? buildingId : art.image;
  const auto fallbackBuildupName =
    art.buildup.has_value() && !art.buildup->empty() ? *art.buildup : spriteName;
  const auto buildupName = resolveTheaterVariant(spriteRoot,
                                                 fallbackBuildupName,
                                                 theater,
                                                 art.newTheater,
                                                 true);

  auto completeLayers = loadCompleteLayers(spriteRoot,
                                           artIni,
                                           paletteKind,
                                           paletteRemap,
                                           buildingId,
                                           art,
                                           spriteName,
                                           theater);
  if (completeLayers.empty()) {
    throw std::runtime_error("No complete layers found for building: " + buildingId);
  }

  auto baseLayerName = resolveTheaterVariant(spriteRoot, spriteName, theater, art.newTheater, false);
  if (!shpExists(spriteRoot, baseLayerName)) {
    baseLayerName = completeLayers.front().id;
  }

  const auto completeFrame = loadFirstDecodedFrame(spriteRoot, baseLayerName);
  const auto completeAnchor = spriteAnchorInFrame(completeFrame, art.foundation.value_or(FoundationSize{2, 2}));

  for (auto& layer : completeLayers) {
    for (std::size_t textureIndex = 0; textureIndex < layer.textures.size(); ++textureIndex) {
      const auto resolvedPath = findIndexedFile(spriteRoot, layer.id + ".shp");
      if (!resolvedPath.has_value()) {
        continue;
      }
      const auto& frames = decodedFramesForPath(*resolvedPath);
      if (frames.empty()) {
        continue;
      }

      std::size_t frameIndex = 0;
      if (layer.textures.size() > 1 && textureIndex < frames.size()) {
        frameIndex = textureIndex;
      }
      frameIndex = std::min(frameIndex, frames.size() - 1);
      attachLogicalCoordTexture(layer.textures[textureIndex], frames[frameIndex], completeAnchor);
    }
  }

  std::vector<UiTexture> buildupTextures;
  std::vector<DecodedFrame> buildupFrames;
  if (const auto buildupPath = findIndexedFile(spriteRoot, buildupName + ".shp"); buildupPath.has_value()) {
    buildupTextures = firstHalfOrSingle(loadAllShpTextures(*buildupPath, paletteKind, paletteRemap));
    buildupFrames = firstHalfOrSingle(loadAllDecodedFrames(*buildupPath));
    for (std::size_t frameIndex = 0; frameIndex < buildupTextures.size() && frameIndex < buildupFrames.size(); ++frameIndex) {
      attachLogicalCoordTexture(buildupTextures[frameIndex],
                                buildupFrames[frameIndex],
                                spriteAnchorInFrame(buildupFrames[frameIndex], art.foundation.value_or(FoundationSize{2, 2})));
    }
  }

  const auto completeTexture = completeLayers.front().textures.front();

  return BuildingAsset{
    buildingId,
    art,
    art.foundation.value_or(FoundationSize{2, 2}),
    completeTexture,
    completeFrame,
    std::move(completeLayers),
    std::move(buildupTextures),
    std::move(buildupFrames)
  };
}
}  // namespace

BuildingAssetMap loadBuildingAssets(const std::filesystem::path& spriteRoot,
                                    const ArtIni& artIni,
                                    const Palette& unitPalette,
                                    const Palette& terrainPalette,
                                    const TheaterStyle theater) {
  BuildingAssetMap assets;
  for (const auto& [_, buildingId] : kBuildableIconToBuilding) {
    const std::string key(buildingId);
    if (assets.find(key) != assets.end()) {
      continue;
    }

    assets.emplace(key,
                   loadBuildingAsset(spriteRoot,
                                     artIni,
                                     unitPalette,
                                     terrainPalette,
                                     theater,
                                     key));
  }
  return assets;
}

BuildingAsset loadBuildingAssetById(const std::filesystem::path& spriteRoot,
                                    const ArtIni& artIni,
                                    const Palette& unitPalette,
                                    const Palette& terrainPalette,
                                    const TheaterStyle theater,
                                    const std::string& buildingId) {
  return loadBuildingAsset(spriteRoot,
                           artIni,
                           unitPalette,
                           terrainPalette,
                           theater,
                           buildingId);
}

std::optional<std::string> buildingIdForIcon(const std::string_view iconId) {
  for (const auto& [icon, building] : kBuildableIconToBuilding) {
    if (iconId == icon) {
      return std::string(building);
    }
  }
  return std::nullopt;
}

const BuildingAsset& assetForInstance(const BuildingAssetMap& assets,
                                      const BuildingInstance& instance) {
  return assets.at(instance.assetId);
}

void updateConstructionStates(std::vector<BuildingInstance>& buildings,
                              const BuildingAssetMap& assets,
                              const std::uint32_t nowTicks) {
  constexpr std::uint32_t kBuildupFrameDurationMs = 50;

  for (auto& building : buildings) {
    if (building.state != BuildingState::Constructing) {
      continue;
    }

    const auto& asset = assetForInstance(assets, building);
    if (asset.buildupTextures.empty() ||
        nowTicks - building.stateStartTicks >= asset.buildupTextures.size() * kBuildupFrameDurationMs) {
      building.state = BuildingState::Complete;
      building.stateStartTicks = nowTicks;
    }
  }
}

void destroyBuildingAssets(BuildingAssetMap& assets) {
  for (auto& [_, asset] : assets) {
    for (auto& layer : asset.completeLayers) {
      for (auto& texture : layer.textures) {
        if (texture.texture != 0) {
          glDeleteTextures(1, &texture.texture);
        }
        if (texture.logicalCoordTexture != 0) {
          glDeleteTextures(1, &texture.logicalCoordTexture);
        }
      }
    }

    for (auto& texture : asset.buildupTextures) {
      if (texture.texture != 0) {
        glDeleteTextures(1, &texture.texture);
      }
      if (texture.logicalCoordTexture != 0) {
        glDeleteTextures(1, &texture.logicalCoordTexture);
      }
    }
  }
}
