#include "vpl_box_renderer.h"
#include "gl_loader.h"

#include "voxel_normals.h"
#include "vxl_file.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
struct Matrix4f {
  std::array<float, 16> values{};

  [[nodiscard]] float& at(const std::size_t row, const std::size_t column) {
    return values[row * 4 + column];
  }

  [[nodiscard]] float at(const std::size_t row, const std::size_t column) const {
    return values[row * 4 + column];
  }

  [[nodiscard]] static Matrix4f identity() {
    Matrix4f matrix;
    matrix.at(0, 0) = 1.0f;
    matrix.at(1, 1) = 1.0f;
    matrix.at(2, 2) = 1.0f;
    matrix.at(3, 3) = 1.0f;
    return matrix;
  }
};

struct VoxelInstance {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  std::uint8_t color = 0;
  std::uint8_t normal = 0;
  std::uint8_t pad0 = 0;
  std::uint8_t pad1 = 0;
};

struct SectionGpuData {
  std::string name;
  VxlSectionTailer tailer{};
  GLuint instanceBuffer = 0;
  GLsizei instanceCount = 0;
};

struct RhinoPartData {
  std::string name;
  HvaFile hva;
  std::vector<SectionGpuData> sections;
};

struct GlHandles {
  GLuint program = 0;
  GLuint shadowProgram = 0;
  GLuint vao = 0;
  GLuint cubeBuffer = 0;
  GLuint paletteTexture = 0;
  GLuint vplTexture = 0;
  GLuint offscreenFbo = 0;
  GLuint offscreenColorTexture = 0;
  GLuint offscreenDepthRenderbuffer = 0;

  GLint transformRowsLocation = -1;
  GLint lightDirectionLocation = -1;
  GLint remapColorLocation = -1;
  GLint modelOffsetLocation = -1;
  GLint scaleFactorLocation = -1;
  GLint canvasInfoLocation = -1;
  GLint projectionCenterLocation = -1;
  GLint depthParamsLocation = -1;
  GLint normalTableLocation = -1;
  GLint paletteTextureLocation = -1;
  GLint vplTextureLocation = -1;

  GLint shadowTransformRowsLocation = -1;
  GLint shadowLightDirectionLocation = -1;
  GLint shadowModelOffsetLocation = -1;
  GLint shadowScaleFactorLocation = -1;
  GLint shadowCanvasInfoLocation = -1;
  GLint shadowProjectionCenterLocation = -1;
  GLint shadowDepthParamsLocation = -1;
  GLint shadowColorLocation = -1;

  int offscreenWidth = 0;
  int offscreenHeight = 0;
};

