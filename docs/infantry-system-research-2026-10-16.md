# 步兵系统调研：原版规则、开源实现与项目接入

日期：2026-10-16。目标：为下一阶段实现步兵系统确定数据来源、模块边界和验收场景。延续项目的 RA2 1.006 行为还原目标；本轮完成静态源码与配置调研，没有运行原版、OpenRA 或进行原版逐帧测量。

后续进展：GI / 动员兵首版已接入，功能、操作和校准边界见 [实现说明](infantry-system-implementation.md)。本文保留调研时的项目状态和建议，以下素材表已更新为接入后的状态。

## 结论

第一批建议实现美国大兵 GI（E1）和动员兵（E2），完成兵营生产、选择、移动、射击、受击卧倒、死亡，以及 GI 部署和车辆碾压。先建立同格多步兵的占位与预留，再接入表现和战斗；现有车辆的一格一单位模型是主要阻碍。

原版 `rules.ini` 决定类型、武器和弹头，`art.ini` 决定步兵 SHP 动作布局及开火时机。OpenRA 适合参考子格占位、动作与表现分离、能力组合；YRpp / Phobos 适合补充原引擎的数据结构和硬编码行为。两者都不能直接替代纯净 RA2 1.006 的行为基准。

## 1. 证据范围与版本

| 来源 | 本次核对版本 | 能证明什么 / 使用边界 |
|---|---|---|
| 项目内原版配置 | 当前 `assets/settings/rules.ini`、`art.ini` | 本项目实际要读取的数据；参数存在不代表当前已经实现 |
| 原版游戏手册 | [RA2 手册镜像][manual] | 面向玩家的机制，如 GI 部署后不能移动；不能证明帧级时序 |
| OpenRA/ra2 | [61e24e3][ora-ra2] | RA2 模组的具体配置；有独立的平衡与实现取舍 |
| OpenRA 引擎 | [release-20231010 / dde3934][ora-engine] | 按模组 [mod.config][ora-version] 声明的版本检查实现；未验证模组运行兼容性，发现的配置键差异见下文 |
| YRpp | [a823ada][yrpp] | 尤里的复仇逆向接口和对象布局；不是公开的 RA2 原始源码 |
| Phobos | [614d7c9][phobos] | YR 引擎挂钩、修复与扩展；必须区分原有行为和新增开关 |
| WorldAlteringEditor | [67f7c01][wae] | 原版地图编辑工具中的步兵子格和绘制模型；不代表游戏运行时的寻路实现 |
| Chrono Divide | [官方 Mod SDK][cd-sdk] | SDK 说明主要兼容 RA2 1.006 模组、不兼容 YR 模组。本次核验的是 SDK 文档，未将其当成已审阅的开源游戏引擎 |

## 2. 原版步兵如何组织

### 2.1 类型、武器、画面是三层数据

以项目配置为准：

| 项目 | GI / E1 | 动员兵 / E2 |
|---|---|---|
| Image / Sequence | GI / GISequence | CONS / ConSequence |
| 生命 / 护甲 | 125 / none | 125 / flak |
| Speed / Cost | 4 / 200 | 4 / 100 |
| 生产前置 | GAPILE | NAHAND |
| 主武器 | M60：Damage 15、ROF 20、Range 4、弹头 SA | M1Carbine：Damage 15、ROF 25、Range 4、弹头 SA |
| 部署后武器 | Para：Damage 15、ROF 15、Range 5、弹头 SSA | 无 |
| 步行机制 | Walk Locomotor，MovementZone=Infantry | 同左 |
| 进入建筑 | Occupier=yes | Occupier=yes |
| 站立开火事件参数 | art 的 FireUp=2 | art 的 FireUp=6 |

来源：[E1](/Users/yiqunsong/projects/ra2-replica/assets/settings/rules.ini:3130)、[E2](/Users/yiqunsong/projects/ra2-replica/assets/settings/rules.ini:3221)、[三种武器](/Users/yiqunsong/projects/ra2-replica/assets/settings/rules.ini:16420)、[GI art](/Users/yiqunsong/projects/ra2-replica/assets/settings/art.ini:213)、[CONS art](/Users/yiqunsong/projects/ra2-replica/assets/settings/art.ini:121)。

