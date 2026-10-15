# 01：窗口与等距网格

`main.cpp` 是本篇唯一的 C++ 源文件，GLSL 着色器放在 `shaders` 目录中。
`CMakeLists.txt` 将它构建为独立程序 `ra2_01_setup`。
运行后显示一个 1280×720 的窗口与 60×30 的菱形网格。可以调整窗口大小，按 Esc 或关闭窗口退出。

## 代码结构

窗口创建、OpenGL 初始化、绘制循环和窗口清理直接写在 `main` 中，只保留两个资源管理类：

- `Shader`：读取两个 GLSL 文件、编译、链接并设置 uniform，统一管理着色器与程序。
- `VertexArray`：管理 VAO 的创建、绑定和释放。

`Shader` 构造后调用一次 `initialize`，`VertexArray` 在构造时创建 VAO。
这两个类都禁止拷贝，并在析构时自动释放 GPU 资源。

阅读顺序是：创建窗口和 OpenGL 上下文 → 加载着色器 → 绑定 VAO → 事件与绘制循环。
正常结束或发生异常时，先离开 `try` 作用域，释放着色器和 VAO，再统一清理 OpenGL 上下文、窗口和 SDL。

两个着色器资源分别为：

- `shaders/grid.vert`：生成覆盖窗口的三角形。
- `shaders/grid.frag`：计算等距网格、交替底色与边线抗锯齿。

CMake 每次构建都会将这两个文件同步到可执行文件旁的 `shaders` 目录。
程序从该目录加载着色器，不依赖启动时的工作目录。修改 GLSL 后重新构建并运行即可，无需重新编译 C++。
如果复制程序到其他位置，需要一起复制 `shaders` 目录；手动编译时也需自行复制。

## 用 CLion 运行

如果已经打开仓库根目录 `ra2-replica`：

1. 等待 CMake 自动重新加载；如果没有更新，选择 **Tools → CMake → Reload CMake Project**。
2. 在右上角的运行目标列表中选择 `ra2_01_setup`。
3. 点击运行或调试。CLion 会先编译实验程序及其第三方依赖。

根工程默认启用 `RA2_BUILD_DEMOS=ON`，其中包含本实验。无需配置 Custom Build Application。

也可以直接打开本目录 `experiment/01-setup`，作为独立 CMake 工程运行同一个目标。
两种方式都可以使用 CLion 自带的 MinGW，无需先编译主游戏，也不需要手动复制 SDL2 DLL。

## 用命令行运行

需要 CMake 3.21 或更高版本，以及支持 C++20 的 C/C++ 编译器。
在 Visual Studio 的开发者终端、CLion 已配置好工具链的终端，或编译工具已加入 PATH 的终端中，
从仓库根目录执行：

```powershell
cmake -S experiment/01-setup -B build-01-setup
cmake --build build-01-setup --config Debug --parallel
cmake --build build-01-setup --config Debug --target run
```

最后一条命令会打开窗口；退出窗口后命令结束。`run` 会自动找到当前配置生成的程序，
无需区分 Ninja 与 Visual Studio 的可执行文件目录。

如果提示找不到 `cmake`，可直接采用上面的 CLion 方法，或将本机 CMake 的 `bin` 目录加入 PATH。
首次构建会编译 SDL2，后续只修改 `main.cpp` 时会增量构建。

## 依赖与隔离

- 独立构建时，SDL2、GLAD 默认从仓库 `third_party` 中读取源代码，在本实验自己的构建目录重新编译。
- 从根工程构建时，根 CMake 通过 `add_subdirectory` 注册实验目标，共用已有的 SDL2 第三方库。
- GLAD 静态链接，SDL2 优先静态链接；如果根工程只启用了动态 SDL2，会自动复制所需的 SDL2 DLL。
- MinGW 构建静态链接编译器运行库。
- 使用系统 OpenGL，显卡驱动需要支持 OpenGL 3.3 Core。
- 不链接游戏模块，不使用游戏素材；独立构建不加载根 CMake，也不需要主项目的构建产物。构建不需要联网。

如果把本实验复制到其他位置，可通过 CMake 变量指定依赖源码的位置：

```powershell
cmake -S . -B build -DSETUP_SDL_SOURCE_DIR="C:/deps/SDL" -DSETUP_GLAD_ROOT="C:/deps/glad"
```

GLAD 路径须包含此仓库所用的 `include/glad/glad.h` 和 `src/glad.cpp`，接口为 `gladLoadGLLoader`。
