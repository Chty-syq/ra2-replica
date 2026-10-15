# 坦克移动、炮塔与弹道核查

日期：2026-10-12。范围：当前主程序实际使用的 `World → makeWorldView → VXL / weapon renderer`，重点为犀牛 HTNK、灰熊 MTNK。下文保留修复前的核查证据；修复结果见本节，旧代码行号仅对应核查时版本。

## 修复结果

已在主程序实际使用的路径接入修复，继续使用原版 rules、art、VXL、HVA 和 SHP。Speed、ROT、ROF 的基础换算没有改变，没有额外加入起步惯性或炮管后坐力。

| 项目 | 已实现行为 | 验证结果 |
|---|---|---|
| 跨格移动 | 同一逻辑帧消耗剩余路程，定点累计距离；保留占格、预留和出厂专用通道 | 犀牛步长从 4～113 改为 58～59 milli-cells；灰熊从 4～128 改为 66～67。10 格仍为 171 / 151 帧 |
| 转弯 | 直线接近段连接拐点内的二次曲线，不再强制穿过格心；按弧长行驶，预留扫过的邻格；急弯受 ROT 控制 | `(10,5)` 路线从 212 改为 206 帧，途中静止转向从 5 帧改为 0；45° 转弯不再反摆或超调 |
| 炮塔 | 保留独立 16 位朝向；移动归正受 ROT 控制；变化中的目标方向不会重置并冻结转向；VXL 使用与仿真相同的完整角度 | 90° 转塔后移动首帧保持 90°；瞄准 `(3,1)` 从 22.5° 改为 18.435059°，几何值 18.434949° |
| 战斗状态 | 读取 `OpportunityFire`、`CanPassiveAquire`；待机寻敌、移动射击、追击中持续瞄准；自动射击不取消移动命令 | 移动交火、禁止机会射击和持续变化目标方向均有回归测试 |
| 炮弹 | 从实际 FLH 发射，读取 Gravity；定点维护三维位置和速度，显示层只插值；发射时按目标当前速度预测，发射后不跟踪 | 1 / 3 / 5 格静止目标伤害正确；匀速目标预判可命中；发射后才开始移动的目标仍可躲开 |
| 障碍与地表 | 读取 Cannon 的三种碰撞开关，扫过墙、悬崖和地面；寻路、移动采用地表通行系数；高度和 IsTilter 传入车辆显示及炮口变换 | 墙与高地拦截、绕障、Ice 的 Track=80%、坡面抬升及多朝向 FLH 一致性测试通过 |
| 动画 | 使用武器 `Anim`、弹头 `AnimList` 和 art 时序，读取实际 SHP 帧数、Normalized、Translucent | GUNFIRE 完整播放 4 帧；AP 按伤害选 S_CLSN22，单人 6 速下播放 26 逻辑帧 |