GI 部署的增强来自射程、射速和弹头对护甲的倍率变化，不能只把 Damage 调高。手册明确描述部署后固定射击、再次部署命令收起；内部实现应保留部署过渡状态，不能把它当成 MCV 变建筑。[原版手册][manual]

### 2.2 同格多单位需要子格占位

步兵占位应包含 `Cell + SubCell`。YRpp 的 `CellClass` 有 `FindInfantrySubposition`、占位标记以及桥面相关的替代占位数据，表明步兵位置选择与普通整格占用不同。[YRpp CellClass][yr-cell]

三个步兵子格有两项交叉参考：OpenRA/ra2 配置列出三个步兵偏移；WorldAlteringEditor 的自动放置只选择 Bottom、Left、Right 三个位置。编辑器虽保留五项枚举和数组，但不能据此推导 RA2 正常移动允许五个步兵共享一格。[RA2 MapGrid][ora-grid]、[编辑器放置逻辑][wae-slots]

版本适配疑点：该模组写的是 `SubCellsOffsets`，声明依赖的 release-20231010 引擎字段却叫 `SubCellOffsets`。因此这里只把模组配置作为三子格的设计意图证据，未宣称这一 checkout 中的配置已成功生效；直接运行或移植配置前需要检查这个差异。[引擎字段][ora-engine-grid]

因此首版采用三子格模型；**原版运行时的精确偏移、优先选择顺序、对向会车及敌我混格规则仍需实测**。OpenRA 坐标、原版 lepton 和项目 milli-cell 不是同一单位，不直接复制偏移数字。

实现上不能只把 `occupant` 换成三个 ID：移动途中需要源子格、目标子格与预留；停车、改令、死亡必须释放正确的预留；车辆和建筑仍占整格。群体移动的终点分配也要允许一个格容纳多个步兵，否则外观和队形仍会像坦克。

### 2.3 SHP 动作按序列和方向寻址

GI 与动员兵使用二维 SHP。其基本序列在原版 art 中结构相同：

| 动作 | Start, Count, FacingMultiplier | 含义 |
|---|---|---|
| Ready / Guard | 0,1,1 | 各朝向的站立帧 |
| Walk | 8,6,6 | 每方向六帧行走 |
| Prone | 86,1,6 | 从爬行动作中取各方向的静止帧 |
| Crawl | 86,6,6 | 卧倒移动 |
| FireUp | 164,6,6 | 站立射击 |
| FireProne | 212,6,6 | 卧倒射击 |
| Down / Up | 260,2,2 / 276,2,2 | 趴下 / 起身 |
| Die1 / Die2 | 134,15,0 / 149,15,0 | 不按朝向分组的死亡动作 |

来源：[ConSequence](/Users/yiqunsong/projects/ra2-replica/assets/settings/art.ini:9612)、[GISequence](/Users/yiqunsong/projects/ra2-replica/assets/settings/art.ini:9890)。YRpp 的 `SubSequenceStruct` 对应 StartFrame、CountFrames、FacingMultiplier 和 Facing，支持这种数据解释。[结构定义][yr-sequence]

基本寻址为 `Start + directionIndex × FacingMultiplier + localFrame`，还要处理第四项固定方向。第三个数是方向组跨度，不是方向数量；例如 Prone 的 Count=1、跨度却为 6。正常动作可用八向显示，逻辑朝向继续保留项目的完整角度精度。

解析器要校验所有方向的帧范围，并处理 `Count=0` 的未提供序列。GI 的 `DeployedIdle=0,0,0` 不能触发除零或错误循环。固定方向的 Idle、重映射颜色、脚底锚点和阴影应在资源预览中分别核对。

**有两个不同的 FireUp：** `[GI] FireUp=2` 是发射事件参数；`[GISequence] FireUp=164,6,6` 是图像序列。CONS 的事件参数为 6，而序列 Count 也为 6，说明不能未经校准就按零基索引限制到 0～5。原版计数器的起点、更新顺序和循环边界必须验证。[art 字段说明](/Users/yiqunsong/projects/ra2-replica/assets/settings/art.ini:25)