struct Vec3f {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

constexpr float kPi = 3.1415926536f;
constexpr float kPixelsToLeptons = 30.0f * 1.41421356237f / 256.0f;

constexpr std::array<float, 36 * 3> kCubeVertices = {
  -0.5f, -0.5f,  0.5f,  -0.5f,  0.5f,  0.5f,   0.5f, -0.5f,  0.5f,
  -0.5f,  0.5f,  0.5f,   0.5f, -0.5f,  0.5f,   0.5f,  0.5f,  0.5f,

  -0.5f, -0.5f, -0.5f,  -0.5f,  0.5f, -0.5f,   0.5f, -0.5f, -0.5f,
  -0.5f,  0.5f, -0.5f,   0.5f, -0.5f, -0.5f,   0.5f,  0.5f, -0.5f,

  -0.5f, -0.5f, -0.5f,   0.5f, -0.5f, -0.5f,  -0.5f, -0.5f,  0.5f,
   0.5f, -0.5f, -0.5f,  -0.5f, -0.5f,  0.5f,   0.5f, -0.5f,  0.5f,

  -0.5f,  0.5f, -0.5f,   0.5f,  0.5f, -0.5f,  -0.5f,  0.5f,  0.5f,
   0.5f,  0.5f, -0.5f,  -0.5f,  0.5f,  0.5f,   0.5f,  0.5f,  0.5f,

   0.5f, -0.5f, -0.5f,   0.5f,  0.5f, -0.5f,   0.5f, -0.5f,  0.5f,
   0.5f,  0.5f, -0.5f,   0.5f, -0.5f,  0.5f,   0.5f,  0.5f,  0.5f,

  -0.5f, -0.5f, -0.5f,  -0.5f,  0.5f, -0.5f,  -0.5f, -0.5f,  0.5f,
  -0.5f,  0.5f, -0.5f,  -0.5f, -0.5f,  0.5f,  -0.5f,  0.5f,  0.5f
};

constexpr const char* kVertexShaderSource = R"GLSL(
#version 330 core

layout(location = 0) in vec3 aCorner;
layout(location = 1) in vec3 aVoxelPosition;
layout(location = 2) in vec2 aVoxelMeta;

uniform vec4 uTransformRows[4];
uniform vec3 uLightDir;
uniform vec3 uRemapColor;
uniform vec3 uModelOffset;
uniform float uScaleFactor;
uniform vec3 uCanvasInfo;
uniform vec2 uProjectionCenter;
uniform vec2 uDepthParams;
uniform vec4 uNormalTable[256];
uniform sampler2D uPaletteTexture;
uniform usampler2D uVplTexture;

out vec4 vColor;

const float kPi = 3.1415926536;
const float kEpsilon = 1e-10;

float clampZero(float value) {
  return value > 0.0 ? value : 0.0;
}

vec3 RGBtoHCV(vec3 rgb) {
  vec4 p = (rgb.g < rgb.b) ? vec4(rgb.bg, -1.0, 2.0 / 3.0) : vec4(rgb.gb, 0.0, -1.0 / 3.0);
  vec4 q = (rgb.r < p.x) ? vec4(p.xyw, rgb.r) : vec4(rgb.r, p.yzx);
  float c = q.x - min(q.w, q.y);
  float h = abs((q.w - q.y) / (6.0 * c + kEpsilon) + q.z);
  return vec3(h, c, q.x);
}

vec3 RGBtoHSV(vec3 rgb) {
  vec3 hcv = RGBtoHCV(rgb);
  return vec3(hcv.x, hcv.y / (hcv.z + kEpsilon), hcv.z);
}

vec3 HUEtoRGB(float hue) {
  float r = abs(hue * 6.0 - 3.0) - 1.0;
  float g = 2.0 - abs(hue * 6.0 - 2.0);
  float b = 2.0 - abs(hue * 6.0 - 4.0);
  return clamp(vec3(r, g, b), 0.0, 1.0);
}

vec3 HSVtoRGB(vec3 hsv) {
  vec3 rgb = HUEtoRGB(hsv.x);
  return ((rgb - 1.0) * hsv.y + 1.0) * hsv.z;
}

vec3 vxl_projection(vec3 modelPos) {
  const float f = 5000.0;
  modelPos.xy *= vec2(1.0, -1.0);

  vec3 result = vec3(0.0);
  result.x = uProjectionCenter.x + (modelPos.x - modelPos.y) / sqrt(2.0);
  result.y = uProjectionCenter.y + (modelPos.x + modelPos.y) / sqrt(8.0) - modelPos.z * sqrt(3.0) / 2.0;
  result.z = sqrt(3.0) / 2.0 / f *
             (4000.0 * sqrt(2.0) / 3.0 - (modelPos.x + modelPos.y) / sqrt(2.0) - modelPos.z / sqrt(3.0));
  return result;
}

vec4 mul_row_vec_mat4(vec4 value) {
  return vec4(
    dot(value, vec4(uTransformRows[0].x, uTransformRows[1].x, uTransformRows[2].x, uTransformRows[3].x)),
    dot(value, vec4(uTransformRows[0].y, uTransformRows[1].y, uTransformRows[2].y, uTransformRows[3].y)),
    dot(value, vec4(uTransformRows[0].z, uTransformRows[1].z, uTransformRows[2].z, uTransformRows[3].z)),
    dot(value, vec4(uTransformRows[0].w, uTransformRows[1].w, uTransformRows[2].w, uTransformRows[3].w))
  );
}

void main() {
  uint colorIndex = uint(aVoxelMeta.x + 0.5);
  uint normalIndex = uint(aVoxelMeta.y + 0.5);

  vec4 modelPos = vec4(aVoxelPosition + aCorner, 1.0);
  vec4 voxelPos = mul_row_vec_mat4(modelPos);
  voxelPos.xyz *= vec3(uScaleFactor);
  voxelPos.xyz += uModelOffset;

  vec3 rawNormal = mul_row_vec_mat4(vec4(uNormalTable[int(normalIndex)].xyz, 0.0)).xyz;
  vec3 normal = length(rawNormal) > 0.0001 ? normalize(rawNormal) : vec3(0.0, 0.0, 1.0);

  vec3 up = vec3(0.0, 0.0, 1.0);
  vec3 lightDir = normalize(uLightDir);
  vec3 lightDir2 = length(lightDir + up) == 0.0 ? vec3(0.0) : normalize(lightDir + up);

  float f1 = dot(normal, lightDir);
  float dot2 = dot(normal, lightDir2);
  float f2 = dot2 / (3.0 - (3.0 - 1.0) * dot2);
  uint vplSection = uint(clamp(16.0 * (clampZero(f1) + clampZero(f2)), 0.0, 31.0));
  uint realColorIndex = texelFetch(uVplTexture, ivec2(int(colorIndex), int(vplSection)), 0).r;

  vec4 paletteColor = texelFetch(uPaletteTexture, ivec2(int(realColorIndex), 0), 0);
  vec4 realColor = vec4(paletteColor.rgb, 1.0);
  if (realColorIndex >= 16u && realColorIndex < 32u) {
    float stage = float(realColorIndex - 16u);
    vec3 hsv = RGBtoHSV(uRemapColor);
    hsv.y = hsv.y * sin(stage * kPi / 67.5 + kPi / 3.6);
    hsv.z = hsv.z * cos(stage * 7.0 * kPi / 270.0 + kPi / 9.0);
    realColor = vec4(HSVtoRGB(hsv), 1.0);
  }

  vec3 projected = vxl_projection(voxelPos.xyz);
  projected.xy /= uCanvasInfo.xy / 2.0;
  projected.xy -= vec2(1.0);
  projected.y *= -1.0;
  // `uDepthParams` 这里使用的是和 2D 渲染器一致的 depth01 语义：0..1。
  // OpenGL 的裁剪空间 z 需要的是 -1..1，所以这里必须和 Renderer2D
  // 一样做一次 `depth01 -> clip-z` 映射。
  projected.z = uDepthParams.x + projected.z * uDepthParams.y;

  gl_Position = vec4(projected.xy, projected.z * 2.0 - 1.0, 1.0);
  vColor = realColor * (uCanvasInfo.z + 1.0);
}
)GLSL";

constexpr const char* kFragmentShaderSource = R"GLSL(
#version 330 core

in vec4 vColor;
out vec4 FragColor;

void main() {
  FragColor = vColor;
}
)GLSL";

constexpr const char* kShadowVertexShaderSource = R"GLSL(
#version 330 core

layout(location = 0) in vec3 aCorner;
layout(location = 1) in vec3 aVoxelPosition;

uniform vec4 uTransformRows[4];
uniform vec3 uModelOffset;
uniform float uScaleFactor;
uniform vec3 uCanvasInfo;
uniform vec2 uProjectionCenter;
uniform vec2 uDepthParams;

vec3 vxl_projection(vec3 modelPos) {
  const float f = 5000.0;
  modelPos.xy *= vec2(1.0, -1.0);

  vec3 result = vec3(0.0);
  result.x = uProjectionCenter.x + (modelPos.x - modelPos.y) / sqrt(2.0);
  result.y = uProjectionCenter.y + (modelPos.x + modelPos.y) / sqrt(8.0) - modelPos.z * sqrt(3.0) / 2.0;
  result.z = sqrt(3.0) / 2.0 / f *
             (4000.0 * sqrt(2.0) / 3.0 - (modelPos.x + modelPos.y) / sqrt(2.0) - modelPos.z / sqrt(3.0));
  return result;
}

vec4 mul_row_vec_mat4(vec4 value) {
  return vec4(
    dot(value, vec4(uTransformRows[0].x, uTransformRows[1].x, uTransformRows[2].x, uTransformRows[3].x)),
    dot(value, vec4(uTransformRows[0].y, uTransformRows[1].y, uTransformRows[2].y, uTransformRows[3].y)),
    dot(value, vec4(uTransformRows[0].z, uTransformRows[1].z, uTransformRows[2].z, uTransformRows[3].z)),
    dot(value, vec4(uTransformRows[0].w, uTransformRows[1].w, uTransformRows[2].w, uTransformRows[3].w))
  );
}

void main() {
  vec4 modelPos = vec4(aVoxelPosition + aCorner, 1.0);
  vec4 voxelPos = mul_row_vec_mat4(modelPos);
  voxelPos.xyz *= vec3(uScaleFactor);
  voxelPos.xyz += uModelOffset;

  // 红警里的体素单位阴影更接近“垂直压到地面”的投影，
  // 不跟随实时光照方向去拉长。
  vec3 shadowPos = voxelPos.xyz;
  shadowPos.z = 0.0;

  vec3 projected = vxl_projection(shadowPos);
  projected.xy /= uCanvasInfo.xy / 2.0;
  projected.xy -= vec2(1.0);
  projected.y *= -1.0;
  // 阴影和本体必须使用同一套深度语义，否则会和建筑的 world pass
  // 排序不一致。
  projected.z = uDepthParams.x + projected.z * uDepthParams.y;

  gl_Position = vec4(projected.xy, projected.z * 2.0 - 1.0, 1.0);
}
)GLSL";

