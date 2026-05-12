#include "vpl_box_renderer.h"
#include "gl_loader.h"
#include "palette.h"
#include "voxel_normals.h"
#include "vpl_file.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <cmath>
#include <stdexcept>
#include <string>

namespace {
#ifdef VXL_RENDER_EXPERIMENT_ROOT
constexpr const char* kExperimentRoot = VXL_RENDER_EXPERIMENT_ROOT;
#else
constexpr const char* kExperimentRoot = ".";
#endif

[[nodiscard]] std::filesystem::path experimentRoot() {
  return std::filesystem::path(kExperimentRoot);
}

[[nodiscard]] std::filesystem::path locateProjectRoot(std::filesystem::path current) {
  while (!current.empty()) {
    if (std::filesystem::exists(current / "assets")) {
      return current;
    }
    if (current == current.root_path()) {
      break;
    }
    current = current.parent_path();
  }

  throw std::runtime_error("Could not locate project root containing assets/");
}

struct ExperimentRgbColor {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 255;
};

struct ExperimentRendererState {
  float rotationThetaDegrees = 0.0f;
  float rotationPhiDegrees = 0.0f;
  float xyAngleDegrees = 0.0f;
  float zAngleDegrees = 0.0f;
  float scale = 1.0f;
  float turretRotationDegrees = 0.0f;
  float turretOffsetPixels = 0.0f;
  float extraLight = 0.2f;
  VoxelNormalTableSelection normalTableSelection = VoxelNormalTableSelection::AutoFromVxl;
  std::array<float, 3> lightDirection{0.2013022f, -0.9101138f, -0.3621709f};
  ExperimentRgbColor remapColor{252, 0, 0, 255};
  ExperimentRgbColor backgroundColor{0, 0, 255, 255};
};

[[nodiscard]] VplBoxRendererState toSharedState(const ExperimentRendererState& state) {
  VplBoxRendererState shared;
  shared.lightDirection = state.lightDirection;
  shared.bodyRotationDegrees = 0.0f;
  shared.scaleFactor = state.scale;
  shared.turretRotationDegrees = state.turretRotationDegrees;
  shared.turretOffsetPixels = state.turretOffsetPixels;
  shared.extraLight = state.extraLight;
  shared.normalTableSelection = state.normalTableSelection;
  shared.remapColor = Rgba{state.remapColor.r, state.remapColor.g, state.remapColor.b, state.remapColor.a};
  shared.backgroundColor = Rgba{
    state.backgroundColor.r,
    state.backgroundColor.g,
    state.backgroundColor.b,
    state.backgroundColor.a
  };

  const auto toRadians = [](const float degrees) {
    return degrees * 3.14159265358979323846f / 180.0f;
  };
  const auto identity = std::array<float, 16>{
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
  };
  const auto multiply = [](const std::array<float, 16>& left, const std::array<float, 16>& right) {
    std::array<float, 16> result{};
    for (int row = 0; row < 4; ++row) {
      for (int column = 0; column < 4; ++column) {
        float sum = 0.0f;
        for (int k = 0; k < 4; ++k) {
          sum += left[row * 4 + k] * right[k * 4 + column];
        }
        result[row * 4 + column] = sum;
      }
    }
    return result;
  };
  const auto makeRotationY = [&](const float radians) {
    auto matrix = identity;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    matrix[0] = c;
    matrix[2] = -s;
    matrix[8] = s;
    matrix[10] = c;
    return matrix;
  };
  const auto makeRotationZ = [&](const float radians) {
    auto matrix = identity;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    matrix[0] = c;
    matrix[1] = s;
    matrix[4] = -s;
    matrix[5] = c;
    return matrix;
  };
  const auto makeAxisRotation = [&](const float radians) {
    const float axisLength = std::sqrt(2.0f);
    const float x = 1.0f / axisLength;
    const float y = 1.0f / axisLength;
    const float z = 0.0f;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const float oneMinusC = 1.0f - c;
    auto matrix = identity;
    matrix[0] = c + x * x * oneMinusC;
    matrix[1] = z * s + x * y * oneMinusC;
    matrix[2] = -y * s + x * z * oneMinusC;
    matrix[4] = -z * s + y * x * oneMinusC;
    matrix[5] = c + y * y * oneMinusC;
    matrix[6] = x * s + y * z * oneMinusC;
    matrix[8] = y * s + z * x * oneMinusC;
    matrix[9] = -x * s + z * y * oneMinusC;
    matrix[10] = c + z * z * oneMinusC;
    return matrix;
  };

  shared.worldTransform = multiply(
    multiply(makeRotationZ(-toRadians(state.zAngleDegrees + state.rotationThetaDegrees)),
             makeRotationY(-toRadians(state.rotationPhiDegrees))),
    makeAxisRotation(-toRadians(state.xyAngleDegrees)));
  return shared;
}

void drawControlPanel(bool& showPanel, ExperimentRendererState& state, const std::string& summary) {
  if (!showPanel) {
    return;
  }

  ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(420.0f, 430.0f), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("VXL Renderer Experiment", &showPanel)) {
    ImGui::End();
    return;
  }

  ImGui::TextWrapped("This experiment mirrors the reference project's vpl_renderer hardware path.");
  ImGui::Separator();
  ImGui::TextWrapped("%s", summary.c_str());
  ImGui::Separator();