射击事件应由逻辑帧驱动，画面读取同一动作起始帧；不能让渲染回调决定子弹生成，否则窗口帧率、离屏和动画跳帧会影响伤害。动作帧速也不统一：Phobos 整理的 YR 默认表中 Walk 为 3、FireUp 为 1，部分 Idle 还带 Normalized。这是兼容层的参考，不能直接换算成固定毫秒并宣称等于 RA2。[SequenceRates][phobos-rates]

### 2.4 卧倒同时影响动作、移动和受伤

需要区分受击引发的状态、状态持续时间、动作切换、移动速度和伤害倍率。`Crawls` 描述是否使用爬行动画，不能简单解释为是否会受惊或卧倒；`Fearless`、`Fraidycat` 也是独立的类型属性。[art 注释](/Users/yiqunsong/projects/ra2-replica/assets/settings/art.ini:25)、[YRpp 类型字段][yr-sequence]

原版配置的 `ProneDamage` 属于弹头：SA 为 70%，SSA 为 50%，注释默认值为 1.0。因此“所有步兵趴下都减伤一半”会直接改变 GI 与动员兵交战结果。[SA / SSA](/Users/yiqunsong/projects/ra2-replica/assets/settings/rules.ini:19166)、[弹头说明](/Users/yiqunsong/projects/ra2-replica/assets/settings/rules.ini:19102)

Phobos 将 YR 原有卧倒移速常量开放为设置，列出 Crawls / NoCrawls 的默认倍率为 0.67 / 1.5；OpenRA `TakeCover` 的默认移速倍率却为 50%。这些不能混为同一个原版结论。RA2 的恐惧阈值、衰减、起身时刻和准确移速留作实测项。[Phobos 卧倒设置][phobos-prone]、[OpenRA TakeCover][ora-cover]

### 2.5 部署、死亡、碾压和进驻各有独立规则

GI 原版 art 使用 `Deploy=300,15,0`、`Deployed=292,1,1`、`DeployedFire=315,6,6`，但 `Undeploy=276,2,2`。OpenRA/ra2 的收起动作则倒放部署帧 314～300，长度为 15。**本项目应读取自己的原版序列，不照抄模组的动画表。**[GISequence](/Users/yiqunsong/projects/ra2-replica/assets/settings/art.ini:9907)、[OpenRA GI 序列][ora-gi-sequence]

死亡表现还涉及弹头 `InfDeath`，不能把所有死亡都当成同一个爆炸。逻辑死亡时释放目标、占位和预留，再用独立效果播放尸体动画；这避免尸体继续挡路，也避免实体移除时动画消失。OpenRA 的 `WithDeathAnimation` 将死亡画面生成为 `SpriteEffect`，值得参考这种生命周期分离。[死亡效果实现][ora-death]

碾压需要同时检查车辆的碾压能力、步兵的可碾压属性、敌我关系和当前姿态。GI 的 `Deployer` 与 YR 的 `DeployedCrushable` 是不同属性，不能仅凭一个 `Crushable` 值决定所有姿态。[原版类型注释](/Users/yiqunsong/projects/ra2-replica/assets/settings/rules.ini:3037)、[YRpp 属性][yr-sequence]

E1/E2 的 `Occupier=yes` 是后续建筑进驻入口，需另做建筑容量、驻军归属、驻军武器和出入流程；不等于兵营生产或普通车辆乘载。第一批可暂不开放进驻命令，避免出现能够进入却无法正确战斗和退出的半套行为。

## 3. 开源项目值得借鉴的实现

| 机制 | 开源实现 | 对当前项目的建议 |
|---|---|---|
| 子格占位 | OpenRA `Mobile` 保留 From/To Cell 与 From/To SubCell；`Locomotor` 查询可用子格 | 独立占位接口，提供查询、预留、提交、释放；所有寻路和出厂调用走同一入口 |
| 步行通行 | RA2 模组的 foot locomotor 开启 SharesCell，独立配置地形速度 | 在现有地形/寻路基础上增加 Foot 能力；车辆曲线转弯与车距约束不直接套给步兵 |
| 动作表现 | `WithInfantryBody` 区分等待、移动、射击、闲置；射击通知处理更新顺序冲突 | 姿态和动作分开；逻辑事件决定动作起点，显示层只选序列并插值 |
| 卧倒 | `TakeCover` 处理伤害通知、持续时间、移速/伤害修正及序列前缀 | 借鉴职责拆分，数值与触发规则使用本项目兼容层 |
| GI 部署 | 条件控制移动、主副武器和不同表现组件 | 部署期间保留实体身份，切换能力和武器，不复用 MCV 类型转换 |
| 死亡 | 死亡通知生成独立 SpriteEffect | 将尸体表现与实体存活、碰撞彻底分开 |

