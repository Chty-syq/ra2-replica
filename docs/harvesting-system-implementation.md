# 采矿系统实现（2026-09-16）

已接通矿区 → 采集 → 满载或矿区耗尽 → 返回矿场 → 卸货到账 → 驶离泊位 → 继续采集的循环。
这是可运行的第一版通用采矿能力，还不是已经完成原版行为校准的采矿系统。

2026-10-10：两种矿场的泊位配置已迁移为 `DockCells=4:1`，基准是占地起始格；原有外部停靠位置不变。

2026-10-12：从 `assets_origin/terrain` 找到了此前漏检的矿物覆盖层。原文件扩展名是 `.tem/.sno/.urb`，内部是 SHP，并不缺失。
已复制 72 个原版覆盖层及 `sprites/oregath.shp` 到运行资源目录，逐文件 SHA-256 校验一致。
三种主题各使用 12 种矿石、12 种宝石外形和 12 档矿量；地格坐标稳定决定外形，矿量减少仅切换疏密帧。
`TIB02` 原文件的满矿帧在第 0 帧，运行时将显示顺序调整为 `1..11,0`，避免最后少量矿石突然变密；文件内容保持原样。
采集动画使用原版 `OREGATH` 的 8 方向 × 15 帧，在正在采矿时按当前采集周期播放，移动、停止、返矿时结束。
矿物图集保留透明索引和完整画布坐标，不接受阵营染色。最初误用了 `unit*.pal`，现已按下述资料核对并修正。
演示矿区有浓密中心和稀疏边缘；悬停矿格可查看剩余数量、单价和总价值。

### 矿物颜色修正（2026-10-12）

矿物的 `.tem/.sno/.urb` 只决定素材的剧场版本，不代表使用单位调色盘。RA2 的矿物覆盖层和普通地块使用不同的 palette：

| 用途 | 温带 | 雪地 | 城市 |
|---|---|---|---|
| 黄金矿、宝石矿 | `temperat.pal` | `snow.pal` | `urban.pal` |
| 普通 TMP 地块、道路、悬崖 | `isotem.pal` | `isosno.pal` | `isourb.pal` |
| 单位、普通建筑 | `unittem.pal` | `unitsno.pal` | `uniturb.pal` |