运动与弹道整数几何集中在 [kinematics.cpp](../src/simulation/kinematics.cpp)，权威状态在 [world.cpp](../src/simulation/world.cpp)，显示适配在 [world_view.cpp](../src/presentation/world_view.cpp)。补充的动画分档依据是 [Phobos PR 2394](https://github.com/Phobos-developers/Phobos/pull/2394)：默认按伤害除以 25 后截断，并限制在列表范围内；没有启用扩展的随机选择。

弹道使用范围、重力推导有效初速，低角度解采用 1/256 逻辑帧精度，逐帧先推进位置再施加重力。碰撞沿本帧线段以不超过 1/32 格的间隔采样，避免穿过整格障碍。当前 1 / 3 / 5 格的预测飞行时间为 2 / 7 / 12 帧；这是本实现的诊断结果，**不是原版逐帧实测值**。首轮修复的 [CSV](calibration/tank-behavior-after-2026-10-12.csv) 与修复前 [CSV](calibration/tank-behavior-2026-10-12.csv) 均保留，后续转弯数据见下节。CSV 中水平速度列是距离除以整帧飞行时间的平均数，不能作为初速；射击计数只统计 HTNK。

验证：完整构建无编译警告，CTest **17/17 通过**，包含原有时钟、确定性回放、建造、采矿、出厂测试，以及新增 [tank_behavior_tests.cpp](../tests/tank_behavior_tests.cpp) 和隐藏 OpenGL 场景 `--tank-smoke-test`。该场景可附带 BMP 路径，输出第 1、8、16、24、30、34、38、48、80、120 帧截图，已检查转弯、移动交火、飞行炮弹与原版爆炸显示。

### 拐弯扭动回归修复

首轮曲线仍强制连接每个格心，同时约束首尾切线。这会产生多余的 S 弯：犀牛从 `(0,0)` 前往 `(10,1)` 时，车身先从 0° 反扭到 −7.05322°，再超过目标方向达到 52.0642°，最后回到 45°。首轮测试只检查是否边走边转，没有检查转向是否单调，因此遗漏了这个回归。

改为在拐点前后裁出圆滑转弯段，保留一段直线接近路径；朝向从曲线的解析切线插值，下一段从实际终点继续，不跳回格心。急弯限制每帧车身转角，目标格最终仍到达格心。新实现同一路线的朝向严格在 0°～45°，总转角从 73.2349° 降到 45°，数据见 [转弯对比 CSV](calibration/tank-turn-wobble-2026-10-12.csv)。

新增回归覆盖八个朝向的左／右 45°、90° 转弯、犀牛和灰熊的实际跨段移动、转弯中停止后改道、狭窄直角绕障，并检查朝向不反摆、不超调和位置不跳变。完整 17 项测试已通过，实际 OpenGL 转弯帧也已核对。

校准边界：当前曲线是参考原引擎轨迹状态及 OpenRA 连接方式的实现，并非逐项复制原版 72 条 TurnTrack；精确转弯形状、原地转向条件、开火容差、移动目标提前量和炮弹落地帧，还需要纯净 RA2 1.006 同速录像校准。地图高程、坡度与墙体已通过 `World::setTerrain` 接入逻辑和车辆姿态，但演示场景仍为平面网格，尚未导入原版地图的坡地、悬崖和墙体图块；新增地形测试不会让演示场景凭空出现山坡。

## 修复前结论

结论：差异主要集中在移动轨迹、跨格速度分配、炮塔状态切换、移动射击和炮弹飞行模型。基本 Speed、ROT、ROF 数值已有校准，但这不足以保证整体观感接近原版。

## 证据范围

- **直接证据**：当前运行代码、项目内原版 `rules.ini` / `art.ini`、当前程序的逐帧诊断。
- **引擎参考**：YRpp / Phobos 对尤里的复仇引擎的类型与函数挂钩；用于确认机制，不能把扩展功能自动视为原版功能。
- **实现对照**：OpenRA 引擎和 RA2 模组；用于比较状态管理、轨迹和坐标变换。OpenRA 有独立的时钟、单位尺度、弹道和游戏性取舍，不能直接抄数值。
- **静态补充**：本机 `F:/ra2/yr20211001/gamemd.exe`，版本 `1.11-CnCNet-patch-213aa6b`，SHA256 `07188e1caf521610f9485cd302a1bec894d2dcc05bdb69c5593e794ed88cd1c2`。仅静态检查，没有运行或录制原版。它不是纯净 RA2 1.006 的逐帧基准。

## 优先处理的明确问题

### 1. 直行跨格时发生速度脉冲

位置：[world.cpp:355](../src/simulation/world.cpp#L355)、[world.cpp:368](../src/simulation/world.cpp#L368)。

移动到格子中心时，位置先截断到中心，剩余路程留给下一逻辑帧。总路程没有丢失，但本帧少走、下一帧多走。

实测犀牛从 `(0,0)` 直行到 `(10,0)`：第 16～20 帧的位移为 **0.059、0.059、0.004、0.113、0.058 格**。灰熊同样出现最小 0.004、最大 0.128 格的步长。两者最后仍在 171 / 151 帧到达，解释了为什么已有总耗时测试通过，画面仍有周期性顿挫。渲染插值不能消除这种跨帧速度变化。

建议：在同一个逻辑帧内消耗跨段剩余路程，保留占格和预留检查。OpenRA 的 MovePart 也区分本帧新速度与跨段 carryover，避免重新增加整帧速度。[Move.cs](https://github.com/OpenRA/OpenRA/blob/e180e3225f3930272dd451917467631618f8976f/OpenRA.Mods.Common/Activities/Move/Move.cs#L474)

### 2. 拐弯固定为“停车 → 转正 → 直走”

位置：[world.cpp:349](../src/simulation/world.cpp#L349)。只要车身还没完全对准下一个格子，立即返回，不推进位置。路径执行只有格心之间的直线段，没有转弯轨迹。

实测犀牛到 `(10,5)` 用时 212 帧，其中 5 帧车身转动而位置完全不变，到转向完成的那一帧才开始移动。绕障碍、多次改变方向时更明显。

YRpp 的 DriveLocomotion 对应本项目坦克使用的同一 Locomotor GUID，含 72 项 TurnTrack、16 项 RawTrack 以及 TrackNumber / TrackIndex；说明原引擎有专门的行驶轨迹状态。OpenRA 则显式构造连接前后段的弧线，并更新沿弧车身朝向。现实现缺少这一层；原版哪些转弯允许原地旋转、具体曲线和耗时，仍需干净版本实测。[DriveLocomotionClass.h](https://github.com/Phobos-developers/YRpp/blob/b22f1860dd7b100cad57123d4ca5d459b9020d88/DriveLocomotionClass.h#L13)、[OpenRA Move.cs](https://github.com/OpenRA/OpenRA/blob/e180e3225f3930272dd451917467631618f8976f/OpenRA.Mods.Common/Activities/Move/Move.cs#L445)

### 3. 移动命令让炮塔瞬间归零

位置：[world.cpp:351](../src/simulation/world.cpp#L351)。未处于主动攻击时，直接把炮塔角度赋为车身角度并重置转向计时器。

复现：车身朝 0°，先向侧面开炮，炮塔达到 90°；随后沿车身方向移动。**一帧后炮塔从 90° 变成 0°**，绕过 ROT 限制。这是确定的状态切换突变，不是贴图问题。

建议：炮塔保留自己的朝向和转向状态，移动时通过正常旋转逻辑归正。OpenRA 的炮塔有独立归正状态并受 TurnSpeed 约束；它的 RealignDelay 默认值不能作为原版 RA2 的时间直接采用。[Turreted.cs](https://github.com/OpenRA/OpenRA/blob/e180e3225f3930272dd451917467631618f8976f/OpenRA.Mods.Common/Traits/Turreted.cs#L195)

### 4. 炮塔、车身、炮口使用的角度不一致

位置：[app_main_ui.cpp:754](../src/app_main_ui.cpp#L754)、[rhino_unit.cpp:186](../src/rhino_unit.cpp#L186)、[world_view.cpp:18](../src/presentation/world_view.cpp#L18)、[world.cpp:30](../src/simulation/world.cpp#L30)。

- 车身渲染先量化成 32 向；炮塔相对转角却减去未量化的车身角。合成结果是 `炮塔真实角 + 量化车身角 - 真实车身角`，会引入约 ±5.625° 的锯齿误差。炮口位置又按逻辑炮塔角计算，可能与画出的炮管错位。
- 瞄准逻辑本身也提前量化为 32 向。目标 `(3,1)` 的准确方向是 **18.434949°**，实测发射朝向却是 **22.5°**；炮弹再从该炮口连向准确目标点。因此炮管指向与炮弹实际离开方向可能不共线。

原引擎 Facing / Dir 保留 16 位角度，并提供独立的方向量化接口。不能仅凭“原版体素有离散方向”就把瞄准计算也截成 32 向。应分别确认逻辑瞄准精度和最终体素绘制精度。[YRpp Dir.h](https://github.com/Phobos-developers/YRpp/blob/b22f1860dd7b100cad57123d4ca5d459b9020d88/Dir.h#L12)、[Facing.h](https://github.com/Phobos-developers/YRpp/blob/b22f1860dd7b100cad57123d4ca5d459b9020d88/Facing.h#L83)

OpenRA 的 VXL 炮塔直接使用炮塔世界朝向，避免依赖另一套车身量化结果。[WithVoxelTurret.cs](https://github.com/OpenRA/OpenRA/blob/e180e3225f3930272dd451917467631618f8976f/OpenRA.Mods.Cnc/Traits/Render/WithVoxelTurret.cs#L63)

### 5. 原版移动射击没有接入

位置：[world.cpp:371](../src/simulation/world.cpp#L371)、[definitions.h:41](../src/config/definitions.h#L41)。

原版犀牛、灰熊都设置 `OpportunityFire=yes`，规则注释明确包含执行移动等动作时射击。当前定义没有解析此项，也没有自动寻敌；weapons 只有在显式攻击命令激活后才运行，进入射程后还会清掉移动路径，等走完当前预留格才瞄准开火。

实测：敌方灰熊在射程内，犀牛待机 180 帧发射 **0** 发；随后从其旁边经过 120 帧也是 **0** 发。它缺少原版坦克边走边转炮塔、攻击附近敌人的行为。[原版规则注释](../assets/settings/rules.ini#L2951)、[HTNK](../assets/settings/rules.ini#L6518)、[MTNK](../assets/settings/rules.ini#L6609)

注意：不要把“移动命令必须保留原指定目标”当作原版要求。Phobos 将 KeepTargetOnMove 列为可选扩展；自动机会射击和保留指定目标是两个问题。[Phobos 文档](https://phobos.readthedocs.io/en/latest/Fixed-or-Improved-Logics.html#retain-target-on-movement-command)

### 6. 炮弹飞行仍是简化模型，重力、速度语义和炮口距离不一致

位置：[world.cpp:400](../src/simulation/world.cpp#L400)、[world_view.cpp:74](../src/presentation/world_view.cpp#L74)。

- 原版 `[General] Gravity=6`，当前显示弹道硬编码为 **1**。
- 仿真把原始武器 `Speed=40` 直接当作每帧水平移动的 leptons，再用距离除以速度生成预定命中帧。Phobos 在原引擎 WeaponType SetSpeed / GetSpeed、瞄准角和炮弹 AI 上都有重力挂钩；这些步骤不能被一个距离倒计时替代。
- 命中时间用**车中心**到目标的距离计算，实际显示从 **PrimaryFireFLH 炮口**出发。这使视觉水平速度不等于仿真使用的 40。
- 仿真不维护炮弹三维位置和速度；显示层另外构造一条保证在指定帧落到目标点的抛物线。物理状态和画面没有统一来源。

实测犀牛射向 1 / 3 / 5 格处：飞行时间为 **7 / 20 / 32 逻辑帧**；扣掉炮口前伸 150 leptons 后，视觉水平速度为 **15.14 / 30.90 / 35.31 leptons/帧**。这些是当前程序的数值，不是原版对照实测值。

静态补充：上述本机 YR 文件在 `0x7729F0` 对无 ROT 的抛射体重新生成武器速度，读取 Range、Gravity 并调用 `0x48AB90`；该函数包含乘数 1.2、数学查表及整数转换。这进一步证明“直接使用 INI 的 40”缺少原引擎处理步骤。本次不据此宣称原版炮弹应精确飞行多少帧。

建议：先统一仿真和渲染的炮口、目标三维坐标，再还原有效初速、发射角、重力积分和触地判定。不要只把 1 改成 6 后继续沿用旧飞行时长。[原版 Gravity](../assets/settings/rules.ini#L615)、[Phobos WeaponType hooks](https://github.com/Phobos-developers/Phobos/blob/a3ad19985b37d353128007c9fdb04c14ef2fff35/src/Ext/WeaponType/Hooks.cpp#L5)、[Bullet gravity hooks](https://github.com/Phobos-developers/Phobos/blob/a3ad19985b37d353128007c9fdb04c14ef2fff35/src/Ext/Bullet/Hooks.cpp#L225)、[默认重力来源](https://github.com/Phobos-developers/Phobos/blob/a3ad19985b37d353128007c9fdb04c14ef2fff35/src/Ext/BulletType/Body.cpp#L5)

## 其他已确认的简化项

| 项目 | 当前情况 | 影响与证据 |
|---|---|---|
| 炮弹障碍与高差 | Shot 只有起终点和命中时间；到时按落点算伤害，没有逐帧碰撞和高度 | `Cannon` 的 SubjectToCliffs / SubjectToElevation / SubjectToWalls 全为 yes，当前未解析。[规则](../assets/settings/rules.ini#L18272)、[impact](../src/simulation/world.cpp#L407) |
| 地表速度与车身姿态 | 移动速度只取 type.speed；地图格子没有高程，渲染没有模拟驱动的俯仰/侧倾 | 原版有地表通行系数和 IsTilter。具体地形分类须随地图接入校准，不能笼统声称公路更快：当前原版规则 Clear / Road / Rough 的 Track 都是 100%。[移动](../src/simulation/world.cpp#L355)、[地图结构](../src/map_grid.h#L25)、[规则](../assets/settings/rules.ini#L22342) |
| 爆炸与炮口动画 | 固定 0.04 参考秒/图像帧，固定寿命；未使用 art 动画时序或 Normalized；爆炸 variant 始终默认 0 | 原版资源已加载不等于播放正确。AP 列出 S_CLSN16 / S_CLSN22，但当前活动路径一直选择小爆炸。GUNFIRE 有 4 帧，两个爆炸各 13 帧。[适配器](../src/presentation/world_view.cpp#L90)、[默认值](../src/weapon_system.h#L57)、[原版 AP](../assets/settings/rules.ini#L19388) |
| 运动目标 | 发射后始终落向开火瞬间的位置，无提前量 | 实测目标从 x=5 移至 x=7.125，第一发仍落在 x=5，目标未掉血。**未把“炮弹会打空”本身定为 bug**：原版普通炮弹也不是追踪导弹，提前量和命中规则需单独核对。 |

## 已有正确基础与不能贸然补的效果

- 平地直线 10 格：犀牛 **171**、灰熊 **151** 逻辑帧，符合现有 Speed 导入换算；应先修速度分配，不宜靠整体加速掩盖顿挫。
- ROT=5 的独立 90° 转向计时与 YRpp Facing 公式一致，当前从命令执行到首发为 **13 帧**。这只验证了转向计时基础，不证明原版所有开火门限都必须“完全转正”。
- ROF=65 / 60 目前按逻辑帧使用；在 60 逻辑帧/秒时是约 1.083 / 1 秒。不能将参考的 15 帧时钟误当成固定现实帧率。
- 犀牛和灰熊都为 **Accelerates=false**，增加通用起步惯性没有原版规则依据。
- 灰熊逻辑 ID 是 MTNK，但 `Image=GTNK`，它的炮口应读 art 的 GTNK（150,0,100）。当前解析正确；art 中名为 MTNK 的旧“Apocalypse tank”条目不能套给灰熊。
- 不应凭现代坦克印象加入夸张炮管后坐力。Phobos 文档明确说明其 TurretRecoil 实现是重新实现的效果，不能倒推原版 RA2 犀牛/灰熊默认应有相同行为。[Turret recoil](https://phobos.readthedocs.io/en/latest/Fixed-or-Improved-Logics.html#turret-recoil)

## 建议修复次序与验收

1. **先修可独立验证的移动和转向错误**：同帧跨段、炮塔受控归正、统一车身/炮塔/炮口坐标。保留现有总移动时间和装填时间。
2. **补行驶与作战状态**：行驶转弯轨迹、机会射击、追击时的炮塔跟踪；把原地急转和行驶拐弯区分开。
3. **还原炮弹模型**：有效初速、FLH、重力、高差、障碍与命中；随后接入原版炮口和爆炸动画的时序。
4. 用同游戏速度的纯净 RA2 1.006 录像核对：直行 10 格、45°/90°/180° 改向、转塔后移动、侧向移动射击、1/3/5 格静止射击、横向运动目标、墙和高地。记录坐标、炮塔角、首发帧、落地帧，而不只凭总耗时判断。

验证：本次临时诊断程序直接链接最新 `cmake-build-debug/libra2_simulation.a` 和 `libra2_core.a`；源文件位于 `build-core-review/tank_behavior_probe.cpp`，数据见 [CSV](calibration/tank-behavior-2026-10-12.csv)。既有 `rule_calibration_regressions`、`simulation_regressions`、`game_speed_regressions` 全部通过；它们覆盖基础时钟和规则，未覆盖上述观感问题。

对照版本：YRpp `b22f1860dd7b100cad57123d4ca5d459b9020d88`；Phobos `a3ad19985b37d353128007c9fdb04c14ef2fff35`；OpenRA `e180e3225f3930272dd451917467631618f8976f`；OpenRA/ra2 `61e24e3c1d7b586aa55a86096d29e1559aa9b994`。[OpenRA/ra2 犀牛规则](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/rules/soviet-vehicles.yaml)、[坦克武器规则](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/weapons/bullets.yaml)