来源：[Mobile][ora-mobile]、[Locomotor][ora-locomotor]、[foot 配置][ora-foot]、[WithInfantryBody][ora-body]、[TakeCover][ora-cover]、[GI 能力组合][ora-gi-rules]、[WithDeathAnimation][ora-death]。

应特别避免三类混用：

1. **通用引擎默认值与 RA2 模组意图混用。** OpenRA 默认网格含五个步兵子格，RA2 模组配置意图为三个；还需检查上文的配置键适配差异。[引擎 MapGrid][ora-engine-grid]、[模组 MapGrid][ora-grid]
2. **YR 扩展字段当成 RA2 原有字段。** Phobos 的 `BarracksExitCell`、可配置序列 Rate、`ProneSpeed` 是扩展入口；可以参考其说明的默认行为，不应要求原版 INI 含有这些字段。
3. **RA2 与 YR 行为相同的假设。** Phobos 文档明确指出 GI 类步兵的 AI 自动部署在 RA2 工作、在 YR 中损坏；SecondaryFire / SecondaryProne 动作也是 YR 新增。后续 AI 和特殊步兵实现要保留版本边界。[Phobos 说明][phobos-auto-deploy]

## 4. 当前项目的接入缺口

| 位置 | 已有基础 | 需要改动 |
|---|---|---|
| [definitions.cpp](/Users/yiqunsong/projects/ra2-replica/src/config/definitions.cpp:97) | 已枚举 InfantryTypes，读入基本规则 | groundMobile 目前只接受车辆；默认 SpeedType=Track；新增 Foot 能力、Secondary、步兵属性与 Sequence 定义 |
| [definitions.h](/Users/yiqunsong/projects/ra2-replica/src/config/definitions.h:11) | 武器、弹头、视觉定义 | VisualDef 以 VXL 部件为主；补 SHP 序列元数据、开火事件、ProneDamage、InfDeath，不以渲染代码硬编码兵种 |
| [world.cpp 占位](/Users/yiqunsong/projects/ra2-replica/src/simulation/world.cpp:150) | 地形、寻路、群组移动、预留 | occupied / reservations 为 Cell→单个 EntityId；World::occupant、终点分配、堵塞处理和出厂检测需要统一支持子格 |
| [world.cpp 生产](/Users/yiqunsong/projects/ra2-replica/src/simulation/world.cpp:319) | 经济、科技、生产进度、车辆出厂 | 当前强制 UNITTYPE + Vehicle；增加 InfantryType 生产与兵营出口策略 |
| [world.cpp 战斗](/Users/yiqunsong/projects/ra2-replica/src/simulation/world.cpp:1316) | 寻敌、瞄准、ROF、不可见弹丸基础 | 增加动作开火时机、副武器选择、受击姿态与死亡事件 |
| [world.cpp 命中](/Users/yiqunsong/projects/ra2-replica/src/simulation/world.cpp:1403) | 护甲倍率、范围伤害 | 当前零 CellSpread 仍按最小半径扫描实体；同格步兵要明确直接目标与范围伤害，避免误伤或漏伤 |
| [world_view.cpp](/Users/yiqunsong/projects/ra2-replica/src/presentation/world_view.cpp:53) | 仿真快照、显示插值 | 非建筑全部转成 RhinoUnitState；增加独立 InfantryView 和 SHP 绘制入口 |
| [unit_selection.h](/Users/yiqunsong/projects/ra2-replica/src/interaction/unit_selection.h:37) | 多选、框选、增减选择 | 接口依赖 RhinoUnitState；改为共享的可选单位视图，支持步兵与车辆混选 |
| [app_main_ui.cpp](/Users/yiqunsong/projects/ra2-replica/src/app_main_ui.cpp:51) | 车辆渲染缓存、场景交互 | 新增步兵资源缓存、点击命中、绘制排序与生产页；复用 SHP 解码和调色板 |

两个容易被基础演示掩盖的问题也要纳入验收：