  ImGui::SliderFloat("Scale", &state.scale, 0.2f, 6.0f);
  ImGui::SliderFloat("Extra light", &state.extraLight, -0.5f, 2.0f);
  ImGui::InputFloat3("Light direction", state.lightDirection.data());
  int normalTableSelection = static_cast<int>(state.normalTableSelection);
  const char* normalTableItems[] = {"Auto (from VXL)", "TS (Index 2)", "RA2 (Index 4)"};
  if (ImGui::Combo("Normal table", &normalTableSelection, normalTableItems, IM_ARRAYSIZE(normalTableItems))) {
    state.normalTableSelection = static_cast<VoxelNormalTableSelection>(normalTableSelection);
  }
  ImGui::TextWrapped("Auto 模式会读取 VXL 头里的 normals generation；手动模式可以强制切到 TS 或 RA2。");
  if (state.normalTableSelection == VoxelNormalTableSelection::AutoFromVxl) {
    ImGui::TextUnformatted("当前：自动模式（实际检测结果见上方 summary）");
  } else {
    const auto forcedKind = resolveVoxelNormalTableKind(state.normalTableSelection, VoxelNormalTableKind::Ra2Index4);
    ImGui::Text("当前：手动强制 %s", voxelNormalTableName(forcedKind));
  }

  ImGui::SeparatorText("World");
  ImGui::SliderFloat("Z angle", &state.zAngleDegrees, -180.0f, 180.0f);
  ImGui::SliderFloat("XY angle", &state.xyAngleDegrees, -180.0f, 180.0f);
  ImGui::SliderFloat("Rotation theta", &state.rotationThetaDegrees, -180.0f, 180.0f);
  ImGui::SliderFloat("Rotation phi", &state.rotationPhiDegrees, -180.0f, 180.0f);

  ImGui::SeparatorText("Turret");
  ImGui::SliderFloat("Turret rotation", &state.turretRotationDegrees, -180.0f, 180.0f);
  ImGui::SliderFloat("Turret offset", &state.turretOffsetPixels, -32.0f, 32.0f);

  float remap[3] = {
    static_cast<float>(state.remapColor.r) / 255.0f,
    static_cast<float>(state.remapColor.g) / 255.0f,
    static_cast<float>(state.remapColor.b) / 255.0f
  };
  if (ImGui::ColorEdit3("Remap color", remap)) {
    state.remapColor.r = static_cast<std::uint8_t>(remap[0] * 255.0f);
    state.remapColor.g = static_cast<std::uint8_t>(remap[1] * 255.0f);
    state.remapColor.b = static_cast<std::uint8_t>(remap[2] * 255.0f);
  }

  float background[3] = {
    static_cast<float>(state.backgroundColor.r) / 255.0f,
    static_cast<float>(state.backgroundColor.g) / 255.0f,
    static_cast<float>(state.backgroundColor.b) / 255.0f
  };
  if (ImGui::ColorEdit3("Background", background)) {
    state.backgroundColor.r = static_cast<std::uint8_t>(background[0] * 255.0f);
    state.backgroundColor.g = static_cast<std::uint8_t>(background[1] * 255.0f);
    state.backgroundColor.b = static_cast<std::uint8_t>(background[2] * 255.0f);
  }

  if (ImGui::Button("Reset")) {
    state = ExperimentRendererState{};
  }
  ImGui::SameLine();
  ImGui::TextUnformatted("Ctrl+D toggles this panel");

  ImGui::End();
}
}  // namespace

int main(int, char**) {
  SDL_Window* window = nullptr;
  SDL_GLContext context = nullptr;

  try {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
      throw std::runtime_error(std::string("Failed to initialize SDL: ") + SDL_GetError());
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    window = SDL_CreateWindow("vpl_renderer experiment",
                              SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED,
                              1280,
                              720,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
      throw std::runtime_error(std::string("Failed to create SDL window: ") + SDL_GetError());
    }

    context = SDL_GL_CreateContext(window);
    if (!context) {
      throw std::runtime_error(std::string("Failed to create OpenGL context: ") + SDL_GetError());
    }
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(1);
    ensureOpenGlLoaded();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, context);
    ImGui_ImplOpenGL3_Init("#version 330");

    {
      VplBoxRenderer renderer;
      renderer.initialize(window);
      const auto root = locateProjectRoot(experimentRoot());
      renderer.loadRhinoAssets(root / "assets" / "vehicles" / "rhino_tank",
                               VplFile::load(root / "assets" / "palettes" / "voxel" / "voxels.vpl"));
      renderer.setPalette(Palette::load(root / "assets" / "palettes" / "theater" / "unittem.pal"));

      ExperimentRendererState state;
      bool showPanel = true;
      bool running = true;

      while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event) != 0) {
          ImGui_ImplSDL2_ProcessEvent(&event);
          if (event.type == SDL_QUIT) {
            running = false;
          }
          if (event.type == SDL_KEYDOWN &&
              event.key.keysym.sym == SDLK_d &&
              (event.key.keysym.mod & KMOD_CTRL) != 0) {
            showPanel = !showPanel;
          }
        }

        int drawableWidth = 0;
        int drawableHeight = 0;
        SDL_GL_GetDrawableSize(window, &drawableWidth, &drawableHeight);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        drawControlPanel(showPanel, state, renderer.loadedAssetSummary());

        renderer.renderToScreen(toSharedState(state), drawableWidth, drawableHeight);

        ImGui::Render();
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glDisable(GL_BLEND);

        SDL_GL_SwapWindow(window);
      }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
  } catch (const std::exception& exception) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "vpl_renderer experiment", exception.what(), window);
    if (ImGui::GetCurrentContext() != nullptr) {
      ImGui_ImplOpenGL3_Shutdown();
      ImGui_ImplSDL2_Shutdown();
      ImGui::DestroyContext();
    }
    if (context) {
      SDL_GL_DeleteContext(context);
    }
    if (window) {
      SDL_DestroyWindow(window);
    }
    SDL_Quit();
    return 1;
  }
}
