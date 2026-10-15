# 用现代 C++ 重建《红色警戒 2》（一）：技术选型、窗口搭建与等距网格

展开基地车、放下第一座电厂、让矿车开向矿区，这些操作构成了《红色警戒 2》一局游戏的开始。对开发者来说，每个操作背后都有一组值得拆解的问题：建筑应该放在哪里？鼠标位置对应哪个格子？载具如何显示和移动？资源与建造进度又如何更新？

这个系列将围绕这些问题，逐步实现一个能够运行、也便于阅读的红警 2 复刻项目。第一篇从窗口和网格开始：使用 C++、SDL2 与 OpenGL，创建一个可以调整大小的窗口，在其中绘制一张菱形网格底图。

本篇对应的代码位于仓库的 `experiment/01-setup`。应用代码只有一个 `main.cpp`，两个 GLSL 文件单独保存。读者需要了解基本的 C++ 语法、类和函数；涉及的图形学概念会随代码逐步介绍。

## 一、游戏介绍：我们希望还原什么

《命令与征服：红色警戒 2》是一款即时战略游戏。玩家指挥盟军或苏联部队，围绕基地建设、资源采集、部队生产和战斗展开对抗。Westwood 开发、EA 发行的这款作品，将不同阵营的建筑、单位与科技组织成了一套辨识度很高的玩法。[游戏官方商店介绍](https://store.steampowered.com/app/2229850/Command__Conquer_Red_Alert_2__Yuris_Revenge/)

本系列以《红色警戒 2》原版的实际游戏表现为参照。衡量还原程度的标准，是玩家能够观察和操作到的结果：建筑的占地与锚点、单位的朝向与速度、动画的节奏、矿车的往返过程，以及资金、电力和生产队列之间的关系。

我们希望用现代 C++ 组织这些系统，并利用 GPU 处理适合并行计算的绘制工作。在后续章节中，表现是否接近原版、结构是否方便扩展，以及性能是否有实际改善，都需要分别验证。

为了便于学习，系列示例放在 `experiment` 目录下，每一篇围绕一个明确的主题展开。接下来会依次涉及静态资源、体素模型、建造栏、步兵、载具、建筑、采矿与金钱，以及电力与血条。本篇先建立这些内容共同需要的显示环境和坐标基础。

这一阶段完成的画面是一张铺满窗口的菱形网格。它由着色器根据坐标计算生成，尚未包含保存地形、占地或单位的地图数据。后续加入游戏对象时，我们会继续使用这里建立的坐标关系。

## 二、技术选型：每个工具负责什么

本篇采用下面这组工具：

| 工具 | 在示例中的职责 |
|---|---|
| C++20 | 编写主程序，组织资源和控制流程 |
| SDL2 | 创建窗口和 OpenGL 上下文，接收窗口与键盘事件 |
| OpenGL 3.3 Core | 向 GPU 提交绘制命令 |
| GLSL 330 | 编写在 GPU 上执行的顶点、片元着色器 |
| GLAD | 加载当前 OpenGL 上下文提供的函数入口 |
| CMake | 配置依赖、生成程序并复制着色器资源 |

### C++ 负责组织流程，GLSL 负责计算画面

主程序使用 C++20 作为统一编译标准，但这一篇主要使用类、析构函数、异常和智能指针等基础能力。代码的重点是让资源归属和执行顺序容易看懂。

C++ 负责创建窗口、读取文件、处理输入，并告诉 GPU 要画什么。GLSL 则是另一种语言，编译后由 GPU 执行。这里有两个着色器：

- 顶点着色器决定三角形顶点的位置。
- 片元着色器根据屏幕位置计算网格颜色。

因此，同一个项目里会出现两套程序：运行在 CPU 上的 C++ 主程序，以及通过 OpenGL 使用的 GPU 着色器程序。

### SDL2 处理窗口，OpenGL 处理绘制

SDL 提供跨平台的窗口、输入、音频等底层能力。本篇只使用其中的窗口、事件和 OpenGL 上下文接口。[SDL 项目介绍](https://libsdl.org/index.php)

选择 SDL2，是为了直接使用仓库中已有的依赖，让示例和后续章节保持一致。本文的函数签名与返回值判断也按 SDL2 编写。

OpenGL 3.3 Core 已经包含本篇需要的可编程绘图能力。我们会明确经历创建着色器、链接程序、绑定顶点数组和提交绘制的过程。GLSL 文件开头的 `#version 330 core` 与这套接口对应。[Khronos OpenGL 与 GLSL 规范入口](https://registry.khronos.org/OpenGL/index_gl.php)

OpenGL 的 API 本身具有 C 风格。示例保留了这些调用，同时用两个小类管理着色器和 VAO 的生命周期，让资源创建与释放的关系更加清楚。

### GLAD 让 OpenGL 函数可以被调用

创建窗口并不意味着所有 OpenGL 函数都已经准备好。程序还需要取得当前上下文支持的函数入口，GLAD 负责这部分加载工作。[GLAD 项目说明](https://github.com/Dav1dde/glad)

本仓库使用 `glad/glad.h` 和 `gladLoadGLLoader` 风格的接口。创建上下文后，程序只需要加载一次：

~~~cpp
if (!gladLoadGLLoader(SDL_GL_GetProcAddress))
  throw std::runtime_error("Failed to load OpenGL functions");
~~~

之后就可以直接调用 `glCreateShader`、`glUseProgram` 和 `glDrawArrays` 等函数。

### CMake 保持示例可以独立运行

`01-setup` 有自己的 `CMakeLists.txt`。从这个目录单独配置时，它会构建所需的 SDL2 和 GLAD；从仓库根工程配置时，则复用已经声明的 SDL2 第三方目标。

两种入口生成的应用目标都叫 `ra2_01_setup`。它不链接主游戏模块，也不需要原版游戏素材。根工程通过 `add_subdirectory` 注册它，方便 CLion 在打开整个仓库时直接识别运行目标。

构建过程还会把两个 GLSL 文件复制到可执行文件旁边。只修改着色器时，重新构建即可同步文件，C++ 源码不需要因此重新编译。

## 三、框架搭建：从目录到一个能够持续绘制的窗口

### 先运行示例，再阅读代码

目录结构如下：

~~~text
experiment/01-setup/
├── CMakeLists.txt
├── README.md
├── main.cpp
└── shaders/
    ├── grid.vert
    └── grid.frag
~~~

构建需要 CMake 3.21 或更高版本、支持 C++20 的 C/C++ 编译器，以及支持 OpenGL 3.3 Core 的图形环境。SDL2 与 GLAD 使用仓库 `third_party` 中附带的源码。

如果 CLion 已经打开仓库根目录，重新加载 CMake，选择 `ra2_01_setup`，然后运行即可。根工程默认启用 `RA2_BUILD_DEMOS=ON`，该选项包含本实验。也可以把 `experiment/01-setup` 直接打开为一个独立工程。

在已经配置好 CMake 和编译器的终端中，从仓库根目录执行：

~~~powershell
cmake -S experiment/01-setup -B build-01-setup
cmake --build build-01-setup --config Debug --parallel
cmake --build build-01-setup --config Debug --target run
~~~

最后一条命令会打开窗口，关闭窗口后命令结束。`run` 目标会找到当前构建配置对应的可执行文件，避免手动区分不同生成器的输出目录。

如果终端提示找不到 `cmake`，可以使用 CLion 配置的构建环境，或者把 CMake 和编译器所在目录加入 `PATH`。

### 让主流程集中在 main 中

窗口创建只使用一次，因此直接放在 `main` 中。主函数可以按四个步骤阅读：

~~~text
创建窗口、上下文并加载 OpenGL
    ↓
准备着色器与 VAO
    ↓
处理事件并持续绘制
    ↓
释放 GPU 资源、上下文和窗口
~~~

窗口和上下文的变量定义在 `try` 外部，便于正常结束和发生异常时共用清理逻辑：

~~~cpp
SDL_Window* window = nullptr;
SDL_GLContext context = nullptr;
int exitCode = 0;
~~~

初始化首先启动 SDL 的视频子系统，并声明需要的 OpenGL 版本：

~~~cpp
SDL_SetMainReady();
if (SDL_Init(SDL_INIT_VIDEO) != 0)
  throw std::runtime_error(SDL_GetError());

if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) != 0 ||
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3) != 0 ||
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) != 0 ||
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) != 0)
  throw std::runtime_error(SDL_GetError());