- `InvisibleLow` 在原版声明受墙、悬崖和高差影响；目前不可见弹丸分支直接到达目标，绕过其他弹丸的障碍采样。步兵机枪不能因此穿墙。[原版弹丸](/Users/yiqunsong/projects/ra2-replica/assets/settings/rules.ini:18220)、[当前分支](/Users/yiqunsong/projects/ra2-replica/src/simulation/world.cpp:1436)
- 兵营出口不是战车工厂出门轨迹的简单缩放。Phobos 记录 YR 默认 GDIBarracks / NODBarracks 出口格偏移分别为 (1,2) / (2,2)；接入时还需核对本项目建筑原点、原版 ExitCoord、脚底坐标与遮挡顺序。[出口参考][phobos-exit]

### 4.1 assets_origin 素材核验补充

2026-10-16 根据用户补充的资源目录重新检查：**GI / CONS 主体和第一批视觉依赖已在 `assets_origin` 找到**。先前的资源缺口只适用于运行时 `assets`，不代表原始资源池缺失。首版实现已按项目目录规范复制所需文件，原件保持不变。

| 用途 | 原始素材 | 核验结果 / 运行时状态 |
|---|---|---|
| GI 主体 | [sprites/gi.shp](/Users/yiqunsong/projects/ra2-replica/assets_origin/sprites/gi.shp) | 78×66，744 帧；前 372 帧为动作区，后 372 帧全透明；已复制到 assets/infantry/allied/gi |
| 动员兵主体 | [sprites/cons.shp](/Users/yiqunsong/projects/ra2-replica/assets_origin/sprites/cons.shp) | 76×96，602 帧；前 301 帧为动作区，后 301 帧全透明；已复制到 assets/infantry/soviet/cons |
| 普通 / 升级图标 | [language](/Users/yiqunsong/projects/ra2-replica/assets_origin/language) 中的 giicon.shp、giuico.shp、e2icon.shp、e2uico.shp | 均为 60×48 单帧；运行时已有相同的 giicon，其余三项已复制。侧栏已改用正确的 E2ICON；升级逻辑尚未开放 |
| 机枪火光 | [sprites](/Users/yiqunsong/projects/ra2-replica/assets_origin/sprites) 中的 mgun-n/ne/e/se/s/sw/w/nw.shp | 八方向，每文件 18×17、6 帧；已复制到 assets/effects/infantry |
| 命中特效 | [piffpiff.shp](/Users/yiqunsong/projects/ra2-replica/assets_origin/sprites/piffpiff.shp)、[piff.shp](/Users/yiqunsong/projects/ra2-replica/assets_origin/sprites/piff.shp) | 均为 28×28，分别 12 / 7 帧；SA / SSA 当前配置使用 PIFFPIFF，PIFF 作为相关资源列出 |
| 调色板 | [palettes](/Users/yiqunsong/projects/ra2-replica/assets_origin/palettes) 中的 unittem.pal、unitsno.pal、uniturb.pal、cameo.pal、anim.pal | 每份 768 字节；运行时对应文件全部已存在且逐字节一致，无需重复复制 |

使用项目原有 `ShpTsFile::load / decodeFrame` 编译临时探针，完整解码上述 **16 个 SHP、1417 帧，全部通过**。逐项检查 GISequence / ConSequence 共 43 项定义，方向跨度均未超出各自主体动作区，零长度 DeployedIdle 单独处理。原始目录与运行时配置的 GI、CONS、两份 Sequence、E1、E2 及所查武器/弹头段一致。

生成并检查了 [动作预览](/Users/yiqunsong/projects/ra2-replica/build/research/infantry/assets/infantry-contact-sheet.png)；它使用现有调色板代码和蓝/红队色展示动作样本，不代表已经校准原版播放速度。[核验清单](/Users/yiqunsong/projects/ra2-replica/build/research/infantry/assets/manifest.json) 记录源路径、完整 SHA-256、尺寸、帧数和序列范围。两份主体 SHP 的后半段均无非透明像素，后续渲染不能假定这些帧能提供独立阴影。

