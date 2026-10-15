# 当前代码结构

本文档描述 **当前仍在使用** 的主项目代码结构，方便后续继续扩功能时快速定位入口。

项目以原版可观察表现为还原目标，采用现代 C++ 和图形技术优化实现；
具体原则见 [架构说明](current-demo-architecture.md)。

## 构建模块与测试

项目要求 C++20，使用标准模式并关闭编译器语言扩展；CMake 不允许退回较低语言标准。
主程序、渲染实验和测试目标使用同一编译标准。
源码与普通字符串字面量统一使用 UTF-8（MSVC `/utf-8`，GCC/Clang 字符集选项）。
ImGui 的中文文本使用 UTF-8 编码的 `char` 字符串，兼容 C++20 的 `char8_t` 类型规则。

2026-09-12 速度与规则校准验证：MinGW GCC 13.1 Debug 三个程序构建通过，11 项 CTest 通过；
Release 核心模块 7 项测试通过；MSVC AddressSanitizer 核心全量构建及 7 项测试通过。
MSVC 本次验证范围为不依赖图形系统的核心模块。详见 [游戏速度实现](game-speed-implementation.md) 和 [规则校准](gameplay-calibration-2026-09-12.md)。

2026-09-15：修复血条显示宽度与基地车展开动画回归，Debug 构建及 12 项 CTest 通过，
新增两阵营建筑动画图形场景，见 [修复记录](building-presentation-fix-2026-09-15.md)。

2026-09-16：新增独立 `simulation/harvesting.*` 采矿系统与 `presentation/resource_view.*` 矿区表现。
支持配置驱动矿车、矿石/宝石、矿场队列、超时空返回与收入结算；详见 [采矿实现](harvesting-system-implementation.md)。

`CMakeLists.txt` 将代码组织成四个共享静态库：

- `ra2_core`：资源解码、配置读取、地图和寻路，不依赖 SDL/OpenGL。
- `ra2_simulation`：配置驱动的 World/实体组件、固定步和命令系统，私有依赖固定版本 EnTT，无图形依赖。
- `ra2_ui_backend`：GLAD、ImGui 及 SDL/OpenGL 后端，供上层共享。
- `ra2_engine`：表现适配、资源、场景和渲染实现，依赖上述三个库。

`ra2_power_demo` 与 `vxl_render_experiment` 链接共享实现，避免重复编译。
`shp_render_experiment` 保持独立的 SDL 实验程序。
项目警告选项通过 `ra2_project_options` 应用，不再全局传递给第三方源码。

测试位于 `tests/`，通过 CTest 运行：

