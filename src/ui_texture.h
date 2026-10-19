#pragma once

#include <SDL_opengl.h>

#include <cstdint>
#include <vector>

enum class UiTextureColorMode {
  Rgba,
  IndexedPalette
};

enum class UiTexturePaletteKind {
  None,
  Unit,
  Terrain,
  Resource,
  Animation
};

struct UiTexture {
  GLuint texture = 0;
  GLuint logicalCoordTexture = 0;
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba;
  UiTextureColorMode colorMode = UiTextureColorMode::Rgba;
  UiTexturePaletteKind paletteKind = UiTexturePaletteKind::None;
  bool paletteRemap = false;
  // Optional CPU hit mask, one bit per pixel. Indexed building textures keep
  // this instead of an RGBA copy so picking can follow their visible silhouette.
  std::vector<std::uint8_t> hitMask{};
};