另外已定位 engineer、snipe、adog、dog、spy、shk、flakt、deso、rock、tany、seal、ivan、cleg、yuri、trst 等主体 SHP。其中 JUMPJET 的 Image 为 ROCK，GHOST 为 SEAL，TERROR 为 TRST，应按规则 Image 字段查找，而非直接用单位 ID 拼文件名；本轮未对这些后续兵种做完整逐帧解码。

音频仍有缺口：原始目录有 sound.ini 的 GI / Conscript 配置，但 audio 目录只有 15 个菜单类 AUD；未找到所查 GI / 动员兵语音、枪声样本或 audio.bag / audio.idx。第一批画面和动作资源已可接入，声音资源需后续补齐。

## 5. 建议的实施顺序

### 阶段 A：资源与定义

从已核验的原始素材复制 `gi.shp`、`cons.shp` 与缺少的图标、效果资源到运行时目录，复用已有调色板；帧数清单与动作预览已完成。扩展 INI 编译器，解析原版 Sequence、开火事件、主副武器和步兵类型能力。所有八向动作、固定方向 Idle、零长度序列和帧越界应在这一阶段可诊断。

### 阶段 B：占位与步行

为地面移动增加三子格占位策略，保留整格占位策略。源/目标子格、预留和释放统一管理；行走使用 Foot 通行能力；群组终点按子格容量分配。先通过三人同格、四人溢出、对向拥堵和混合车辆交通，再接入完整战斗。

### 阶段 C：GI / 动员兵战斗与画面

增加 InfantryView / SHP renderer，复用调色板、队色和现有场景深度机制。姿态（站立、趋向卧倒、卧倒、起身、部署过渡等）与动作（待机、移动、射击）分开管理。射击事件、命中、卧倒与部署转换由仿真逻辑推进；显示层不产生游戏事件。

GI 部署切换 M60 / Para，按原版序列收放；补齐 ProneDamage、InfDeath、普通死亡和碾压。只影响画面的闲置动作随机数应与游戏逻辑随机数隔离，这是本项目的确定性设计建议。

### 阶段 D：生产和操作形成完整流程

支持 GAPILE / NAHAND 的 InfantryType 队列、出口、集合点、出口阻塞重试与费用结算；选择、框选、编组、移动和攻击改为按能力操作。第一批验收对象仍只选 E1/E2，避免特殊武器同时扩大范围。

随后按能力扩展：工程师与建筑交互、GI/动员兵进驻；再做军犬扑击、间谍伪装、防空步兵、磁暴步兵、辐射工兵及飞行步兵。它们分别需要目标交互、特殊攻击或不同运动层，不能仅新增一张贴图和数值。

## 6. 验收与原版校准

| 场景 | 必须验证的结果 |
|---|---|
| 三个步兵进入同格，第四个加入 | 子格不重叠，第四个等待或落到可用位置；不覆盖已有实体 |
| 多个单位争抢一个目标子格 | 预留结果确定，取消、死亡和换令不留下永久堵塞 |
| 步兵与坦克混合移动、碾压 | 保留车辆原有通行行为；关系与姿态正确影响碾压 |
| 同格步兵遭直接攻击 / 范围攻击 | 零扩散只处理直接目标；范围伤害每实体结算一次，依据原版 CellSpread 注释 |
| GI / CONS 开火与中途改令 | 分别核对 FireUp=2 / 6 的实际事件语义；不漏发、不重复发射 |
| 站立、趴下、爬行、起身 | 图像、速度、伤害状态同步；SA / SSA 卧倒倍率分别正确 |
| GI 部署、收起、死亡、中途改令 | 正确序列和武器；不沿用 MCV 转换；中断时无残留动作/预留 |
| 兵营出口被堵住后放开 | 正确等待、出兵和走向集合点；不重扣费用、不重复出生 |
| 八方向与前后遮挡 | 帧合法、脚底不跳动，队色、阴影和建筑遮挡正确 |
| 同一命令记录以不同显示帧率播放 | 逻辑状态散列与开火时刻一致，30/60/144 Hz 不改变战斗 |

需要另用纯净 RA2 1.006、相同游戏速度和受控地图校准：子格偏移/优先级，直行与斜行步速，转向和停走行为，恐惧触发/恢复，开火计数器边界，部署中命令处理，碾压距离及兵营出口。记录游戏版本、配置哈希、命令帧和事件帧；本报告不把开源参考值冒充这些实测结果。

