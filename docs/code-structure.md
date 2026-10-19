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
`ra2_01_setup` 和 `ra2_02_static_resource_rendering` 也是默认构建目标，
只复用第三方依赖；后者额外使用 GLM。两个教程目录均可单独配置构建。
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
`--sidebar-smoke-test [截图.bmp]` 验证两种侧栏皮肤的四个分页都同时列出盟军、苏军条目，检查所有滚动位置的实际点击、原版图标，以及共用工程师 / 基地车图标的阵营身份；传入截图参数后在同目录生成 `sidebar-allied.bmp`、`sidebar-soviet.bmp`。
`--building-smoke-test [截图.bmp]` 验证两阵营基地车展开/收起，支持保存血条与动画关键帧。
`--support-infantry-smoke-test [截图.bmp]` 验证双方工程师生产、进入建筑占领 / 修复、狙击手射击及原版 SHP 显示，运行 320 帧。
`--air-defense-smoke-test [截图.bmp]` 验证防空兵空地武器切换、火箭飞行兵起飞 / 飞行 / 悬停 / 开火 / 坠落、实际鼠标选取和原版血条，运行 420 帧；飞行几何位于 `src/simulation/flight.*`，逻辑回归为 `air_defense_regressions`。
`--special-vehicles-smoke-test [截图.bmp]` 验证坦克杀手命中爆炸、光棱光束 / 分束 / 四阶段炮塔、幻影伪装 / 显形 / 实际选取，并切换三种剧场，运行 360 帧。树形和光束位于 `src/presentation/special_vehicle_view.*`，逻辑回归为 `special_vehicle_regressions`。该图形测试还检查幻影单一 RTNK 车体的八方向 / 三剧场像素、伪装树的序列偏移、地形调色板和阴影，并连续读回 66 游戏帧的己方 / 敌方外观、开火当帧的原版枪口效果及 15 帧显形边界；有截图参数时输出 `mirage-body-atlas.bmp`、`mirage-timing-atlas.bmp`、`mirage-timing-sequence.bmp`。
`--transport-smoke-test [截图.bmp]` 验证登车、实际鼠标 / U 键卸载、驻军火力、IFV 原版炮塔切换、维修及导弹，运行 700 帧。
`--demolition-smoke-test [截图.bmp]` 验证恐怖分子、自爆卡车及自爆 IFV，运行 220 帧；检查原版 TRST 步行 / 站立 / 血条、TRUCKA 车体八方向 / 三剧场像素、MININUKE / DEMTEXP 帧、实际鼠标选取和残留辐射。传入截图参数后额外生成 `demolition-native-units.bmp`、`demolition-native-explosions.bmp` 与场景关键帧。
`--ivan-apocalypse-smoke-test [截图.bmp]` 验证疯狂伊文、双方工程师拆弹、伊文 IFV 和天启，运行 600 帧；检查八方向 / 三剧场原版素材、mousepal 炸弹标记、六阶段 / 8 帧闪烁与 BombSight、实际 SDL 点击拆弹、移动目标、地面双发 / 对空导弹和免费自疗。传入截图参数后额外生成单位、13 帧图标、爆炸 / 炮口图集与场景关键帧。
`--commando-smoke-test [截图.bmp]` 验证海豹 / 雪地海豹、谭雅、间谍、超时空兵的原版三剧场 / 八方向主体与水中姿态、WarpIn / WarpOut / WarpAway、蓝色时间光束、冻结和观察方不同的伪装；另保存单位 / 特效图集和时间抹除场景。逻辑回归 `commando_regressions` 覆盖 C4、游泳、潜入、训练加成、传送 / 恢复和独立 Temporal 通道。权威状态在 `simulation/world.cpp` 的 C4Charge / SpyDisguise / ChronoRecovery / TemporalVictim / TemporalChannel / Veteran 及玩家间谍收益中，均参与重放哈希；渲染适配只读快照。
`--naval-smoke-test [截图.bmp]` 验证首批海军的原版 VXL / HVA、MEDUSA 方向帧、水花 / 尾流 / 气泡、三剧场原生 TMP 海岸、潜艇可见性、实际鼠标坦克登船与深水 / 返岸卸载、船厂生产和空海武器，运行 480 帧。传入路径后输出原版素材图集和场景关键帧。逻辑回归 `naval_regressions` 覆盖地形放置 / 水域移动 / 出口、容量、探测、鱼雷撞岸及发射者死亡、全向防空、维修和重放。`presentation/naval_view.*` 管理三剧场地块及显式演示海岸初始化；`TmpFile::decodeCells` 读取原生多格 52 字节格头，保留旧教学格式的 `decodeSingleTile`。World 的 Submerge / ShipyardRepair、原有 Cargo / Factory / Shot 持有权威状态；显示适配只读快照。正常模式通过“查看海岸 / 返回基地”切换视角，船厂只允许放入水域。
`--naval-special-smoke-test [截图.bmp]` 检查剩余五类海军及 ASW / HORNET / DMISL 的八方向 / 三剧场原版素材、空甲板、海洋生物动作 / 血条、SQDG 的 AltPalette、声波，以及 900 帧舰载机 / 双导弹 / 缠绕与声波解救、实际 SDL 选取。逻辑回归 `naval_special_regressions` 覆盖生产 / 前置 / 费用、出击返航、损失补充、拦截、最小射程 / 强制地面攻击、缠绕和隐身 / 声呐、重放；`simulation/naval_aircraft.*` 提供整数飞行几何，World 的 NavalSpawner / NavalAircraft / SonicWave 及原有 leap::State / Infested 保存状态，`missile.*` 分别读取 V3Rocket* / DMisl* 配置。详见 [第二批海军](infantry-system-implementation.md#第二批海军舰载机无畏导弹与海洋生物2026-10-18)。
`RA2_BUILD_BENCHMARKS=ON` 构建无图形基准工具及 `ra2_calibration_probe` 固定场景计时工具。

### macOS / Apple Silicon 构建

依赖源码已包含在 `third_party/`，当前 CMake 直接编译仓库内 SDL2，
不需要先安装 vcpkg 或单独下载 SDL2。需要 C++20 编译器、macOS SDK、CMake 3.21+；
以下命令使用 Ninja 和 CLion 自带的 CMake，均从仓库根目录执行。

如果终端提示 `cmake: command not found`，但已安装 Apple Silicon 版 CLion，可在当前终端设置：

```sh
export PATH="/Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin:/Applications/CLion.app/Contents/bin/ninja/mac/aarch64:$PATH"
```

本机曾出现 `xcrun --show-sdk-path` 选中 `MacOSX27.0.sdk`，
链接器却不认识其中的 `arm64e.x1`，连 CMake 的空程序检测都失败。
已验证安装中的 `MacOSX.sdk` 链接指向兼容的 `MacOSX26.5.sdk`。
遇到同样错误时，先确认 `ls -l /Library/Developer/CommandLineTools/SDKs`，
再显式指定兼容 SDK；不要修改 SDK 文件，也不要跳过编译器检测。
`SDKROOT` 同时供 SDL 的 Objective-C 编译器探测使用：

```sh
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
cmake -S . -B cmake-build-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_OSX_SYSROOT="$SDKROOT" \
  -DRA2_ENABLE_GRAPHICS_TESTS=ON
cmake --build cmake-build-debug --parallel 8
ctest --test-dir cmake-build-debug --output-on-failure
./cmake-build-debug/ra2_power_demo
```

Release 使用 `cmake-build-release` 和 `-DCMAKE_BUILD_TYPE=Release`；
可加 `-DRA2_BUILD_BENCHMARKS=ON` 构建基准工具。
CLion 的 Debug、Release CMake Profile 可设置同样的 `CMAKE_OSX_SYSROOT` 选项
及 `SDKROOT` 环境变量，生成目录分别使用上述两个目录。

GLM 的来源信息保存在 `third_party/glm/VERSION.txt`。
不要改回 `VERSION`：在不区分大小写的文件系统上，该文件会遮蔽 C++ 标准头 `<version>`，
导致 libc++ 把 `GLM 1.0.3` 当成 C++ 代码解析，阻塞第二个教程的构建。

2026-10-16 验证：AppleClang 21.0.0、CMake 4.3.1、macOS 26.5 SDK、arm64，
Debug / Release 均完成全部默认目标构建，Release 另含两个基准工具；
两种配置的 20 项 CTest 均通过（13 项非图形回归与 7 项图形冒烟测试）。
仍有 SDL/ImGui 的兼容性警告以及侧栏未使用函数警告，不影响本次构建。
图形冒烟测试不覆盖完整人工交互与画面验收。

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

磁暴步兵 / 磁能坦克复用现有 SHP、VXL、确定性电弧和武器选择；CoilCharger 在 World 内维护友军充能关系，支持数量决定线圈供电与副武器。ElectricAssault 不进入伤害结算，InfDeath=5 产生独立 ELECTRO 事件。EffectAnimationDef 统一解析动画循环，world_view 读取同一时间线。新增 `tesla_unit_regressions`、`demo_tesla_units`（`--tesla-units-smoke-test [截图.bmp]`），详见 [步兵接入记录](infantry-system-implementation.md#磁暴步兵与磁能坦克2026-10-18)。

`simulation/radiation.*` 管理持续辐射区、等级与灯光衰减；`simulation/missile.*` 管理 V3 发射准备、整数飞行曲线和俯仰。World 负责部署释放、区域伤害、发射车 / 火箭关系、补弹、拦截和快照。`radiation_v3_regressions`、`demo_radiation_v3`（`--radiation-v3-smoke-test [截图.bmp]`）覆盖本批实现，详见 [步兵与 V3 接入记录](infantry-system-implementation.md#辐射工兵与-v3-火箭车2026-10-18)。

`simulation/leap.*` 维护警犬 / 恐怖机器人扑击的整数轨迹与阶段。World 负责目标过滤、LimboLaunch 释放占位、寄生双向引用、持续破坏、受击抑制、维修清除、宿主死亡后的落地和重新占位。`spriteVehicle` 只描述 SHP 动画，DRON 继续使用 Mobile / 载具工厂 / 整格占位；`infantry_view` 共享单位图集、原版阴影与血条绘制。`dog_drone_regressions`、`demo_dog_drone`（`--dog-drone-smoke-test [截图.bmp]`）覆盖本批实现，详见 [警犬与恐怖机器人](infantry-system-implementation.md#双方警犬与恐怖机器人2026-10-18)。

步兵伤害命中与路径子格独立：World 以三维距离到步兵命中体积边缘计算弹头扩散 / 衰减，默认兼容半径由 `Simulation.InfantryHitRadiusLeptons` 控制；实体瞄准保留子格坐标，普通炮弹保持发射时落点。`projectile_impact_regressions` 覆盖窄范围炮弹、三子格、移动躲避和高度 / 墙体，见 [弹着点修正记录](infantry-system-implementation.md#炮弹落点与步兵命中体积2026-10-18)。

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

四个建造分页统一交错列出盟军和苏军条目，侧栏皮肤只决定界面贴图 / 调色板。每个图标及点击结果保留自己的阵营，双方共用的工程师、基地车图标分别映射到 ENGINEER / SENGINEER、AMCV / SMCV。所有分页的可用状态、阵营、价格及建筑电力提示统一由 `updateSidebarAvailability` 更新；未实现单位继续置灰。侧栏生产选择通过 `sidebarFactoryMatches` 按工厂原版 AIBasePlanningSide 选择所属阵营，再检查原有类别 / 海陆匹配。原版工厂的 Owner 包含双方可拥有国家，不可用作工厂阵营标识。`gameplay_regressions` 同时放置两种工厂，验证双方工程师 / 基地车的点击映射、工厂选择及生产出厂。

此次更新后 Debug / Release 各 31/31 测试通过，含 `demo_mixed_sidebar` 的真实 OpenGL 与滚动点击检查。两种皮肤的四类列表和首尾页截图、全量测试日志保存在 `build/mixed-sidebar/`，`preview.png` 为步兵 / 载具分页对比。

## 建筑系统

- [building_system.h](/F:/projects/ra2-replica/src/building_system.h)
- [building_system_runtime.cpp](/F:/projects/ra2-replica/src/building_system_runtime.cpp)
  - 建筑资源装载
  - 建造动画与完成态层
  - 图标到建筑 ID 的映射

- [art_ini.h](/F:/projects/ra2-replica/src/art_ini.h)
- [art_ini.cpp](/F:/projects/ra2-replica/src/art_ini.cpp)
  - `art.ini` 解析

防御页的 16 个入口统一映射到 rules.ini 的逻辑建筑类型；ArtIni 再读取 Image 找到 GAPRIS / NATSLA / GAWETH / GASPST 等美术定义。普通防御塔共用武器、供电和蓄能模拟，四种建筑 VXL 炮塔共用表现入口。围墙连接与发射起始帧均来自 World 快照。实现范围和待补机制见 [防御建筑补齐记录](building-presentation-fix-2026-09-15.md#2026-10-17防御建筑栏补齐)，专项测试为 `defense_regressions` 与 `demo_defense`。

## 2D 世界渲染

`ToggleRepair / SellBuilding` 命令在 `simulation/world.cpp` 推进维修费用、回血和售卖结算，状态通过 `world_view` 传给建筑表现；售卖共用逆序建造动画。`presentation/building_action_view.*` 管理原版维修标记和维修 / 售卖鼠标光标。`building_service_regressions` 与 `demo_building_actions` 分别覆盖模拟边界和实际侧栏交互。

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
- `tests/demolition_tests.cpp` 验证原版 Suicide / Explodes / DeathWeapon、实际坐标范围伤害、稳定 ID 连锁结算、乘载销毁、IFV 自爆模式及国家 / 雷达 / 生产出口；死亡在清除乘员和实体前统一结算，隐藏乘员不会额外复制自爆。
- `tests/ivan_apocalypse_tests.cpp` 验证附着炸弹独立所有者 / 计时、移动 / 目标死亡 / 售卖 / 载具部署与运输边界、拆弹 / 取消、伊文 IFV、天启武器和全局自疗节拍；`BombSnapshot` 仅供显示，权威 `AttachedBomb` 随目标实体保存并纳入重放哈希。
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

## 独立空军（2026-10-19）

- `src/simulation/aircraft.h/.cpp`：固定翼 / Jumpjet 载具的整数飞行、连续转弯、降落收敛；与飞行兵、舰载子机的状态隔离。
- `config/definitions.*`：AircraftDef / AirportDef，原版四机位偏移、Ammo / ReloadRate、FlightLevel、Jumpjet 参数、Vertical / Acceleration；机场生产 AircraftType，SHAD / ZEP 仍按 VehicleType 从战车工厂顶部离场。
- `simulation/world.cpp`：Pilot 权威任务、机位预定、齐射扣弹 / 返航补给、转场 / 机场损失、空运落地占位和乘员、AG / AA 域切换、坠落体素；均进入状态哈希。
- `presentation/world_view.cpp`、`rhino_unit.*`、`voxel_unit.cpp`、`vpl_box_renderer.*`：空中选择与高度、地面阴影、机场停靠遮挡、原版 HVA 帧和 ShadowIndex。弹药 PIPS2 来自 `presentation/infantry_view.*` 的公共单位覆盖层。
- `tests/aircraft_tests.cpp` / `aircraft_regressions`：生产 / 四机位 / 弹药 / 运输 / 防空 / 机场 / 工厂顶部出厂 / 重放回归。
- `ra2_power_demo --aircraft-smoke-test [截图.bmp]` / `demo_aircraft`：1200 帧真实 SDL / OpenGL 和八方向、三剧场、两帧原版旋翼图集；检查真实点击和完整空军任务，截图写入指定目录。

`--sandbox-smoke-test` / `demo_sandbox` 检查完整默认练习场（海陆空全部样例）的真实启动、单位数量及正供电，防止专项场景通过而默认场景漏建或缺电。