constexpr const char* kShadowFragmentShaderSource = R"GLSL(
#version 330 core

uniform vec4 uShadowColor;
out vec4 FragColor;

void main() {
  FragColor = uShadowColor;
}
)GLSL";

[[nodiscard]] Matrix4f multiply(const Matrix4f& left, const Matrix4f& right) {
  Matrix4f result{};
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t column = 0; column < 4; ++column) {
      float sum = 0.0f;
      for (std::size_t k = 0; k < 4; ++k) {
        sum += left.at(row, k) * right.at(k, column);
      }
      result.at(row, column) = sum;
    }
  }
  return result;
}

[[nodiscard]] Matrix4f makeTranslationMatrix(const Vec3f& translation) {
  auto matrix = Matrix4f::identity();
  matrix.at(3, 0) = translation.x;
  matrix.at(3, 1) = translation.y;
  matrix.at(3, 2) = translation.z;
  return matrix;
}

[[nodiscard]] Matrix4f makeScalingMatrix(const Vec3f& scale) {
  auto matrix = Matrix4f::identity();
  matrix.at(0, 0) = scale.x;
  matrix.at(1, 1) = scale.y;
  matrix.at(2, 2) = scale.z;
  return matrix;
}

[[nodiscard]] Matrix4f makeRotationZMatrix(const float radians) {
  auto matrix = Matrix4f::identity();
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  matrix.at(0, 0) = c;
  matrix.at(0, 1) = s;
  matrix.at(1, 0) = -s;
  matrix.at(1, 1) = c;
  return matrix;
}

[[nodiscard]] Matrix4f makeBaseMatrix(const VxlMatrix3x4& source) {
  auto matrix = Matrix4f::identity();
  matrix.at(0, 0) = source.at(0, 0);
  matrix.at(0, 1) = source.at(1, 0);
  matrix.at(0, 2) = source.at(2, 0);
  matrix.at(1, 0) = source.at(0, 1);
  matrix.at(1, 1) = source.at(1, 1);
  matrix.at(1, 2) = source.at(2, 1);
  matrix.at(2, 0) = source.at(0, 2);
  matrix.at(2, 1) = source.at(1, 2);
  matrix.at(2, 2) = source.at(2, 2);
  matrix.at(3, 0) = source.at(0, 3);
  matrix.at(3, 1) = source.at(1, 3);
  matrix.at(3, 2) = source.at(2, 3);
  return matrix;
}

[[nodiscard]] float toRadians(const float degrees) {
  return degrees * (kPi / 180.0f);
}

[[nodiscard]] Matrix4f makeWorldMatrix(const std::array<float, 16>& values) {
  Matrix4f matrix{};
  matrix.values = values;
  return matrix;
}

[[nodiscard]] Matrix4f buildSectionTransform(const VxlSectionTailer& tailer,
                                             const VxlMatrix3x4& matrix,
                                             const float prerotationRadians,
                                             const float offsetLeptons,
                                             const Matrix4f& world) {
  const Vec3f minBounds{tailer.minBounds[0], tailer.minBounds[1], tailer.minBounds[2]};
  const Vec3f maxBounds{tailer.maxBounds[0], tailer.maxBounds[1], tailer.maxBounds[2]};
  const Vec3f dimensions{
    std::max(1.0f, static_cast<float>(tailer.xsize)),
    std::max(1.0f, static_cast<float>(tailer.ysize)),
    std::max(1.0f, static_cast<float>(tailer.zsize))
  };
  const Vec3f scaleVector{
    (maxBounds.x - minBounds.x) / dimensions.x,
    (maxBounds.y - minBounds.y) / dimensions.y,
    (maxBounds.z - minBounds.z) / dimensions.z
  };

  auto base = makeBaseMatrix(matrix);
  base.at(3, 0) *= scaleVector.x * tailer.scale;
  base.at(3, 1) *= scaleVector.y * tailer.scale;
  base.at(3, 2) *= scaleVector.z * tailer.scale;
  if (std::abs(prerotationRadians) > 1e-6f) {
    base = multiply(base, makeRotationZMatrix(prerotationRadians));
  }

  const auto translationToCenter = makeTranslationMatrix(minBounds);
  const auto scale = makeScalingMatrix(scaleVector);
  const auto offset = makeTranslationMatrix(Vec3f{offsetLeptons, 0.0f, 0.0f});
  return multiply(multiply(multiply(multiply(translationToCenter, scale), base), offset), world);
}

[[nodiscard]] GLuint compileShader(const GLenum type, const char* source) {
  const GLuint shader = glCreateShaderPtr(type);
  glShaderSourcePtr(shader, 1, &source, nullptr);
  glCompileShaderPtr(shader);

  GLint status = GL_FALSE;
  glGetShaderivPtr(shader, GL_COMPILE_STATUS, &status);
  if (status == GL_TRUE) {
    return shader;
  }

  GLint logLength = 0;
  glGetShaderivPtr(shader, GL_INFO_LOG_LENGTH, &logLength);
  std::string log(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
  glGetShaderInfoLogPtr(shader, logLength, nullptr, log.data());
  glDeleteShaderPtr(shader);
  throw std::runtime_error("Failed to compile shader: " + log);
}

[[nodiscard]] GLuint createProgram(const char* vertexSource, const char* fragmentSource) {
  const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
  const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);

  const GLuint program = glCreateProgramPtr();
  glAttachShaderPtr(program, vertexShader);
  glAttachShaderPtr(program, fragmentShader);
  glLinkProgramPtr(program);

  glDeleteShaderPtr(vertexShader);
  glDeleteShaderPtr(fragmentShader);

  GLint status = GL_FALSE;
  glGetProgramivPtr(program, GL_LINK_STATUS, &status);
  if (status == GL_TRUE) {
    return program;
  }

  GLint logLength = 0;
  glGetProgramivPtr(program, GL_INFO_LOG_LENGTH, &logLength);
  std::string log(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
  glGetProgramInfoLogPtr(program, logLength, nullptr, log.data());
  glDeleteProgramPtr(program);
  throw std::runtime_error("Failed to link shader program: " + log);
}

[[nodiscard]] GLuint createProgram() {
  return createProgram(kVertexShaderSource, kFragmentShaderSource);
}

[[nodiscard]] GLuint uploadTexture(const int width,
                                   const int height,
                                   const std::vector<std::uint8_t>& rgba) {
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  glBindTexture(GL_TEXTURE_2D, 0);
  return texture;
}

void uploadPaletteTexture(const Palette& palette, GLuint texture) {
  std::array<std::uint8_t, 256 * 4> bytes{};
  for (std::size_t i = 0; i < 256; ++i) {
    const auto color = palette.color(static_cast<std::uint8_t>(i));
    bytes[i * 4 + 0] = color.r;
    bytes[i * 4 + 1] = color.g;
    bytes[i * 4 + 2] = color.b;
    bytes[i * 4 + 3] = 255;
  }

  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, bytes.data());
  glBindTexture(GL_TEXTURE_2D, 0);
}

