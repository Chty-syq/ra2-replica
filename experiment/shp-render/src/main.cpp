#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
using Bytes = std::vector<std::uint8_t>;

constexpr std::uint32_t kRaw0 = 0;
constexpr std::uint32_t kRaw1 = 1;
constexpr std::uint32_t kRle3 = 3;

struct Rgba {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 255;
};

struct FrameInfo {
  std::uint16_t x = 0;
  std::uint16_t y = 0;
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  std::uint32_t flags = 0;
  std::uint32_t dataOffset = 0;
};

struct ShpFile {
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  std::vector<FrameInfo> frames;
  Bytes bytes;
};

struct DecodedFrame {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> indices;
};

[[nodiscard]] Bytes readFile(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Cannot open file: " + path.string());
  }
  return Bytes(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void requireBytes(const Bytes& bytes, const std::size_t offset, const std::size_t count) {
  if (offset > bytes.size() || count > bytes.size() - offset) {
    throw std::runtime_error("Unexpected end of file");
  }
}

[[nodiscard]] std::uint16_t readU16(const Bytes& bytes, const std::size_t offset) {
  requireBytes(bytes, offset, 2);
  return static_cast<std::uint16_t>(bytes[offset]) |
         static_cast<std::uint16_t>(bytes[offset + 1] << 8);
}

[[nodiscard]] std::uint32_t readU32(const Bytes& bytes, const std::size_t offset) {
  requireBytes(bytes, offset, 4);
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

[[nodiscard]] ShpFile loadShp(const fs::path& path) {
  ShpFile shp;
  shp.bytes = readFile(path);
  requireBytes(shp.bytes, 0, 8);

  if (readU16(shp.bytes, 0) != 0) {
    throw std::runtime_error("This is not a TS/RA2 SHP file");
  }

  shp.width = readU16(shp.bytes, 2);
  shp.height = readU16(shp.bytes, 4);
  const auto frameCount = readU16(shp.bytes, 6);
  requireBytes(shp.bytes, 8, static_cast<std::size_t>(frameCount) * 24);

  for (std::size_t i = 0; i < frameCount; ++i) {
    const std::size_t offset = 8 + i * 24;
    FrameInfo frame{
      readU16(shp.bytes, offset + 0),
      readU16(shp.bytes, offset + 2),
      readU16(shp.bytes, offset + 4),
      readU16(shp.bytes, offset + 6),
      readU32(shp.bytes, offset + 8),
      readU32(shp.bytes, offset + 20)
    };

    if (static_cast<std::uint32_t>(frame.x) + frame.width > shp.width ||
        static_cast<std::uint32_t>(frame.y) + frame.height > shp.height) {
      throw std::runtime_error("Frame rectangle is outside the SHP canvas");
    }
    shp.frames.push_back(frame);
  }
  return shp;
}

void decodeRaw(const ShpFile& shp,
               const FrameInfo& frame,
               std::vector<std::uint8_t>& canvas) {
  std::size_t input = frame.dataOffset;

  for (std::uint16_t localY = 0; localY < frame.height; ++localY) {
    requireBytes(shp.bytes, input, frame.width);
    const std::size_t output =
      static_cast<std::size_t>(frame.y + localY) * shp.width + frame.x;

    std::copy_n(shp.bytes.begin() + static_cast<std::ptrdiff_t>(input),
                frame.width,
                canvas.begin() + static_cast<std::ptrdiff_t>(output));
    input += frame.width;
  }
}

void decodeRle(const ShpFile& shp,
               const FrameInfo& frame,
               std::vector<std::uint8_t>& canvas) {
  std::size_t input = frame.dataOffset;

  for (std::uint16_t localY = 0; localY < frame.height; ++localY) {
    const std::size_t lineStart = input;
    const auto lineLength = readU16(shp.bytes, lineStart);
    if (lineLength < 2) {
      throw std::runtime_error("Invalid RLE line length");
    }

    requireBytes(shp.bytes, lineStart, lineLength);
    input = lineStart + lineLength;

    // 4 字节的行只有长度和结束标记，表示整行透明。
    if (lineLength == 4) {
      continue;
    }

    std::size_t cursor = lineStart + 2;
    std::size_t payloadEnd = lineStart + lineLength;
    std::size_t x = 0;

    // 行开头也可能先跳过一段透明像素。
    if (cursor + 1 < payloadEnd && shp.bytes[cursor] == 0) {
      x = std::min<std::size_t>(shp.bytes[cursor + 1], frame.width);
      cursor += 2;
    }

    // 行末透明段无需写入，因为画布默认就是透明索引 0。
    if (payloadEnd >= cursor + 2 && shp.bytes[payloadEnd - 2] == 0) {
      payloadEnd -= 2;
    }

    const std::size_t rowStart =
      static_cast<std::size_t>(frame.y + localY) * shp.width + frame.x;

    while (cursor < payloadEnd && x < frame.width) {
      const auto value = shp.bytes[cursor++];

      // 0,count 表示向右跳过 count 个透明像素。
      if (value == 0 && cursor < payloadEnd && shp.bytes[cursor] > 0) {
        const std::size_t count = shp.bytes[cursor++];
        x += std::min(count, static_cast<std::size_t>(frame.width) - x);
        continue;
      }

      canvas[rowStart + x] = value;
      ++x;
    }
  }
}

[[nodiscard]] DecodedFrame decodeFrame0(const ShpFile& shp) {
  if (shp.frames.empty()) {
    throw std::runtime_error("SHP contains no frames");
  }

  const auto& frame = shp.frames.front();
  DecodedFrame decoded{
    shp.width,
    shp.height,
    std::vector<std::uint8_t>(static_cast<std::size_t>(shp.width) * shp.height, 0)
  };

  if (frame.dataOffset == 0 || frame.width == 0 || frame.height == 0) {
    return decoded;
  }

  if (frame.flags == kRaw0 || frame.flags == kRaw1) {
    decodeRaw(shp, frame, decoded.indices);
  } else if (frame.flags == kRle3) {
    decodeRle(shp, frame, decoded.indices);
  } else {
    throw std::runtime_error("Unsupported SHP flag: " + std::to_string(frame.flags));
  }
  return decoded;
}

[[nodiscard]] std::uint8_t expand6Bit(const std::uint8_t value) {
  return static_cast<std::uint8_t>((static_cast<std::uint32_t>(value) * 255u) / 63u);
}

[[nodiscard]] Rgba rgb565(const std::uint8_t r,
                          const std::uint8_t g,
                          const std::uint8_t b,
                          const std::uint8_t a) {
  const auto r5 = static_cast<std::uint8_t>(r / 8u);
  const auto g6 = static_cast<std::uint8_t>(g / 4u);
  const auto b5 = static_cast<std::uint8_t>(b / 8u);
  return Rgba{
    static_cast<std::uint8_t>((static_cast<std::uint32_t>(r5) * 255u) / 31u),
    static_cast<std::uint8_t>((static_cast<std::uint32_t>(g6) * 255u) / 63u),
    static_cast<std::uint8_t>((static_cast<std::uint32_t>(b5) * 255u) / 31u),
    a
  };
}

[[nodiscard]] std::array<Rgba, 256> loadPalette(const fs::path& path) {
  const auto bytes = readFile(path);
  if (bytes.size() != 256 * 3) {
    throw std::runtime_error("PAL must contain exactly 768 bytes");
  }

  std::array<Rgba, 256> palette{};
  for (std::size_t i = 0; i < palette.size(); ++i) {
    palette[i] = rgb565(expand6Bit(bytes[i * 3 + 0]),
                        expand6Bit(bytes[i * 3 + 1]),
                        expand6Bit(bytes[i * 3 + 2]),
                        static_cast<std::uint8_t>(i == 0 ? 0 : 255));
  }
  return palette;
}

[[nodiscard]] std::vector<std::uint8_t> makeRgbaPixels(
  const DecodedFrame& frame,
  const std::array<Rgba, 256>& palette) {
  std::vector<std::uint8_t> pixels(frame.indices.size() * 4);

  for (std::size_t i = 0; i < frame.indices.size(); ++i) {
    const auto color = palette[frame.indices[i]];
    pixels[i * 4 + 0] = color.r;
    pixels[i * 4 + 1] = color.g;
    pixels[i * 4 + 2] = color.b;
    pixels[i * 4 + 3] = color.a;
  }
  return pixels;
}

[[nodiscard]] fs::path projectRoot() {
#ifdef SHP_RENDER_EXPERIMENT_ROOT
  return fs::path(SHP_RENDER_EXPERIMENT_ROOT);
#else
  return fs::current_path();
#endif
}
}  // namespace

int main(int, char**) {
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  SDL_Texture* texture = nullptr;

  try {
    const auto root = projectRoot();
    const auto shp = loadShp(
      root / "assets" / "buildings" / "allied" / "gapowr" / "gapowr.shp");
    const auto palette = loadPalette(
      root / "assets" / "palettes" / "theater" / "unitsno.pal");
    const auto frame = decodeFrame0(shp);
    const auto rgba = makeRgbaPixels(frame, palette);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
      throw std::runtime_error(SDL_GetError());
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    window = SDL_CreateWindow("GAPOWR.SHP - frame 0",
                              SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED,
                              frame.width * 5,
                              frame.height * 5,
                              SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
      throw std::runtime_error(SDL_GetError());
    }

    renderer = SDL_CreateRenderer(window,
                                  -1,
                                  SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == nullptr) {
      throw std::runtime_error(SDL_GetError());
    }

    texture = SDL_CreateTexture(renderer,
                                SDL_PIXELFORMAT_RGBA32,
                                SDL_TEXTUREACCESS_STATIC,
                                frame.width,
                                frame.height);
    if (texture == nullptr) {
      throw std::runtime_error(SDL_GetError());
    }

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(texture, nullptr, rgba.data(), frame.width * 4);

    bool running = true;
    while (running) {
      SDL_Event event{};
      while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_QUIT ||
            (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
          running = false;
        }
      }

      int outputWidth = 0;
      int outputHeight = 0;
      SDL_GetRendererOutputSize(renderer, &outputWidth, &outputHeight);
      const float scale = std::min(static_cast<float>(outputWidth) / frame.width,
                                   static_cast<float>(outputHeight) / frame.height);
      const SDL_Rect destination{
        static_cast<int>((outputWidth - frame.width * scale) * 0.5f),
        static_cast<int>((outputHeight - frame.height * scale) * 0.5f),
        static_cast<int>(frame.width * scale),
        static_cast<int>(frame.height * scale)
      };

      SDL_SetRenderDrawColor(renderer, 41, 46, 56, 255);
      SDL_RenderClear(renderer);
      SDL_RenderCopy(renderer, texture, nullptr, &destination);
      SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    if (texture != nullptr) SDL_DestroyTexture(texture);
    if (renderer != nullptr) SDL_DestroyRenderer(renderer);
    if (window != nullptr) SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
}