```sh
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

仅验证核心模块时，可以不配置 SDL/OpenGL：

```sh
cmake -S . -B build-core -DRA2_BUILD_DEMOS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-core --config Release
ctest --test-dir build-core -C Release --output-on-failure
```

`RA2_ENABLE_GRAPHICS_TESTS=ON` 额外启用启动与模拟联动检查：主程序接受 `--smoke-test`，
创建隐藏窗口、加载实际资源、渲染三帧并正常清理退出。此项需要 OpenGL 3.3，默认关闭。
它不替代人工检查交互和画面。
`--simulation-smoke-test [截图.bmp]` 额外验证生产、部署、战斗与显示，支持保存本程序的回归截图。
`--game-speed-smoke-test` 验证实际主循环的有限档位、不限速以及切回有限档。
`--building-smoke-test [截图.bmp]` 验证两阵营基地车展开/收起，支持保存血条与动画关键帧。
`RA2_BUILD_BENCHMARKS=ON` 构建无图形基准工具及 `ra2_calibration_probe` 固定场景计时工具。

## 配置、模拟与表现边界

当前实现、操作方式、支持范围与性能数据见 [实现记录](unit-system-implementation.md)，
出车阶段、通道占用与验证见 [工厂内部出车](factory-egress-implementation.md)。

- `src/config/ini_document.*`：一次解析、字段覆盖、数值校验和来源定位。
- `src/config/definitions.*`：单位/武器/弹丸/弹头定义及资源映射，实际编译 409 个配置类型。
- `src/simulation/game_clock.*`：原版兼容速度档位、现实时间调度与游戏帧换算。
- `src/compat/rules_timing.h`：规则到位移、转向持续时间、生产段和动画间隔的兼容换算。
- `src/simulation/world.*`：唯一玩法状态，通用移动、战斗、生产和部署。
- `src/presentation/world_view.*`：只读模拟快照，转成既有 SHP/VXL 绘制结构。
- `assets/settings/replica.ini`：载具资源、建筑图层/帧限制、暂定兼容参数。
- `src/foundation.*`：模拟与显示共享占地几何。

## 入口层

- [app_main_ui.cpp](/F:/projects/ra2-replica/src/app_main_ui.cpp)
  - 主循环
  - SDL 初始化
  - 事件分发
  - 世界渲染与 UI 渲染编排

- [demo_app_support.h](/F:/projects/ra2-replica/src/demo_app_support.h)
- [demo_app_support.cpp](/F:/projects/ra2-replica/src/demo_app_support.cpp)
  - 项目资源根路径定位
  - OpenGL 上下文配置
  - 剧院调色盘装载
  - 建筑资源按剧院/国家色重载

- [demo_scene.h](/F:/projects/ra2-replica/src/demo_scene.h)
- [demo_scene.cpp](/F:/projects/ra2-replica/src/demo_scene.cpp)
  - 建筑放置辅助逻辑
  - 预览更新
  - 渲染队列生成
  - 侧栏状态更新

## UI 与调试层

- [imgui_debug_panel.h](/F:/projects/ra2-replica/src/imgui_debug_panel.h)
- [imgui_debug_panel.cpp](/F:/projects/ra2-replica/src/imgui_debug_panel.cpp)
  - ImGui 调试面板
  - Rhino 相关调试参数分组
  - 剧院 / 国家色切换

- [sidebar.h](/F:/projects/ra2-replica/src/sidebar.h)
- [sidebar.cpp](/F:/projects/ra2-replica/src/sidebar.cpp)
  - 右侧建造栏资源加载
  - 按钮布局
  - tab、按钮、图标点击逻辑

## 建筑系统

- [building_system.h](/F:/projects/ra2-replica/src/building_system.h)
- [building_system_runtime.cpp](/F:/projects/ra2-replica/src/building_system_runtime.cpp)
  - 建筑资源装载
  - 建造动画与完成态层
  - 图标到建筑 ID 的映射

- [art_ini.h](/F:/projects/ra2-replica/src/art_ini.h)
- [art_ini.cpp](/F:/projects/ra2-replica/src/art_ini.cpp)
  - `art.ini` 解析

## 2D 世界渲染

- [renderer2d.h](/F:/projects/ra2-replica/src/renderer2d.h)
- [renderer2d.cpp](/F:/projects/ra2-replica/src/renderer2d.cpp)
  - 主项目 2D 贴图渲染器
  - 世界层与 UI 层 pass

- [iso_world.h](/F:/projects/ra2-replica/src/iso_world.h)
- [iso_world.cpp](/F:/projects/ra2-replica/src/iso_world.cpp)
  - 等距坐标换算
  - 地面网格
  - 建筑绘制命令

- [map_grid.h](/F:/projects/ra2-replica/src/map_grid.h)
- [map_grid.cpp](/F:/projects/ra2-replica/src/map_grid.cpp)
  - 建筑占格
  - 放置判定
  - 当前是稀疏无限地图，宽高不是硬边界，负坐标仍有效

- [pathfinding.h](/F:/projects/ra2-replica/src/pathfinding.h)
- [pathfinding.cpp](/F:/projects/ra2-replica/src/pathfinding.cpp)
  - 独立的八方向 A* 寻路，不依赖渲染器
  - 已占用终点解析到附近空闲格
  - 默认每次最多扩展 8,192 个节点，避免不可达目标阻塞主线程
  - `nullopt` 表示未在预算内找到路径，空路径表示已在目标格

## 单位与武器逻辑

- 主程序通过 `simulation/World` 创建所有实体和执行命令；生命值、移动、冷却不由渲染修改。
- `rhino_unit.h/.cpp`、`weapon_system.h/.cpp` 提供既有绘制适配，旧模拟辅助函数暂留给实验和测试，主程序不再调用它们推进逻辑。
- `tests/simulation_tests.cpp` 验证实际运行的配置与模拟核心，包含 30/60/144 Hz、长帧、占用、战斗、生产和部署回归。
- `tests/gameplay_tests.cpp` 验证表现适配、动画时序及此前辅助函数。
- `tests/calibration_tests.cpp` 验证默认规则下的到达、转向和生产帧数；旧队列测试显式使用快速生产配置。
- `tools/calibration_probe.cpp` 输出固定场景 CSV；`tools/inspect_reference_timing.py` 只读提取已识别参考程序的常量。

## VXL 载具渲染

- [voxel_unit.h](/F:/projects/ra2-replica/src/voxel_unit.h)
- [voxel_unit.cpp](/F:/projects/ra2-replica/src/voxel_unit.cpp)
  - 把地图格坐标翻译成 Rhino 的场景锚点与深度范围

- [rhino_render_state.h](/F:/projects/ra2-replica/src/rhino_render_state.h)
- [rhino_render_state.cpp](/F:/projects/ra2-replica/src/rhino_render_state.cpp)
  - 从调试面板状态组装 Rhino 渲染状态

- [vpl_box_renderer.h](/F:/projects/ra2-replica/src/vpl_box_renderer.h)
- [vpl_box_renderer.cpp](/F:/projects/ra2-replica/src/vpl_box_renderer.cpp)
  - 共享 VXL 渲染器
  - 车体 / 炮塔 / 炮管部件组合
  - 法线表、VPL、palette、remap、阴影

- [vxl_file.h](/F:/projects/ra2-replica/src/vxl_file.h)
- [vxl_file.cpp](/F:/projects/ra2-replica/src/vxl_file.cpp)
  - `VXL/HVA` 解析

- [vpl_file.h](/F:/projects/ra2-replica/src/vpl_file.h)
- [vpl_file.cpp](/F:/projects/ra2-replica/src/vpl_file.cpp)
  - `voxels.vpl` 解析

- [voxel_normals.h](/F:/projects/ra2-replica/src/voxel_normals.h)
  - TS / RA2 法线表

## 调色盘与颜色

- [palette.h](/F:/projects/ra2-replica/src/palette.h)
- [palette.cpp](/F:/projects/ra2-replica/src/palette.cpp)
  - 调色盘读取
  - RGB565 量化
  - remap 颜色处理

- [rules_colors.h](/F:/projects/ra2-replica/src/rules_colors.h)
- [rules_colors.cpp](/F:/projects/ra2-replica/src/rules_colors.cpp)
  - `rules.ini [Colors]` 解析

## OpenGL 装载

- [gl_loader.h](/F:/projects/ra2-replica/src/gl_loader.h)
- [gl_loader.cpp](/F:/projects/ra2-replica/src/gl_loader.cpp)
  - `SDL + GLAD` 的统一入口

## 参考资料

- [ModEnc: Voxel](https://modenc.renegadeprojects.com/Voxel)
- [ModEnc: HVA](https://modenc.renegadeprojects.com/HVA)
- [ModEnc: VPL](https://modenc.renegadeprojects.com/VPL)
- [ModEnc: Normals](https://modenc.renegadeprojects.com/Normals)
