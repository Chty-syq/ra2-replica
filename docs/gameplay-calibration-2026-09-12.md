# 移动、转向、生产与动画：第一轮校准

日期：2026-09-12。承接 [游戏速度实现](game-speed-implementation.md)。

本轮把已有演示参数替换为规则文件和参考引擎支持的换算，继续使用统一的配置定义、World 和只读表现层，
没有增加犀牛、灰熊等兵种专属逻辑。所有下表数字都是**本项目的场景测量**，不是原版游戏的实机测量。

## 证据与适用范围

- 游戏数值来自项目的 `assets/settings/rules.ini` 和 `art.ini`。
- 转向和引擎接口参考 [YRpp Facing.h](https://github.com/Phobos-developers/YRpp/blob/72fb9ee29e6ac9e26007555f3bf58ee4726a7269/Facing.h)、
  [Dir.h](https://github.com/Phobos-developers/YRpp/blob/72fb9ee29e6ac9e26007555f3bf58ee4726a7269/Dir.h)、
  [FactoryClass.h](https://github.com/Phobos-developers/YRpp/blob/72fb9ee29e6ac9e26007555f3bf58ee4726a7269/FactoryClass.h)。
  YRpp 固定提交：`72fb9ee29e6ac9e26007555f3bf58ee4726a7269`。
- 动画启动时处理 Rate、Normalized 的参考为 [Phobos Anim 实现](https://github.com/Phobos-developers/Phobos/blob/b90bfd872813a847b77c2bf5d9ddcedab66b10e3/src/Ext/Anim/Body.cpp)。
  Phobos 固定提交：`b90bfd872813a847b77c2bf5d9ddcedab66b10e3`。
- 本机找到 `F:/ra2/yr20211001/gamemd.exe`，文件版本 `1.11-CnCNet-patch-213aa6b`，
  SHA-256 为 `07188e1caf521610f9485cd302a1bec894d2dcc05bdb69c5593e794ed88cd1c2`。
  对它只做了静态反汇编和常量读取，没有运行或修改该安装。
  原始常量和地址记录在 [reference-timing.json](calibration/reference-timing.json)。

这份参考是带补丁的《尤里的复仇》，不是未修改的《红色警戒 2》1.006。
因此本轮结果表示“依据规则和参考引擎校准”，不能作为 RA2 1.006 表现完全一致的证据。
后续仍需在明确版本、相同规则和相同场景下做原版运行对照。

## 修改与测量结果

CSV 保留在 [校准前](calibration/replica-before.csv) 和 [校准后](calibration/replica-after.csv)。
计时单位为游戏帧；对战 6 档每现实秒 60 帧，对战 4 档每现实秒 30 帧。

| 场景 | 校准前 | 校准后 | 当前对战 6 档耗时 |
| --- | ---: | ---: | ---: |
| 犀牛 HTNK 从静止直行 10 格 | 250 | 171 | 2.85 秒 |
| 灰熊 MTNK 从静止直行 10 格 | 215 | 151 | 约 2.517 秒 |
| 盟军基地车 AMCV 从静止直行 10 格 | 375 | 256 | 约 4.267 秒 |
| 犀牛/灰熊炮塔转过 90 度至首次射击 | 19 | 13 | 约 0.217 秒 |
| 单工厂生产犀牛至厂内生成 | 120 | 540 | 9 秒 |
| 单工厂生产灰熊至厂内生成 | 120 | 432 | 7.2 秒 |
| 单工厂生产盟军基地车至厂内生成 | 120 | 1890 | 31.5 秒 |
| 发电厂/盟苏战车工厂放置后的名义施工 | 120 | 54 | 0.9 秒 |

直行场景从 `(0,0)` 到 `(10,0)`，初始朝向已对准，无障碍与其他单位，
命令在第 1 帧执行，记录首次到达终点中心的帧。炮塔场景向 `(0,3)` 强制攻击，
记录首次创建射击事件的帧，包含命令开始转向的一帧。
生产场景为空闲、出口畅通的单工厂，记录实体在厂内创建，不包含准备和驶离时间。
生产变长是移除了原先固定两秒的沙盒加速，并非模拟性能下降。

### 移动与转向

参考程序的单位 Speed 导入位置为 `0x71464A`，基础速度为：

```text
leptons_per_frame = min(255, floor(clamp(Speed, 0, 100) * 256 / 100))
1 cell = 256 leptons
```

犀牛 Speed=6 得到 15 leptons/帧，灰熊 Speed=7 得到 17，基地车 Speed=4 得到 10。
项目仍以 1000 子格表示一格，换算余数跨帧、跨格保留，避免在每个格子边界损失位移。
尚未实现原版完整的弧形行驶轨迹、加减速、地形与老兵等速度修正。

转向从最短的有符号 16 位角度差计算整数持续时间：

```text
duration = floor(abs(angle_delta) / (clamp(ROT, 0, 127) * 256))
```

持续时间内按剩余帧数插值整个角度，而不是每帧累加旧的经验常数。
ROT=5 转过 90 度用 12 个经过的游戏帧；命令在第 1 帧开始，首次射击在第 13 帧。
ROT=0 按参考转向类直接到达目标朝向。
转向状态进入确定性摘要；停止命令清除旧转向计时，防止恢复同方向命令时突然跳转。

### 生产与施工

导入 `General.BuildSpeed` 和类型的 `BuildTimeMultiplier`，常规生产使用 54 段整数时钟：

```text
base = floor(Cost * BuildSpeed * 0.9)
adjusted = floor(base * BuildTimeMultiplier)
interval = clamp(floor(adjusted / 54), 1, 255)
production_frames = 54 * interval
```

证据位置：类型基础时间 `0x711EE0`、实例修正 `0x6F47A0`、工厂分段计时 `0x4C9EA0`。
当前只覆盖单工厂、正常供电、普通单位的基础时间和类型倍率。
房屋/难度加成、低电量、多工厂倍率、逐步扣款等还没有接入；现有一次扣费和取消退款行为保持原实现。
倍率在本项目中编译成百万分之一精度的整数，保证跨编译器确定性；
没有声称所有特殊配置的舍入边界都与原引擎 x87 浮点运算逐位一致。

`Sandbox=yes` 仍允许免费生产和放置，但不再自动缩短生产时间。
`ProductionFramesOverride=0` 使用规则；显式设为正整数可以覆盖生产时长。
队列、出厂等既有测试通过 `tests/simulation_fixture.h` 显式选择 120 帧，
新增校准测试、测量工具和图形冒烟场景使用真实默认规则，避免快速测试掩盖默认行为错误。

施工名义时长现为 `floor(BuildupTime * 900)`，默认 `.06` 得到 54 帧。
**施工还未完成精确校准**：参考程序在 `0x45F2DD` 将该预算除以 SHP 的施工帧数，
再对单帧间隔取整；项目尚未把这一资源元数据纳入模拟定义，因此这里只使用名义总时长。
基地车展开/收起仍有 90 帧的暂定流程，出厂准备/收尾仍为 12/6 帧。

### 建筑工作和生产动画

原先把 `art.ini Rate` 当作毫秒间隔，现改为原始游戏帧间隔：

- 正数 Rate：`floor(900 / Rate)` 游戏帧。
- 缺省或 Rate=-1：保留默认 1 帧。
- 其他非正数或换算结果为 0：不推进动画。
- `Normalized=yes`：在动画启动时再根据原引擎反向速度索引换算间隔。

Rate 导入位置为 `0x427F7B`，归一化函数为 `0x5FB2E0`。
较小的 1～4 帧间隔使用整数查表，更长的间隔使用 `floor(delay * 8 / (index+1))`，
其中本项目 `index = 6 - GS档位`，查表原始数据保留在 JSON 中。
例如发电厂 GAPOWR_A 的 Rate=220 得到基础间隔 4 帧，Normalized 后对战 6 档为 7 帧，4 档为 5 帧。

图层按整数游戏帧选帧，不再经毫秒换算产生累计误差。
动画开始时保存档位；切档不重启动画，也不改写已有周期的间隔，新启动的周期采用新档位。
这些呈现数据不进入玩法状态摘要。主层和前景层继续共用阶段进度。
RandomRate、Next、Reverse，以及完整的开始/循环/结束序列规则还没有全部接入。

## 代码与复现

| 入口 | 用途 |
| --- | --- |
| `src/compat/rules_timing.h` | 集中保存已核实的换算和整数边界 |
| `src/config/definitions.*` | 导入原始规则，编译类型生产时间，拒绝旧参数 |
| `src/simulation/world.*` | 通用移动、转向、生产和动画启动事件 |
| `src/art_ini.*`、`src/building_system*` | Rate/Normalized 解析和公共图层选帧 |
| `src/presentation/world_view.*` | 只读动画时刻和档位适配 |
| `tests/calibration_tests.cpp` | 默认规则的事件时间、配置覆盖及边界回归 |
| `tools/calibration_probe.cpp` | 固定场景测量，输出 CSV |
| `tools/inspect_reference_timing.py` | 只读提取已核对版本的引擎常量，未知哈希报错 |

```powershell
cmake -S . -B build-calibration -DRA2_BUILD_DEMOS=OFF -DRA2_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-calibration --target ra2_calibration_probe
./build-calibration/ra2_calibration_probe.exe assets/settings
python tools/inspect_reference_timing.py F:/ra2/yr20211001/gamemd.exe
```

CSV 输出到标准输出，配置摘要输出到标准错误，便于分别保存。
本轮正常配置摘要：`054d3e0bb3777697:fe9b6c0db86bd160:433104a9c5ec79d0`。
快速生产测试使用配置覆盖，摘要与默认配置不同，不能混用。

旧的 `SpeedSubcellsNumerator/Denominator`、`TurnUnitsNumerator/Denominator`、`BuildFrames`、
`ProductionFramesPerCredit` 已移除；加载时遇到这些键会报告来源和迁移错误，不会静默忽略。

## 验证与下一轮对照

- MinGW GCC 13.1 Debug：主程序及两个渲染实验构建通过，11 项 CTest 全部通过。
- MinGW Release：7 项无图形测试通过。
- MSVC 19.50 AddressSanitizer：核心模块全量重建，7 项无图形测试通过。
- 新测试覆盖三类车辆的实际到达/生成帧、转向停止后恢复、类型生产倍率、无效配置和动画切档。
- 既有有限档/不限速回放一致性、出厂阻塞、延后命令、伤害和资源解码测试仍通过。
- 主程序图形场景使用默认生产时长，检查内部生成、出厂、基地车展开和目标死亡；
  `--simulation-smoke-test [截图.bmp]` 现在运行到第 2160 游戏帧，另保存第 1920 帧的 `-egress.bmp`。
  两张实际渲染截图已检查，车辆和建筑图层正常显示。这些合成时间步场景不能用来宣称实际性能提升。

三种编译配置的回放摘要一致。以下场景使用快速生产覆盖，配置摘要为
`054d3e0bb3777697:fe9b6c0db86bd160:2e85056d1580d0c7`：

| 场景 | 状态摘要 |
| --- | --- |
| 普通移动/开火 | `13623927451343281313` |
| 多工厂/延后命令 | `16811057825505610903` |
| 全档位回放 | `1910882574402675063` |

下一轮原版对照应先固定 RA2 1.006 的版本和规则，使用平坦无障碍测试地图、普通未升级车辆、
单工厂和正常电量，分别测直行 10 格、90 度转向、首辆生产与驶离、施工和连续工作动画。
优先记录游戏帧计数；若只能录像，应同时记录录像帧率、档位和是否掉速，
区分命令发出、开始动作、生产完成、实体出现和驶离完成，不能把所有阶段合并成一个秒数。
当前最需要补齐的是弯道轨迹、加减速、SHP 施工取整与基地车转换时序；弹道和经济修正也仍待验证。