~~~

文件开头的 `SDL_MAIN_HANDLED` 表示使用普通 C++ 入口。与之配套的 `SDL_SetMainReady()` 在 `SDL_Init()` 前调用。

接着创建一个初始大小为 1280×720 的窗口：

~~~cpp
window = SDL_CreateWindow("RA2 Replica | 01 - Setup | Esc to exit",
  SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
  SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
if (!window) throw std::runtime_error(SDL_GetError());

context = SDL_GL_CreateContext(window);
if (!context) throw std::runtime_error(SDL_GetError());
~~~

三个窗口标志分别表示支持 OpenGL、允许调整大小，以及允许高 DPI 绘图区域。

OpenGL 上下文可以理解为这次绘图使用的工作环境，其中包含当前使用的程序、绑定的资源和绘制状态。上下文创建成功后，再加载 GLAD，随后才能创建着色器和 VAO。

### 用两个类管理 GPU 资源

示例只保留 `Shader` 和 `VertexArray` 两个资源管理类。

`Shader` 把顶点着色器、片元着色器及链接后的程序作为一组资源管理。初始化过程是：

~~~text
读取 grid.vert → 编译顶点着色器 ─┐
                               ├→ 链接成一个着色器程序
读取 grid.frag → 编译片元着色器 ─┘
~~~

这里的“编译”发生在程序启动时：OpenGL 接收 GLSL 文本，检查并编译它们。编译成功后，再把两个阶段链接起来。编译和链接失败时，示例会读取日志并输出错误，便于定位 GLSL 中的问题。

`GLuint` 类型的成员保存 OpenGL 分配的资源编号。它们用于引用 GPU 资源，不是着色器源码，也不是顶点坐标。

主程序中的使用方式是：

~~~cpp
Shader shader;
shader.initialize("grid.vert", "grid.frag");
shader.use();
shader.setVec2("uTileSize", 60, 30);

VertexArray vao;
vao.bind();
~~~

`use()` 选中当前绘制使用的程序，`setVec2()` 设置该程序中的二维向量参数。

VAO，即顶点数组对象，通常记录顶点属性应当怎样从缓冲区读取。本篇直接在顶点着色器中写出三个顶点的位置，因此没有创建顶点缓冲区；Core 模式下绘制仍然需要绑定一个 VAO。

两个类都禁止拷贝，并在析构时释放自己管理的资源。`Shader` 先完成默认构造，再执行一次 `initialize()`，这样即使初始化中途抛出异常，已经创建的着色器也会由析构函数清理。

### 资源路径相对于程序所在目录

程序通过 `SDL_GetBasePath()` 获取可执行文件所在目录，再读取其中的 `shaders/grid.vert` 和 `shaders/grid.frag`。

这个细节让运行方式更自由：从 CLion、仓库根目录或其他终端目录启动，程序都能找到随可执行文件一起放置的着色器。

读取文件时使用 `std::unique_ptr` 搭配 `SDL_free`，让 SDL 分配的内存离开作用域后自动释放。读出的文本交给 OpenGL 编译后，这段临时内存就不再需要。

如果把程序复制到其他位置，需要同时复制旁边的 `shaders` 目录。示例没有实现运行时热重载；修改 GLSL 后，重新构建并启动程序即可看到结果。

### 每帧处理事件、绘制并显示

窗口创建完成后，程序进入循环。每次循环先处理关闭窗口和 Esc 键事件：

~~~cpp
SDL_Event event{};
while (SDL_PollEvent(&event)) {
  if (event.type == SDL_QUIT ||
      (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE))
    running = false;
}
if (!running) break;
~~~

窗口最小化或绘图区域无效时，程序短暂等待，避免持续空转。其余情况下，获取当前尺寸并绘制：

~~~cpp
int width = 0, height = 0, pixelWidth = 0, pixelHeight = 0;
SDL_GetWindowSize(window, &width, &height);
SDL_GL_GetDrawableSize(window, &pixelWidth, &pixelHeight);

glViewport(0, 0, pixelWidth, pixelHeight);
shader.setVec2("uWindowSize", static_cast<float>(width), static_cast<float>(height));
shader.setVec2("uDrawableSize", static_cast<float>(pixelWidth), static_cast<float>(pixelHeight));
glDrawArrays(GL_TRIANGLES, 0, 3);
SDL_GL_SwapWindow(window);
~~~

这是绘制部分的节选，完整循环还保留了尺寸有效性检查。

`width`、`height` 是窗口的逻辑尺寸；`pixelWidth`、`pixelHeight` 是实际绘图区域的像素尺寸。在支持高 DPI 的环境中，二者可能不同，所以 `glViewport` 使用后者，而着色器同时接收两套尺寸。[SDL 绘图区域尺寸说明](https://wiki.libsdl.org/SDL2/SDL_GL_GetDrawableSize)

程序在后台缓冲区完成绘制，再通过 `SDL_GL_SwapWindow()` 呈现这一帧。这样可以在准备下一帧时继续显示已有画面。[SDL 双缓冲窗口说明](https://wiki.libsdl.org/SDL2/SDL_GL_SwapWindow)

启动时会尝试启用垂直同步；如果驱动不支持，就在每次绘制后等待 16 毫秒，降低空场景的忙等开销。这只是本篇的简单显示节奏控制。后续加入单位、采矿等逻辑时，还需要单独设计游戏逻辑的更新时间。

### 按正确顺序退出

`shader` 和 `vao` 都定义在 `try` 作用域中。正常结束循环，或者初始化、绘制准备过程中抛出异常，都会先离开这个作用域。

随后再执行：

~~~cpp
if (context) SDL_GL_DeleteContext(context);
if (window) SDL_DestroyWindow(window);
SDL_Quit();
return exitCode;
~~~

这个顺序保证了 GPU 资源的析构函数执行时，OpenGL 上下文仍然存在。异常处理只负责输出错误、设置退出码，窗口和 SDL 的清理统一放在最后。

## 四、网格渲染：一个三角形如何生成整张底图

### 先看 CPU 与 GPU 的分工

网格绘制使用一个覆盖窗口的大三角形。C++ 负责提交这个三角形和必要参数，GPU 根据像素位置计算网格颜色。

~~~mermaid
flowchart LR
    A["C++：提交绘制命令"] --> B["顶点着色器：输出三个顶点"]
    B --> C["裁剪和光栅化：确定覆盖区域"]
    C --> D["片元着色器：计算格子与边线颜色"]
    D --> E["后台缓冲区"]
    E --> F["交换缓冲区，显示画面"]
~~~

`glDrawArrays(GL_TRIANGLES, 0, 3)` 表示从编号 0 开始，使用三个顶点组成三角形。网格中有多少个可见格子，并不会改变这次调用的顶点数量。

### 顶点着色器只负责覆盖窗口

`grid.vert` 的完整逻辑很短：

~~~glsl
#version 330 core

const vec2 positions[3] = vec2[3](
  vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0)
);

void main() {
  gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
~~~

这次绘制中，`gl_VertexID` 对应 0、1、2，着色器据此选出三个顶点的位置。

`gl_Position` 使用裁剪空间坐标。这里令最后一个分量 `w` 为 1，可见区域对应横纵坐标的 `[-1, 1]`。三个顶点构成的大三角形覆盖了这个区域，超出部分由图形管线裁剪。

经过光栅化，覆盖范围内会产生待着色的片元。初次理解时，可以把下一阶段看成“为覆盖区域中的像素计算颜色”。具体的网格形状都在片元着色器中生成。

### 第一步：取得当前像素的屏幕位置

片元着色器接收三个参数：

~~~glsl
uniform vec2 uWindowSize;
uniform vec2 uDrawableSize;
uniform vec2 uTileSize;
~~~

`uniform` 表示由主程序传入、一次绘制中各个着色器调用共享的参数。`uTileSize` 在启动时设为 `(60, 30)`，两套窗口尺寸则每帧更新，以响应窗口大小变化。

GLSL 提供的 `gl_FragCoord.xy` 是当前片元在绘图区域中的位置。先把它换算为窗口逻辑坐标：

~~~glsl
vec2 screen = gl_FragCoord.xy * uWindowSize / uDrawableSize;
screen.y = uWindowSize.y - screen.y;
vec2 origin = uWindowSize * 0.5;
~~~

第一行处理高 DPI 下的尺寸比例。第二行把 OpenGL 默认从左下角向上增长的 Y 坐标，转换成从左上角向下增长的屏幕坐标。

`origin` 是地图坐标原点在屏幕上的位置。本篇始终把它放在窗口中心：当窗口为 1280×720 时，原点为 `(640, 360)`。

### 第二步：建立地图坐标与屏幕坐标的关系

本文沿用“等距网格”的常见叫法，具体使用宽高比为 2:1 的菱形投影。每个格子宽 `W = 60`、高 `H = 30`，整数地图坐标表示格子的中心。

地图上的两个方向，在屏幕中分别是：

~~~text
地图 x 增加 1：向右 W/2，向下 H/2
地图 y 增加 1：向左 W/2，向下 H/2
~~~

由此得到从地图到屏幕的正向变换：

~~~text
screenX = originX + (mapX - mapY) × W/2
screenY = originY + (mapX + mapY) × H/2
~~~

例如，地图格子 `(1, 0)` 的中心，会出现在窗口中心右侧 30、下方 15 的位置；格子 `(0, 1)` 则在左侧 30、下方 15 的位置。连续的地图格子就形成了屏幕上的菱形排列。

绘制建筑或单位时，我们通常会从地图坐标计算屏幕位置。而现在片元着色器已经知道屏幕位置，需要反过来求它对应的地图坐标。

设：

~~~text
a = (screenX - originX) / (W/2)
b = (screenY - originY) / (H/2)
~~~

代入正向变换后，有 `a = mapX - mapY`、`b = mapX + mapY`。两式相加、相减即可得到：

~~~text
mapX = (a + b) / 2
mapY = (b - a) / 2
~~~

代码正是这个逆变换：

~~~glsl
vec2 projected = (screen - origin) / (uTileSize * 0.5);
vec2 logical = vec2(
  projected.y + projected.x,
  projected.y - projected.x
) * 0.5;
~~~

以屏幕位置 `(670, 375)` 为例，它相对于原点 `(640, 360)` 的偏移是 `(30, 15)`。归一化后 `a = 1`、`b = 1`，得到地图坐标 `(1, 0)`。

屏幕上的任意位置都可以用同样的方式换算。计算结果通常带有小数，代表它处于某个格子内部的具体位置。

### 第三步：确定所属格子并设置底色

因为整数坐标约定为格子中心，所以格子边界位于半整数处。例如，中心为 `mapX = 1` 的格子，在这一维上的范围从 `0.5` 延伸到 `1.5`。

因此，所属格子可以这样求：

~~~glsl
vec2 cell = floor(logical + 0.5);
~~~

例如，连续地图坐标 `(1.2, -0.1)` 会归入格子 `(1, 0)`。

为了让网格更容易辨认，两个相邻格子使用不同的底色：

~~~glsl
float alternate = mod(cell.x + cell.y, 2.0);
vec3 ground = mix(
  vec3(0.41, 0.42, 0.45),
  vec3(0.46, 0.47, 0.50),
  alternate
);
~~~

格子坐标之和对 2 取模，结果交替为 0 或 1。`mix` 根据这个值在两种颜色之间选择，于是形成类似棋盘的排列。

### 第四步：计算边线并平滑过渡

有了底色，还需要让菱形边界更加清楚。由于边界位于半整数处，可以利用地图坐标的小数部分求出到边界的距离：

~~~glsl
vec2 distanceToEdge = abs(fract(logical) - 0.5);
~~~

例如，某一维的小数部分接近 `0.5` 时，说明当前像素靠近该方向的格子边界。两个方向中，只要有一个方向靠近边界，就应该画出线条。

这个距离仍然使用地图坐标单位。为了让线条在屏幕上保持适当的宽度，需要结合坐标在相邻片元之间的变化量：

~~~glsl
vec2 pixelsToEdge = distanceToEdge / max(fwidth(logical), vec2(0.00001));
float line = 1.0 - smoothstep(
  0.0, 1.0,
  min(pixelsToEdge.x, pixelsToEdge.y)
);
~~~

`fwidth(logical)` 估计地图坐标在屏幕邻域内的变化幅度。用它对距离进行归一化，可以按大约一个像素邻域的尺度平滑线条边缘；`max` 中的小数用于避免除以零。

`smoothstep` 让边线强度逐渐过渡，减少斜线边缘生硬的阶梯感。最终把边线颜色混合到底色中：

~~~glsl
fragmentColor = vec4(
  mix(ground, vec3(0.78, 0.80, 0.84), line * 0.55),
  1.0
);
~~~

这里的最后一个分量是透明度，设为 1 表示不透明。每个片元独立执行这些计算，整张网格便出现在窗口中。

### 这张底图为后续内容提供什么

本篇建立了两条可以继续复用的路径：

- 地图坐标到屏幕坐标：决定建筑、单位等对象画在哪里。
- 屏幕坐标到地图坐标：判断鼠标指向哪个格子。

当前的一次绘制调用负责生成网格底图。它省去了为每个可见格子准备顶点的工作，片元计算量则仍然随绘图区域的像素数量增长。后续加入建筑、载具和 UI 时，会有相应的资源与绘制流程。

网格公式也没有保存每个格子的内容。地形类型、建筑占地、资源矿量、单位位置等状态，需要在后续章节建立独立的数据结构，再通过这里的坐标关系显示出来。

接下来，我们会在这张网格上放置第一份静态游戏资源，继续解决图像读取、颜色还原和定位对齐，让窗口中的几何底图逐步变成能够承载游戏对象的场景。