调研阶段只更新文档，并在忽略的 build 目录生成探针、清单与预览；当时未改动游戏实现或原始素材。后续实现及完整构建、测试记录见 [实现说明](infantry-system-implementation.md)。

[manual]: https://manualmachine.com/gamespc/commandconquerredalert2/1117998-user-manual/
[ora-ra2]: https://github.com/OpenRA/ra2/tree/61e24e3c1d7b586aa55a86096d29e1559aa9b994
[ora-engine]: https://github.com/OpenRA/OpenRA/tree/dde39344a0f25af15b039e6a2e4dbf046e624c04
[ora-version]: https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mod.config#L7
[yrpp]: https://github.com/Phobos-developers/YRpp/tree/a823ada387386a18ddadf41b4d7b46f295f94fdb
[phobos]: https://github.com/Phobos-developers/Phobos/tree/614d7c9fa726a6bea79c3931de45637dc9934e28
[wae]: https://github.com/CnCNet/WorldAlteringEditor/tree/67f7c01fd6a7a6f36026c0c4f0b79b5c08222eae
[cd-sdk]: https://github.com/chronodivide/mod-sdk#introduction
[yr-cell]: https://github.com/Phobos-developers/YRpp/blob/a823ada387386a18ddadf41b4d7b46f295f94fdb/CellClass.h#L340
[yr-sequence]: https://github.com/Phobos-developers/YRpp/blob/a823ada387386a18ddadf41b4d7b46f295f94fdb/InfantryTypeClass.h#L9
[ora-grid]: https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/mod.yaml#L189
[wae-slots]: https://github.com/CnCNet/WorldAlteringEditor/blob/67f7c01fd6a7a6f36026c0c4f0b79b5c08222eae/src/MapEditorLibrary/Models/MapTile.cs#L312
[phobos-rates]: https://github.com/Phobos-developers/Phobos/blob/614d7c9fa726a6bea79c3931de45637dc9934e28/src/Utilities/SequenceRates.h#L14
[phobos-prone]: https://github.com/Phobos-developers/Phobos/blob/614d7c9fa726a6bea79c3931de45637dc9934e28/docs/Fixed-or-Improved-Logics.md#prone-speed-customization
[ora-cover]: https://github.com/OpenRA/OpenRA/blob/dde39344a0f25af15b039e6a2e4dbf046e624c04/OpenRA.Mods.Common/Traits/Infantry/TakeCover.cs#L19
[ora-gi-sequence]: https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/sequences/allied-infantry.yaml#L66
[ora-death]: https://github.com/OpenRA/OpenRA/blob/dde39344a0f25af15b039e6a2e4dbf046e624c04/OpenRA.Mods.Common/Traits/Render/WithDeathAnimation.cs#L73
[ora-mobile]: https://github.com/OpenRA/OpenRA/blob/dde39344a0f25af15b039e6a2e4dbf046e624c04/OpenRA.Mods.Common/Traits/Mobile.cs#L250
[ora-locomotor]: https://github.com/OpenRA/OpenRA/blob/dde39344a0f25af15b039e6a2e4dbf046e624c04/OpenRA.Mods.Common/Traits/World/Locomotor.cs#L301
[ora-foot]: https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/rules/world.yaml#L23
[ora-body]: https://github.com/OpenRA/OpenRA/blob/dde39344a0f25af15b039e6a2e4dbf046e624c04/OpenRA.Mods.Common/Traits/Render/WithInfantryBody.cs#L139
[ora-gi-rules]: https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/rules/allied-infantry.yaml#L148
[ora-engine-grid]: https://github.com/OpenRA/OpenRA/blob/dde39344a0f25af15b039e6a2e4dbf046e624c04/OpenRA.Game/Map/MapGrid.cs
[phobos-auto-deploy]: https://github.com/Phobos-developers/Phobos/blob/614d7c9fa726a6bea79c3931de45637dc9934e28/docs/Fixed-or-Improved-Logics.md#auto-deploy-for-gi-like-infantry
[phobos-exit]: https://github.com/Phobos-developers/Phobos/blob/614d7c9fa726a6bea79c3931de45637dc9934e28/docs/Fixed-or-Improved-Logics.md#custom-exit-cell-for-infantry-factory