[[nodiscard]] GLuint createVplTexture(const VplFile& vpl) {
  const int width = 256;
  const int height = static_cast<int>(vpl.sectionCount());
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width * height), 0);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      bytes[static_cast<std::size_t>(y * width + x)] =
        vpl.mapIndex(static_cast<std::size_t>(y), static_cast<std::uint8_t>(x));
    }
  }

  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, width, height, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, bytes.data());
  glBindTexture(GL_TEXTURE_2D, 0);
  return texture;
}

[[nodiscard]] std::vector<SectionGpuData> buildSections(const VxlFile& vxl) {
  std::vector<SectionGpuData> sections;
  sections.reserve(vxl.sectionCount());

  for (std::size_t sectionIndex = 0; sectionIndex < vxl.sectionCount(); ++sectionIndex) {
    const auto& section = vxl.section(sectionIndex);
    std::vector<VoxelInstance> voxels;
    voxels.reserve(static_cast<std::size_t>(section.tailer().xsize) *
                   static_cast<std::size_t>(section.tailer().ysize) *
                   static_cast<std::size_t>(section.tailer().zsize));

    for (std::size_t z = 0; z < section.tailer().zsize; ++z) {
      for (std::size_t y = 0; y < section.tailer().ysize; ++y) {
        for (std::size_t x = 0; x < section.tailer().xsize; ++x) {
          const auto sample = section.voxel(x, y, z);
          if (sample.color == 0) {
            continue;
          }

          voxels.push_back(VoxelInstance{
            static_cast<float>(x),
            static_cast<float>(y),
            static_cast<float>(z),
            sample.color,
            sample.normal,
            0,
            0
          });
        }
      }
    }

    GLuint buffer = 0;
    glGenBuffersPtr(1, &buffer);
    glBindBufferPtr(GL_ARRAY_BUFFER, buffer);
    glBufferDataPtr(GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(voxels.size() * sizeof(VoxelInstance)),
                    voxels.data(),
                    GL_STATIC_DRAW);

    sections.push_back(SectionGpuData{
      section.header().name,
      section.tailer(),
      buffer,
      static_cast<GLsizei>(voxels.size())
    });
  }

  glBindBufferPtr(GL_ARRAY_BUFFER, 0);
  return sections;
}

[[nodiscard]] std::vector<std::uint8_t> flipRowsTopDown(const int width,
                                                        const int height,
                                                        const std::vector<std::uint8_t>& rgba) {
  std::vector<std::uint8_t> flipped(rgba.size(), 0);
  const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
  for (int y = 0; y < height; ++y) {
    const std::size_t srcOffset = static_cast<std::size_t>(height - 1 - y) * rowBytes;
    const std::size_t dstOffset = static_cast<std::size_t>(y) * rowBytes;
    std::copy_n(rgba.data() + srcOffset, rowBytes, flipped.data() + dstOffset);
  }
  return flipped;
}
}  // namespace

struct VplBoxRenderer::Impl {
  GlHandles gl{};
  RhinoPartData body;
  RhinoPartData turret;
  RhinoPartData barrel;
  VoxelNormalTableKind detectedNormalTableKind = VoxelNormalTableKind::Ra2Index4;
  std::uint8_t bodyNormalTypeIndex = 4;
  std::uint8_t turretNormalTypeIndex = 4;
  std::uint8_t barrelNormalTypeIndex = 4;

  void destroyPart(RhinoPartData& part) const {
    for (auto& section : part.sections) {
      if (section.instanceBuffer != 0) {
        glDeleteBuffersPtr(1, &section.instanceBuffer);
        section.instanceBuffer = 0;
      }
    }
    part.sections.clear();
  }

  void destroyOffscreenTargets() {
    if (gl.offscreenDepthRenderbuffer != 0) {
      glDeleteRenderbuffersPtr(1, &gl.offscreenDepthRenderbuffer);
      gl.offscreenDepthRenderbuffer = 0;
    }
    if (gl.offscreenColorTexture != 0) {
      glDeleteTextures(1, &gl.offscreenColorTexture);
      gl.offscreenColorTexture = 0;
    }
    if (gl.offscreenFbo != 0) {
      glDeleteFramebuffersPtr(1, &gl.offscreenFbo);
      gl.offscreenFbo = 0;
    }
    gl.offscreenWidth = 0;
    gl.offscreenHeight = 0;
  }

  void destroy() {
    destroyPart(body);
    destroyPart(turret);
    destroyPart(barrel);
    destroyOffscreenTargets();

    if (gl.paletteTexture != 0) {
      glDeleteTextures(1, &gl.paletteTexture);
      gl.paletteTexture = 0;
    }
    if (gl.vplTexture != 0) {
      glDeleteTextures(1, &gl.vplTexture);
      gl.vplTexture = 0;
    }
    if (gl.cubeBuffer != 0) {
      glDeleteBuffersPtr(1, &gl.cubeBuffer);
      gl.cubeBuffer = 0;
    }
    if (gl.vao != 0) {
      glDeleteVertexArraysPtr(1, &gl.vao);
      gl.vao = 0;
    }
    if (gl.program != 0) {
      glDeleteProgramPtr(gl.program);
      gl.program = 0;
    }
    if (gl.shadowProgram != 0) {
      glDeleteProgramPtr(gl.shadowProgram);
      gl.shadowProgram = 0;
    }
  }