依据是 [C&C Maps Renderer 的 RA2 剧场配置](https://github.com/zzattack/ccmaps-net/blob/56712d7c9c5048afbfe9bddb01c0ec43f6fc008a/CNCMaps.Shared/ModConfig.cs#L199)
以及 [RA2 矿物绘制分支](https://github.com/zzattack/ccmaps-net/blob/56712d7c9c5048afbfe9bddb01c0ec43f6fc008a/CNCMaps.Engine/Game/ObjectCollection.cs#L194)：矿物使用 Overlay palette，LightingType=None。
[OpenRA RA2 的矿物配置](https://github.com/OpenRA/ra2/blob/master/mods/ra2/rules/world.yaml#L10)
把 Ore/Gems 都映射到 resource，其 [调色盘配置](https://github.com/OpenRA/ra2/blob/master/mods/ra2/rules/palettes.yaml#L114) 指向 `temperat.pal`，可交叉验证。
本地原始 `temperat.pal/snow.pal/urban.pal` 三份数据的 SHA-256 相同，均为 `5903B69868B84F494CFBB4E7100398015EF9775B37726019A0D7B5FB6CB33B55`，运行资源保留三份原名以支持主题配置。

采集动画 `oregath.shp` 使用 `anim.pal`。参考 [OpenRA CMIN 的采集覆盖层](https://github.com/OpenRA/ra2/blob/master/mods/ra2/rules/allied-vehicles.yaml#L81)，其 Palette=effect，对应 `anim.pal`。
现已将矿物、采集动画分别接入独立的 Resource/Animation 索引调色盘，移除错误继承的 0.82 建筑压暗；原 SHP 和 palette 字节均保持不变。
此前矿石杂色、宝石偏紫和雪地变色来自调色盘不匹配，不能通过单纯提高亮度修正。

`--mining-smoke-test` 增加 GPU 读回验证：三个主题中分别绘制矿石、宝石和采集动画，将每个像素与原始 palette 的预期 RGB 比对；同时设置不同阵营色，检查这些素材没有意外染色。

## 在游戏中使用

启动 `ra2_power_demo`，默认场景有金色矿石、紫色宝石、盟军与苏军矿场。
矿场按照 `FreeUnit` 各创建一辆矿车，自动开始工作。矿区的颗粒随剩余矿量减少，耗尽后消失。

- 左键选中矿车，再点矿区：采集该区域，满载后自动往返。
- 点可用的己方矿场：提前返回卸货，允许不足一车。
- `H` 或“自动采矿”按钮：恢复自动采矿。
- `S`：停止，保留货物；普通移动/攻击命令也会暂停采矿。
- 选中矿车可看货舱格、具体载量、货物价值和当前工作状态。
- 矿场建成才交付附带矿车；出口受阻会重试，不重复生成。
- 可以从匹配的战车工厂继续生产矿车，完成出厂流程之后才会自动采矿。

默认仍使用原有建造沙盒（`Sandbox=yes`），采矿收入会增加 Credits，但建造不扣费。
在 `replica.ini [Simulation]` 中设 `Sandbox=no` 即使用现有的费用和科技前置检查；
回归测试验证了从 0 资金采到 1000，再支付 900 生产犀牛坦克的流程。

## 模块职责

| 文件 | 职责 |
|---|---|
| `src/config/definitions.*` | 从规则编译资源、采矿与矿场能力，验证类型引用和泊位配置 |
| `src/simulation/harvesting.*` | 资源数量、分类货舱、采矿状态、资源认领、矿场队列/泊位、附带矿车 |
| `src/simulation/world.*` | 命令、共享寻路预算、实体占用、移动/传送、生产、死亡清理、玩家资金 |
| `src/resource_art.*` | 原版矿物/采集帧解码和图集、矿量档位、稳定外形及朝向选择 |
| `src/presentation/resource_view.*` | 原版矿区与采集动画、选中矿车货舱格，仅读取模拟状态 |
| `src/app_main_ui.cpp` | 演示场景、采矿交互、状态显示 |
| `tests/harvesting_tests.cpp` | 无窗口采矿回归场景 |

`HarvestSystem` 通过小型 `HarvestHost` 接口访问世界，不保留 ECS 组件引用。
矿场生成矿车可能使 ECS 存储移动，因此跨生成操作只保留稳定实体 ID 和值快照。
HARV、CMIN 和新配置的矿车使用相同状态机；没有按单位 ID 分派采矿逻辑。
超时空返回是一项能力开关。场景、资源外观及测试中出现的具体类型 ID 不决定系统行为。

## 配置来源

| 来源 | 已接入内容 |
|---|---|
| `rules.ini [Tiberiums]` 与对应资源段 | 资源类型、`Value`；`Image=2` 使用宝石原版图集 |
| 矿车类型 | `Harvester`、`Storage`、`Dock`、`Teleporter`、受支持的移动能力 |
| 矿场类型 | `Refinery`、`DockUnload`、`NumberOfDocks`、`FreeUnit` |
| 原有单位定义 | 移动速度、转向、生命、造价、生产和武器 |
| `replica.ini [Harvester.<ID>]` | `HarvestFrames`、`UnloadFrames`、`ChronoReturnFrames` |
| `replica.ini [Refinery.<ID>]` | `DockCells=x:y,...`，相对占地西北起始格的外部泊位格 |
| `replica.ini [ResourceVisual.<ID>]` | `FullDensityAmount`：满密度贴图对应的矿量，矿石 32、宝石 24；只影响显示，不修改资源上限或单价 |

目前导入 HARV 的 40 单位载量、CMIN 的 20 单位载量，矿石 25/单位、宝石 50/单位。
混合货物逐项计价；只有在泊位实际卸下货物才增加所属玩家资金。资金到达整数上限时暂停卸货并保留货物。
采矿一单位默认 15 逻辑帧、卸货一单位 3 帧，超时空返回等待 16 帧。
这三个时序是明确标注的待校准参数；16 帧仅采用规则中的最小延迟量级，没有实现原版完整的距离计算和超时空恢复时序。

未给 `DockCells` 时，系统在现有建筑占地的外侧生成泊位；显式配置的泊位数量须与 `NumberOfDocks` 一致，不能重复或落在实体占地内。
HORV/CMON 等卸矿表现类型没有 `Storage`，不会当成能独立生产和采矿的完整矿车。

## 冲突和生命周期

- 矿格有认领者，不能由两辆矿车同时采集；数量达到 0 后移除资源。
- 满载返回；没有可用矿格时，未满货舱也返回卸货。
- 矿场必须同属一个玩家、已经建成，并在矿车的 `Dock` 白名单内。
- 一个矿场维护先进先出的等待队列，以及配置数量的泊位。驶离后才释放泊位，避免卸空矿车永久挡住队伍。
- 矿车移动与其他车辆共享实际格子占用、路径及每帧 8192 节点预算。采矿寻路单次最多使用 2048 节点。
- 采集点和卸货点必须精确到达，不能沿用普通移动的“走到附近空格即算完成”。
- 不可达矿格/矿场短暂避让后重试，并可选择其他目标；持续没有行进进展时会重新选择。
- 矿场被毁，矿车保留货物重新寻找目标；没有矿场时等待，出现新矿场后自动恢复。
- 矿车死亡释放矿格、队列及泊位；其货物随车辆损失，不凭空计入资金。
- Stop/Move/Attack 取消采矿任务，但不清空货舱。出厂中的命令继续使用原有延后执行机制。
- 超时空返回原子检查目标占用并更新位置，不能落到其他实体上；表现插值不会把瞬移画成跨地图滑行。

资源、分类货物、队列、泊位、计时器、重试记录和待交付矿车都纳入确定性状态散列。
逻辑使用整数与稳定 ID 顺序；游戏速度改变实际耗时，不改变相同逻辑帧数下的经济结果。

## 验证

新增 `harvesting_regressions` 和隐藏窗口 `demo_harvesting`，连同原有测试共 14 项。
MinGW GCC 13.1 Debug 全部 14 项通过；MSVC AddressSanitizer 核心 8 项通过，未发现内存错误。
采矿回归覆盖：混合矿物守恒、满载返回、耗尽后的部分卸货、停止/移动/继续、手动返矿、所属玩家与 Dock 白名单、
多矿车单泊位排队、多泊位同时卸货、矿格不可达、矿场受阻及替代矿场、矿场/矿车死亡、
FreeUnit 建成后交付/阻塞重试/仅交付一次、新矿车纯配置扩展、实际生产扣费、出厂约束、不同速度下确定性。

```text
cmake --build cmake-build-debug -j 6
ctest --test-dir cmake-build-debug --output-on-failure -j 4
cmake-build-debug/ra2_power_demo.exe --mining-smoke-test
```

为最后一条命令再传一个 BMP 路径，可保存逻辑帧 600、1500 与 2700 的采矿场景截图。
隐藏窗口验证矿车真实模型、矿场资源、资源地面绘制、货舱和收入闭环。

2026-10-12 更新：`--mining-smoke-test` 在运行中切换温带、雪地、城市，截图帧为 600、1500、2700，
并检查 OpenGL 错误。新增 `resource_art_regressions`，逐帧比对原始像素、透明度和帧偏移，检查所有外形的矿量单调变化、耗尽不绘制及采集朝向。
`project_asset_decode` 现在也扫描矿物的剧场扩展名；采矿测试增加采集动画计时与逻辑周期一致、读取不改变状态的验证。
本次 MinGW Debug 编译完成，全部 15 项 CTest 通过，并人工检查了三个主题的场景截图。

## 与原版仍有差异

1. 已使用原版矿石、宝石和采集动画；尚未读取原版地图的覆盖层布局、斜坡矿物和矿柱。当前矿量到帧的映射及采集动画节奏由演示配置驱动，未宣称完全校准原版行为。
2. 泊位目前设在建筑实体占地之外。还需实现矿场可通行的基础区域、入场/倒车朝向、卸矿车型切换、矿场联动动画。
3. 采集节奏、卸货节奏、搜索偏好、超时空返回距离和等待时序尚未用干净的 RA2 1.006 轨迹校准。
4. 尚无矿石再生/扩散、矿柱生成、矿石精炼器收入加成、资源仓储上限及溢出行为。
5. 没有矿车自动反击、自愈和完整受攻击反应。HARV 可以沿用普通攻击命令；为其机枪增加了通用的地面不可见弹道支持，但障碍物拦截与特效仍使用现有战斗系统。
6. 阻路处理使用有界寻路与超时重试，没有原版完整的挪车、避敌和矿场无线电握手行为。

## 参考依据

- [OpenTS UnitClass 采矿任务](https://github.com/OpenTS-Developers/OpenTS/blob/1985c39712ebf02a114cb2d0ced991447e1f4f21/code/unit.cpp)：参考寻找资源、采集、寻找矿场、返程的任务阶段与矿场联系概念。它是 TS 的实现，不能直接当成 RA2 特性的标准。
- [OpenRA Harvester](https://github.com/OpenRA/OpenRA/blob/f3ec7f8e1593b482f85fd101652deb740c33dee6/OpenRA.Mods.Common/Traits/Harvester.cs) 与 [Refinery](https://github.com/OpenRA/OpenRA/blob/f3ec7f8e1593b482f85fd101652deb740c33dee6/OpenRA.Mods.Common/Traits/Buildings/Refinery.cs)：参考把采矿能力和矿场处理分离的组织方式，不采用其平衡参数宣称还原原版。

实现为本项目的独立 C++20 模块，没有移植这两个项目的采矿源代码。