  void ensureOffscreenTargets(const int width, const int height) {
    if (gl.offscreenFbo != 0 && gl.offscreenWidth == width && gl.offscreenHeight == height) {
      return;
    }

    destroyOffscreenTargets();

    glGenFramebuffersPtr(1, &gl.offscreenFbo);
    glBindFramebufferPtr(GL_FRAMEBUFFER, gl.offscreenFbo);

    glGenTextures(1, &gl.offscreenColorTexture);
    glBindTexture(GL_TEXTURE_2D, gl.offscreenColorTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2DPtr(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl.offscreenColorTexture, 0);

    glGenRenderbuffersPtr(1, &gl.offscreenDepthRenderbuffer);
    glBindRenderbufferPtr(GL_RENDERBUFFER, gl.offscreenDepthRenderbuffer);
    glRenderbufferStoragePtr(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbufferPtr(GL_FRAMEBUFFER,
                                 GL_DEPTH_ATTACHMENT,
                                 GL_RENDERBUFFER,
                                 gl.offscreenDepthRenderbuffer);

    const auto status = glCheckFramebufferStatusPtr(GL_FRAMEBUFFER);
    glBindRenderbufferPtr(GL_RENDERBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebufferPtr(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
      destroyOffscreenTargets();
      throw std::runtime_error("Failed to create offscreen framebuffer for VPL box renderer");
    }

    gl.offscreenWidth = width;
    gl.offscreenHeight = height;
  }
};

VplBoxRenderer::~VplBoxRenderer() {
  destroy();
}

void VplBoxRenderer::destroy() {
  if (impl_ != nullptr) {
    impl_->destroy();
    delete impl_;
    impl_ = nullptr;
  }
  initialized_ = false;
}

void VplBoxRenderer::initialize(SDL_Window* window) {
  if (initialized_) {
    return;
  }
  if (window == nullptr) {
    throw std::runtime_error("VPL box renderer initialization requires a valid SDL window");
  }

  ensureOpenGlLoaded();
  impl_ = new Impl();

  impl_->gl.program = createProgram();
  impl_->gl.shadowProgram = createProgram(kShadowVertexShaderSource, kShadowFragmentShaderSource);
  impl_->gl.transformRowsLocation = glGetUniformLocationPtr(impl_->gl.program, "uTransformRows[0]");
  impl_->gl.lightDirectionLocation = glGetUniformLocationPtr(impl_->gl.program, "uLightDir");
  impl_->gl.remapColorLocation = glGetUniformLocationPtr(impl_->gl.program, "uRemapColor");
  impl_->gl.modelOffsetLocation = glGetUniformLocationPtr(impl_->gl.program, "uModelOffset");
  impl_->gl.scaleFactorLocation = glGetUniformLocationPtr(impl_->gl.program, "uScaleFactor");
  impl_->gl.canvasInfoLocation = glGetUniformLocationPtr(impl_->gl.program, "uCanvasInfo");
  impl_->gl.projectionCenterLocation = glGetUniformLocationPtr(impl_->gl.program, "uProjectionCenter");
  impl_->gl.depthParamsLocation = glGetUniformLocationPtr(impl_->gl.program, "uDepthParams");
  impl_->gl.normalTableLocation = glGetUniformLocationPtr(impl_->gl.program, "uNormalTable[0]");
  impl_->gl.paletteTextureLocation = glGetUniformLocationPtr(impl_->gl.program, "uPaletteTexture");
  impl_->gl.vplTextureLocation = glGetUniformLocationPtr(impl_->gl.program, "uVplTexture");
  impl_->gl.shadowTransformRowsLocation = glGetUniformLocationPtr(impl_->gl.shadowProgram, "uTransformRows[0]");
  impl_->gl.shadowLightDirectionLocation = glGetUniformLocationPtr(impl_->gl.shadowProgram, "uLightDir");
  impl_->gl.shadowModelOffsetLocation = glGetUniformLocationPtr(impl_->gl.shadowProgram, "uModelOffset");
  impl_->gl.shadowScaleFactorLocation = glGetUniformLocationPtr(impl_->gl.shadowProgram, "uScaleFactor");
  impl_->gl.shadowCanvasInfoLocation = glGetUniformLocationPtr(impl_->gl.shadowProgram, "uCanvasInfo");
  impl_->gl.shadowProjectionCenterLocation = glGetUniformLocationPtr(impl_->gl.shadowProgram, "uProjectionCenter");
  impl_->gl.shadowDepthParamsLocation = glGetUniformLocationPtr(impl_->gl.shadowProgram, "uDepthParams");
  impl_->gl.shadowColorLocation = glGetUniformLocationPtr(impl_->gl.shadowProgram, "uShadowColor");

  glGenVertexArraysPtr(1, &impl_->gl.vao);
  glBindVertexArrayPtr(impl_->gl.vao);

  glGenBuffersPtr(1, &impl_->gl.cubeBuffer);
  glBindBufferPtr(GL_ARRAY_BUFFER, impl_->gl.cubeBuffer);
  glBufferDataPtr(GL_ARRAY_BUFFER,
                  static_cast<GLsizeiptr>(kCubeVertices.size() * sizeof(float)),
                  kCubeVertices.data(),
                  GL_STATIC_DRAW);
  glEnableVertexAttribArrayPtr(0);
  glVertexAttribPointerPtr(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, nullptr);

  glEnableVertexAttribArrayPtr(1);
  glVertexAttribPointerPtr(1,
                           3,
                           GL_FLOAT,
                           GL_FALSE,
                           sizeof(VoxelInstance),
                           reinterpret_cast<const void*>(offsetof(VoxelInstance, x)));
  glVertexAttribDivisorPtr(1, 1);

  glEnableVertexAttribArrayPtr(2);
  glVertexAttribPointerPtr(2,
                           2,
                           GL_UNSIGNED_BYTE,
                           GL_FALSE,
                           sizeof(VoxelInstance),
                           reinterpret_cast<const void*>(offsetof(VoxelInstance, color)));
  glVertexAttribDivisorPtr(2, 1);

  glBindVertexArrayPtr(0);
  glBindBufferPtr(GL_ARRAY_BUFFER, 0);

  glGenTextures(1, &impl_->gl.paletteTexture);

  initialized_ = true;
}

void VplBoxRenderer::loadRhinoAssets(const std::filesystem::path& voxelRoot, const VplFile& vpl) {
  loadVehicleAssets(voxelRoot, vpl, "htnk", "htnktur", "htnkbarl");
}

void VplBoxRenderer::loadVehicleAssets(const std::filesystem::path& voxelRoot,
                                       const VplFile& vpl,
                                       const std::string& bodyStem,
                                       const std::string& turretStem,
                                       const std::string& barrelStem) {
  if (!initialized_ || impl_ == nullptr) {
    throw std::runtime_error("Renderer must be initialized before loading assets");
  }
  if (bodyStem.empty()) {
    throw std::runtime_error("Vehicle VXL body stem must not be empty");
  }

  impl_->destroyPart(impl_->body);
  impl_->destroyPart(impl_->turret);
  impl_->destroyPart(impl_->barrel);

  const auto bodyVxl = VxlFile::load(voxelRoot / (bodyStem + ".vxl"));

  impl_->bodyNormalTypeIndex = bodyVxl.normalTypeIndex();
  const auto bodyNormalKind = detectVoxelNormalTableKind(impl_->bodyNormalTypeIndex);
  int tsVotes = static_cast<int>(bodyNormalKind == VoxelNormalTableKind::TsIndex2);
  int normalVotes = 1;

  impl_->body = RhinoPartData{
    "body",
    HvaFile::load(voxelRoot / (bodyStem + ".hva")),
    buildSections(bodyVxl)
  };
  auto loadOptionalPart = [&](RhinoPartData& part,
                              std::uint8_t& normalTypeIndex,
                              const std::string& stem,
                              const char* label) {
    normalTypeIndex = 0;
    if (stem.empty()) {
      part = RhinoPartData{};
      return;
    }

    const auto vxl = VxlFile::load(voxelRoot / (stem + ".vxl"));
    normalTypeIndex = vxl.normalTypeIndex();
    const auto normalKind = detectVoxelNormalTableKind(normalTypeIndex);
    tsVotes += static_cast<int>(normalKind == VoxelNormalTableKind::TsIndex2);
    ++normalVotes;
    part = RhinoPartData{
      label,
      HvaFile::load(voxelRoot / (stem + ".hva")),
      buildSections(vxl)
    };
  };
  loadOptionalPart(impl_->turret, impl_->turretNormalTypeIndex, turretStem, "turret");
  loadOptionalPart(impl_->barrel, impl_->barrelNormalTypeIndex, barrelStem, "barrel");
  impl_->detectedNormalTableKind =
    tsVotes * 2 >= normalVotes ? VoxelNormalTableKind::TsIndex2 : VoxelNormalTableKind::Ra2Index4;

  if (impl_->gl.vplTexture != 0) {
    glDeleteTextures(1, &impl_->gl.vplTexture);
    impl_->gl.vplTexture = 0;
  }
  impl_->gl.vplTexture = createVplTexture(vpl);
}

void VplBoxRenderer::loadTurretAssets(const std::filesystem::path& voxelRoot,
                                      const VplFile& vpl,
                                      const std::string& turretStem,
                                      const std::string& barrelStem) {
  if (!initialized_ || impl_ == nullptr) {
    throw std::runtime_error("Renderer must be initialized before loading assets");
  }
  if (turretStem.empty()) {
    throw std::runtime_error("Turret VXL stem must not be empty");
  }

  impl_->destroyPart(impl_->body);
  impl_->destroyPart(impl_->turret);
  impl_->destroyPart(impl_->barrel);
  impl_->body = RhinoPartData{};
  impl_->bodyNormalTypeIndex = 0;

  int tsVotes = 0;
  int normalVotes = 0;
  auto loadOptionalPart = [&](RhinoPartData& part,
                              std::uint8_t& normalTypeIndex,
                              const std::string& stem,
                              const char* label) {
    normalTypeIndex = 0;
    if (stem.empty()) {
      part = RhinoPartData{};
      return;
    }

    const auto vxl = VxlFile::load(voxelRoot / (stem + ".vxl"));
    normalTypeIndex = vxl.normalTypeIndex();
    const auto normalKind = detectVoxelNormalTableKind(normalTypeIndex);
    tsVotes += static_cast<int>(normalKind == VoxelNormalTableKind::TsIndex2);
    ++normalVotes;
    part = RhinoPartData{
      label,
      HvaFile::load(voxelRoot / (stem + ".hva")),
      buildSections(vxl)
    };
  };

  loadOptionalPart(impl_->turret, impl_->turretNormalTypeIndex, turretStem, "turret");
  loadOptionalPart(impl_->barrel, impl_->barrelNormalTypeIndex, barrelStem, "barrel");
  impl_->detectedNormalTableKind =
    normalVotes > 0 && tsVotes * 2 >= normalVotes ? VoxelNormalTableKind::TsIndex2
                                                  : VoxelNormalTableKind::Ra2Index4;

  if (impl_->gl.vplTexture != 0) {
    glDeleteTextures(1, &impl_->gl.vplTexture);
    impl_->gl.vplTexture = 0;
  }
  impl_->gl.vplTexture = createVplTexture(vpl);
}

void VplBoxRenderer::setPalette(const Palette& palette) {
  if (!initialized_ || impl_ == nullptr) {
    throw std::runtime_error("Renderer must be initialized before updating palette");
  }
  uploadPaletteTexture(palette, impl_->gl.paletteTexture);
}

namespace {
void configureRenderState() {
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LEQUAL);
}

void configureShadowRenderState() {
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glDepthFunc(GL_LEQUAL);
}

void uploadNormalTable(const GlHandles& gl, const VoxelNormalTableKind kind) {
  const auto& normalTable = voxelNormalTableForKind(kind);
  std::array<float, 256 * 4> normalValues{};
  for (std::size_t i = 0; i < normalTable.size(); ++i) {
    normalValues[i * 4 + 0] = normalTable[i].x;
    normalValues[i * 4 + 1] = normalTable[i].y;
    normalValues[i * 4 + 2] = normalTable[i].z;
    normalValues[i * 4 + 3] = normalTable[i].w;
  }
  glUniform4fvPtr(gl.normalTableLocation, 256, normalValues.data());
}

void bindPaletteAndVplTextures(const GlHandles& gl) {
  glActiveTexturePtr(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, gl.paletteTexture);
  glActiveTexturePtr(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, gl.vplTexture);
  glActiveTexturePtr(GL_TEXTURE0);
  glUniform1iPtr(gl.paletteTextureLocation, 0);
  glUniform1iPtr(gl.vplTextureLocation, 1);
}

void unbindPaletteAndVplTextures() {
  glActiveTexturePtr(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexturePtr(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
}

void bindSectionInstanceBuffer(const SectionGpuData& section) {
  glBindBufferPtr(GL_ARRAY_BUFFER, section.instanceBuffer);
  glVertexAttribPointerPtr(1,
                           3,
                           GL_FLOAT,
                           GL_FALSE,
                           sizeof(VoxelInstance),
                           reinterpret_cast<const void*>(offsetof(VoxelInstance, x)));
  glVertexAttribPointerPtr(2,
                           2,
                           GL_UNSIGNED_BYTE,
                           GL_FALSE,
                           sizeof(VoxelInstance),
                           reinterpret_cast<const void*>(offsetof(VoxelInstance, color)));
}

void drawRhinoPartSections(const RhinoPartData& part,
                           const Matrix4f& world,
                           const float prerotation,
                           const float offset,
                           const GLint transformRowsLocation) {
  if (part.hva.sectionCount() == 0 || part.hva.frameCount() == 0) {
    return;
  }

  const auto realFrame = static_cast<std::size_t>(0);
  const auto sectionCount = std::min(part.sections.size(), part.hva.sectionCount());
  for (std::size_t sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex) {
    const auto& section = part.sections[sectionIndex];
    if (section.instanceCount <= 0) {
      continue;
    }

    const auto transform =
      buildSectionTransform(section.tailer,
                            part.hva.matrix(realFrame, sectionIndex),
                            prerotation,
                            offset,
                            world);
    glUniform4fvPtr(transformRowsLocation, 4, transform.values.data());
    bindSectionInstanceBuffer(section);
    glDrawArraysInstancedPtr(GL_TRIANGLES, 0, 36, section.instanceCount);
  }
}
}  // namespace

void VplBoxRenderer::renderToScreen(const VplBoxRendererState& state,
                                    const int drawableWidth,
                                    const int drawableHeight) {
  if (!initialized_ || impl_ == nullptr) {
    throw std::runtime_error("Renderer has not been initialized");
  }
  if (drawableWidth <= 0 || drawableHeight <= 0) {
    return;
  }

  glBindFramebufferPtr(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, drawableWidth, drawableHeight);
  configureRenderState();
  glClearColor(static_cast<float>(state.backgroundColor.r) / 255.0f,
               static_cast<float>(state.backgroundColor.g) / 255.0f,
               static_cast<float>(state.backgroundColor.b) / 255.0f,
               1.0f);
  glClearDepth(1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  const Matrix4f world = makeWorldMatrix(state.worldTransform);
  const float bodyRotation = -toRadians(state.bodyRotationDegrees);
  const float turretRotation = -toRadians(state.bodyRotationDegrees + state.turretRotationDegrees);
  const float turretOffset = state.turretOffsetPixels * kPixelsToLeptons;
  const auto activeNormalTableKind =
    resolveVoxelNormalTableKind(state.normalTableSelection, impl_->detectedNormalTableKind);

  glUseProgramPtr(impl_->gl.program);
  glUniform3fPtr(impl_->gl.lightDirectionLocation,
                 state.lightDirection[0],
                 state.lightDirection[1],
                 state.lightDirection[2]);
  glUniform3fPtr(impl_->gl.remapColorLocation,
                 static_cast<float>(state.remapColor.r) / 255.0f,
                 static_cast<float>(state.remapColor.g) / 255.0f,
                 static_cast<float>(state.remapColor.b) / 255.0f);
  glUniform3fPtr(impl_->gl.modelOffsetLocation,
                 state.modelOffset[0],
                 state.modelOffset[1],
                 state.modelOffset[2]);
  glUniform1fPtr(impl_->gl.scaleFactorLocation, state.scaleFactor);

  const std::array<float, 3> canvasInfo{
    static_cast<float>(drawableWidth),
    static_cast<float>(drawableHeight),
    state.extraLight
  };
  glUniform3fvPtr(impl_->gl.canvasInfoLocation, 1, canvasInfo.data());
  glUniform2fPtr(impl_->gl.projectionCenterLocation,
                 static_cast<float>(drawableWidth) * 0.5f,
                 static_cast<float>(drawableHeight) * 0.5f);
  glUniform2fPtr(impl_->gl.depthParamsLocation, 0.5f, 1.0f);

  uploadNormalTable(impl_->gl, activeNormalTableKind);
  bindPaletteAndVplTextures(impl_->gl);

  glBindVertexArrayPtr(impl_->gl.vao);
  drawRhinoPartSections(impl_->body, world, bodyRotation, 0.0f, impl_->gl.transformRowsLocation);
  drawRhinoPartSections(impl_->turret, world, turretRotation, turretOffset, impl_->gl.transformRowsLocation);
  drawRhinoPartSections(impl_->barrel, world, turretRotation, turretOffset, impl_->gl.transformRowsLocation);

  glBindBufferPtr(GL_ARRAY_BUFFER, 0);
  glBindVertexArrayPtr(0);
  unbindPaletteAndVplTextures();
}

UiTexture VplBoxRenderer::renderToTexture(const VplBoxRendererState& state, const int width, const int height) {
  if (!initialized_ || impl_ == nullptr) {
    throw std::runtime_error("Renderer has not been initialized");
  }
  if (width <= 0 || height <= 0) {
    throw std::runtime_error("Offscreen VPL render target must have positive size");
  }

  impl_->ensureOffscreenTargets(width, height);
  glBindFramebufferPtr(GL_FRAMEBUFFER, impl_->gl.offscreenFbo);
  glViewport(0, 0, width, height);
  configureRenderState();
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClearDepth(1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  const Matrix4f world = makeWorldMatrix(state.worldTransform);
  const float bodyRotation = -toRadians(state.bodyRotationDegrees);
  const float turretRotation = -toRadians(state.bodyRotationDegrees + state.turretRotationDegrees);
  const float turretOffset = state.turretOffsetPixels * kPixelsToLeptons;
  const auto activeNormalTableKind =
    resolveVoxelNormalTableKind(state.normalTableSelection, impl_->detectedNormalTableKind);

  glUseProgramPtr(impl_->gl.program);
  glUniform3fPtr(impl_->gl.lightDirectionLocation,
                 state.lightDirection[0],
                 state.lightDirection[1],
                 state.lightDirection[2]);
  glUniform3fPtr(impl_->gl.remapColorLocation,
                 static_cast<float>(state.remapColor.r) / 255.0f,
                 static_cast<float>(state.remapColor.g) / 255.0f,
                 static_cast<float>(state.remapColor.b) / 255.0f);
  glUniform3fPtr(impl_->gl.modelOffsetLocation,
                 state.modelOffset[0],
                 state.modelOffset[1],
                 state.modelOffset[2]);
  glUniform1fPtr(impl_->gl.scaleFactorLocation, state.scaleFactor);

  const std::array<float, 3> canvasInfo{
    static_cast<float>(width),
    static_cast<float>(height),
    state.extraLight
  };
  glUniform3fvPtr(impl_->gl.canvasInfoLocation, 1, canvasInfo.data());
  glUniform2fPtr(impl_->gl.projectionCenterLocation,
                 static_cast<float>(width) * 0.5f,
                 static_cast<float>(height) * 0.5f);
  glUniform2fPtr(impl_->gl.depthParamsLocation, 0.5f, 1.0f);

  uploadNormalTable(impl_->gl, activeNormalTableKind);
  bindPaletteAndVplTextures(impl_->gl);

  glBindVertexArrayPtr(impl_->gl.vao);
  drawRhinoPartSections(impl_->body, world, bodyRotation, 0.0f, impl_->gl.transformRowsLocation);
  drawRhinoPartSections(impl_->turret, world, turretRotation, turretOffset, impl_->gl.transformRowsLocation);
  drawRhinoPartSections(impl_->barrel, world, turretRotation, turretOffset, impl_->gl.transformRowsLocation);

  glBindBufferPtr(GL_ARRAY_BUFFER, 0);
  glBindVertexArrayPtr(0);
  unbindPaletteAndVplTextures();

  std::vector<std::uint8_t> rawPixels(static_cast<std::size_t>(width * height * 4), 0);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rawPixels.data());
  glBindFramebufferPtr(GL_FRAMEBUFFER, 0);

  auto rgba = flipRowsTopDown(width, height, rawPixels);
  return UiTexture{
    uploadTexture(width, height, rgba),
    0,
    width,
    height,
    std::move(rgba)
  };
}

void VplBoxRenderer::renderShadowInWorld(const VplBoxRendererState& state,
                                         const int viewportWidth,
                                         const int viewportHeight,
                                         const float screenCenterX,
                                         const float screenCenterY,
                                         const float depthBase01,
                                         const float depthScale01) {
  if (!initialized_ || impl_ == nullptr) {
    throw std::runtime_error("Renderer has not been initialized");
  }
  if (viewportWidth <= 0 || viewportHeight <= 0) {
    return;
  }

  glBindFramebufferPtr(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, viewportWidth, viewportHeight);
  configureShadowRenderState();

  const Matrix4f world = makeWorldMatrix(state.worldTransform);
  const float bodyRotation = -toRadians(state.bodyRotationDegrees);

  glUseProgramPtr(impl_->gl.shadowProgram);
  glUniform3fPtr(impl_->gl.shadowModelOffsetLocation,
                 state.modelOffset[0],
                 state.modelOffset[1],
                 state.modelOffset[2]);
  glUniform1fPtr(impl_->gl.shadowScaleFactorLocation, state.scaleFactor);
  const std::array<float, 3> canvasInfo{
    static_cast<float>(viewportWidth),
    static_cast<float>(viewportHeight),
    state.extraLight
  };
  glUniform3fvPtr(impl_->gl.shadowCanvasInfoLocation, 1, canvasInfo.data());
  glUniform2fPtr(impl_->gl.shadowProjectionCenterLocation, screenCenterX, screenCenterY);
  glUniform2fPtr(impl_->gl.shadowDepthParamsLocation, depthBase01, depthScale01);
  glUniform4fPtr(impl_->gl.shadowColorLocation,
                 state.shadowGray,
                 state.shadowGray,
                 state.shadowGray,
                 state.shadowAlpha);

  glBindVertexArrayPtr(impl_->gl.vao);
  drawRhinoPartSections(impl_->body, world, bodyRotation, 0.0f, impl_->gl.shadowTransformRowsLocation);
  glBindBufferPtr(GL_ARRAY_BUFFER, 0);
  glBindVertexArrayPtr(0);

  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
}

void VplBoxRenderer::renderInWorld(const VplBoxRendererState& state,
                                   const int viewportWidth,
                                   const int viewportHeight,
                                   const float screenCenterX,
                                   const float screenCenterY,
                                   const float depthBase01,
                                   const float depthScale01) {
  if (!initialized_ || impl_ == nullptr) {
    throw std::runtime_error("Renderer has not been initialized");
  }
  if (viewportWidth <= 0 || viewportHeight <= 0) {
    return;
  }

  glBindFramebufferPtr(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, viewportWidth, viewportHeight);
  configureRenderState();

  const Matrix4f world = makeWorldMatrix(state.worldTransform);
  const float bodyRotation = -toRadians(state.bodyRotationDegrees);
  const float turretRotation = -toRadians(state.bodyRotationDegrees + state.turretRotationDegrees);
  const float turretOffset = state.turretOffsetPixels * kPixelsToLeptons;
  const auto activeNormalTableKind =
    resolveVoxelNormalTableKind(state.normalTableSelection, impl_->detectedNormalTableKind);

  glUseProgramPtr(impl_->gl.program);
  glUniform3fPtr(impl_->gl.lightDirectionLocation,
                 state.lightDirection[0],
                 state.lightDirection[1],
                 state.lightDirection[2]);
  glUniform3fPtr(impl_->gl.remapColorLocation,
                 static_cast<float>(state.remapColor.r) / 255.0f,
                 static_cast<float>(state.remapColor.g) / 255.0f,
                 static_cast<float>(state.remapColor.b) / 255.0f);
  glUniform3fPtr(impl_->gl.modelOffsetLocation,
                 state.modelOffset[0],
                 state.modelOffset[1],
                 state.modelOffset[2]);
  glUniform1fPtr(impl_->gl.scaleFactorLocation, state.scaleFactor);

  const std::array<float, 3> canvasInfo{
    static_cast<float>(viewportWidth),
    static_cast<float>(viewportHeight),
    state.extraLight
  };
  glUniform3fvPtr(impl_->gl.canvasInfoLocation, 1, canvasInfo.data());
  glUniform2fPtr(impl_->gl.projectionCenterLocation, screenCenterX, screenCenterY);
  glUniform2fPtr(impl_->gl.depthParamsLocation, depthBase01, depthScale01);

  uploadNormalTable(impl_->gl, activeNormalTableKind);
  bindPaletteAndVplTextures(impl_->gl);

  glBindVertexArrayPtr(impl_->gl.vao);
  drawRhinoPartSections(impl_->body, world, bodyRotation, 0.0f, impl_->gl.transformRowsLocation);
  drawRhinoPartSections(impl_->turret, world, turretRotation, turretOffset, impl_->gl.transformRowsLocation);
  drawRhinoPartSections(impl_->barrel, world, turretRotation, turretOffset, impl_->gl.transformRowsLocation);

  glBindBufferPtr(GL_ARRAY_BUFFER, 0);
  glBindVertexArrayPtr(0);
  unbindPaletteAndVplTextures();
}

std::string VplBoxRenderer::loadedAssetSummary() const {
  if (impl_ == nullptr) {
    return "renderer not initialized";
  }

  const auto sectionCount =
    impl_->body.sections.size() + impl_->turret.sections.size() + impl_->barrel.sections.size();
  std::ostringstream summary;
  summary << "body sections=" << impl_->body.sections.size()
          << ", turret sections=" << impl_->turret.sections.size()
          << ", barrel sections=" << impl_->barrel.sections.size()
          << ", total=" << sectionCount
          << ", normals=" << voxelNormalTableName(impl_->detectedNormalTableKind)
          << " [body=" << static_cast<int>(impl_->bodyNormalTypeIndex)
          << ", turret=" << static_cast<int>(impl_->turretNormalTypeIndex)
          << ", barrel=" << static_cast<int>(impl_->barrelNormalTypeIndex) << "]";
  return summary.str();
}
