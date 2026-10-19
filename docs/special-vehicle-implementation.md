# 坦克杀手命中效果、光棱坦克与幻影坦克

后续：2026-10-18 已接入磁能坦克 TTNK 及磁暴乘员 IFV，规则、充能交互和验证见 [步兵接入记录](infantry-system-implementation.md#磁暴步兵与磁能坦克2026-10-18)。

后续：2026-10-18 已接入 V3 火箭车、可拦截的独立火箭和辐射乘员 IFV，见 [实现记录](infantry-system-implementation.md#辐射工兵与-v3-火箭车2026-10-18)。

更新：2026-10-17。沿用 `assets_origin` 原始规则、体素、SHP 和剧场调色板，不改写 `rules.ini` / `art.ini`。

## 已完成

| 对象 | 行为 |
|---|---|
| 坦克杀手 TNKD | 修复 SABOT → UltraAP → S_CLSN30 的绘制缺口。原来的弹头事件存在，但运行资源只有 S_CLSN16 / 22，未找到 S_CLSN30 时绘制被跳过。现已原样复制 18 帧爆炸，按实际弹丸命中位置和仿真帧播放 |
| 光棱坦克 SREF | 使用 Weapon1=Comet，HP 150、价格 1200、速度 4、射程 10、伤害 100、ROF 100。普通光束随所属阵营队色显示，持续 LaserDuration=15 帧；命中后按 LargeCometP 发出五条 CometFragment，伤害 30、范围 3 格 |
| 幻影坦克 MGTK | 使用 RTNK 体素、MirageGun 热能武器，HP 200、价格 1000、速度 7、射程 7、伤害 100、ROF 70；驻停后伪装成树，移动时解除，停止后恢复同一种树形。保留载具实体、生命、占位和操作 |

两种新坦克通过 GAWEAP 生产，并要求 GATECH；费用、国家、队列、出厂和混选使用现有系统。坦克杀手保留德国限制与雷达前置。默认 Sandbox 免费且放宽科技，关闭后按原版费用和前置执行。

普通演示在盟军开局增加一辆光棱、一辆幻影和一辆坦克杀手。也可以从盟军载具侧栏生产。左键选择 / 混合框选、左键移动 / 攻击、S 停止，沿用原有操作；幻影无需部署按键。

## 光束、分束和炮塔

`WeaponDef` 保留 IsLaser、IsHouseColor、LaserDuration、LaserInnerColor；`ProjectileDef` 保留 ShrapnelWeapon / ShrapnelCount。光束沿现有即时武器碰撞路径结算，不生成炮弹贴图。仿真输出束的起终点、实际高度、阵营及寿命，绘制层使用队色、亚像素覆盖率与背景颜色混合，不计算伤害。

SREF 枪口使用 art 的 Weapon1FLH=48,0,184。主束按弹头装甲倍率结算，命中单位后发出五条分束；直接打建筑或空地不触发分束。附近敌方地面对象按距离与 ID 选取不同目标，不足五个时补固定随机状态生成的落点。分束从主束实际命中点发出，使用自己的弹头与碰撞配置，不再连锁生成下一轮分束。随机状态和新投射物纳入重放哈希。

修复了共用即时弹道的一种退化情况：光棱分束从贴地的前一次命中点出发时，沿平地接触地面不会马上在起点引爆；仍检查墙、悬崖、真正进入地表以及最终落点。

读取 IsChargeTurret 与 TurretCount=4，使用 `sreftur`、`sreftur1`、`sreftur2`、`sreftur3` 四套原版 VXL / HVA。每次开火后按剩余 ROF 从 3 → 2 → 1 → 0 恢复，停止射击不重置冷却或炮塔动画。

### 首轮光束外观复核（绘制方式已由下文替换）

首版使用 5 / 2.4 / 0.8 像素的三层矩形，中心硬编码纯白，普通透明混合会把彩色边缘压暗；交叉的光束还会写入深度并相互遮挡。结果更像一根细白线套着蓝边，不能仅凭“有光束、数量正确”的旧测试判断接近原版。

本次重新检查原版 Comet / CometFragment 配置，参考 [IsHouseColor 的颜色规则](https://modenc.renegadeprojects.com/IsHouseColor)、[YRpp 的 LaserDrawClass 属性](https://github.com/Phobos-developers/YRpp/blob/master/LaserDrawClass.h)及 [Phobos 对颜色 / 厚度的处理](https://github.com/Phobos-developers/Phobos/blob/develop/src/Ext/Techno/Hooks.WeaponEffects.cpp)。同时在 [tgSacred 的原版 YR 测试录像约 9:59](https://www.youtube.com/watch?v=yxj9ZQV56xg&t=599s)暂停、逐帧查看了射击画面。官方早期宣传图与塔的支援光束没有用作正式版坦克的像素标定。

修正内容：

- 去掉额外白色芯线，使用同一队色的中心 / 中间 / 外缘亮度；主束和分束沿用同一默认颜色模式，没有依据兵种名称额外改色。
- 当时将厚度近似为五条互不重叠的像素带，横向强度 1 / 0.5 / 0.25；使用加亮混合，保留场景深度检查，关闭光束自身的深度写入，让交叉光束叠加。结束后恢复渲染状态。
- 光束亮度在 LaserDuration=15 内衰减；敌方束色与敌方体素共用同一套 HouseColor 选择，取消固定红色。
- 在武器实际发射帧创建主束，在分束创建帧显示分束，修复原来等到伤害投射物更新后才显示而晚一帧的问题。复用同一段碰撞检测裁切束端；视觉检测只操作临时副本，不提前伤害或消耗随机状态。伤害、ROF、分束数量维持原行为。

此处的横向强度和混合方案仍是参考资料与实机画面的兼容近似，未声称恢复原版 16 位光栅器；YR 录像也不能代替纯净 RA2 1.006 的同场景逐像素 / 逐帧对照。修改前后同场景对比位于 `build/prism-beam-review/comparison.png`。

### 开源引擎源码复核（2026-10-17）

调研阶段沿实际调用链检查源码，没有运行这些外部引擎；后续实际修正见下节。源码副本和提交信息位于忽略目录 `build/research/prism-engines/`。

| 项目 | 固定版本 | 参考范围 |
|---|---|---|
| OpenRA/ra2 | `61e24e3c1d7b586aa55a86096d29e1559aa9b994` | 光棱坦克的具体武器配置 |
| OpenRA 引擎 | `dde39344a0f25af15b039e6a2e4dbf046e624c04`（release-20231010） | 按 RA2 模组 mod.config 指定版本追踪 LaserZap → BeamRenderable → DrawLine；另与 `e180e3225f3930272dd451917467631618f8976f` 对照，相关绘制方式没有变化 |
| OpenTS | `8ca7c7c84ba742109d30cc92d9d25c1c7f337598` | TS 引擎逆向重建的激光与软件像素线；没有 RA2 的 IsHouseColor 分支，不能直接当成光棱坦克实现 |
| Phobos / YRpp | `c265252b21961345bfaebd3aa928fb721d166d74` / `a823ada387386a18ddadf41b4d7b46f295f94fdb` | YR 原引擎接口和开源补丁；不属于完整独立引擎，补丁公式也不等于未修改原版算法 |

**OpenRA：一根队色直线，默认没有第二层辉光。**

[RA2 模组 Comet 配置](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/weapons/zaps.yaml#L133)使用普通 `LaserZap`，不是模组中的 `ArcLaserZap`：Width=50、Duration=15、UsePlayerColor=true。Width 是 OpenRA 世界长度，不是 50 个屏幕像素；[BeamRenderable](https://github.com/OpenRA/OpenRA/blob/dde39344a0f25af15b039e6a2e4dbf046e624c04/OpenRA.Mods.Common/Graphics/BeamRenderable.cs#L60)投影后才得到屏幕线宽。

[LaserZap](https://github.com/OpenRA/OpenRA/blob/dde39344a0f25af15b039e6a2e4dbf046e624c04/OpenRA.Mods.Common/Projectiles/LaserZap.cs#L190)逐 tick 将 alpha 线性减到零；SecondaryBeam 默认 false，Comet 没有开启。[DrawLine](https://github.com/OpenRA/OpenRA/blob/dde39344a0f25af15b039e6a2e4dbf046e624c04/OpenRA.Game/Graphics/RgbaColorRenderer.cs#L58)生成同色的两个三角形，颜色预乘 alpha，使用默认 Alpha 混合，没有硬编码白芯、横向强度梯度或加法混合。它还默认更新枪口位置并跟踪目标，而本项目保存发射时的两个端点。这些是 OpenRA 的实现取舍，不能自动视为原版行为。

该版本 Comet 使用 SpreadDamage，相关武器配置没有 CometFragment / 五条分束；它适合参考绘制组织，不能替代本项目原版规则。光棱塔 PrismShot 的 Width=70 也不能拿来当坦克束宽。

**OpenTS：带深度检查的抗锯齿像素线，有独立的内外颜色。**

[LaserDrawClass::Draw_It](https://github.com/OpenTS-Developers/OpenTS/blob/8ca7c7c84ba742109d30cc92d9d25c1c7f337598/code/laser.cpp#L149)绘制一根 InnerColor 核心线；只有 OuterColor 非黑时，才按八方向偏移表增加两根一像素外线。偏移表并非始终关于核心对称，不能用几何上更平滑的宽矩形替换后仍声称逐像素一致。内部强度按寿命插值；普通非闪烁外线使用独立强度参数。

更重要的是底层 [DSurface::Draw_Depth_Antialiased_Line](https://github.com/OpenTS-Developers/OpenTS/blob/8ca7c7c84ba742109d30cc92d9d25c1c7f337598/code/dsurface.cpp#L1137)：逐像素步进、取相邻像素覆盖权重，用三次曲线查表，再按指定 RGB 通道与背景颜色混合，写回 16 位颜色。它不是统一的 `背景 + 队色 × alpha` 加法，激光调用还关闭深度写入。因而，上一轮“使用加法混合才接近原版”的判断只能作为当前近似选择，不能作为原引擎已确认事实。

**Phobos：队色厚束按颜色衰减，但修改了原引擎。**

[模拟发射代码](https://github.com/Phobos-developers/Phobos/blob/c265252b21961345bfaebd3aa928fb721d166d74/src/Ext/Bullet/Body.cpp#L209)在队色模式下使用所属 House 的 LaserColor，外色和外色随机范围传黑色，并设置 IsHouseColor；[LaserThickness 默认值](https://github.com/Phobos-developers/Phobos/blob/c265252b21961345bfaebd3aa928fb721d166d74/src/Ext/WeaponType/Body.h#L208)为 3。[厚度补丁](https://github.com/Phobos-developers/Phobos/blob/c265252b21961345bfaebd3aa928fb721d166d74/src/Misc/Hooks.LaserDraw.cpp#L18)使用 `m(i) = (1 - i/T) × (1 - 1/T)^i` 缩放 RGB。T=3、i=0/1/2 时分别约为 1 / 0.444 / 0.148，与本项目上一版暂定的 1 / 0.5 / 0.25 不同；i 是补丁里的厚度迭代量，不能直接断言对应本项目某一像素带。

[YRpp 接口](https://github.com/Phobos-developers/YRpp/blob/a823ada387386a18ddadf41b4d7b46f295f94fdb/LaserDrawClass.h#L19)还记录 Thickness、IsHouseColor、IsSupported 和强度起止值，IsSupported 的注释提示普通与支援束亮度不同。但构造函数实际跳入游戏二进制，没有公开完整 RA2 光栅器。不能把 Phobos 的修复公式标成 RA2 1.006 原公式，也不能仅凭字段注释完成所有亮度标定。

调研结论：固定五条色带、固定横向比例和统一加法混合都缺少原版对应依据。实现应把屏幕像素线、覆盖率、颜色模式和时间强度分开，避免继续凭观感叠加白芯或宽光晕；优先校准队色模式的实际亮度、斜线抗锯齿和末期消退。OpenRA 可作为简单实现对照，OpenTS 可解释同系列的像素混色方式，Phobos 可提供队色厚束的可读参考；最终仍应以 RA2 1.006 的同场景、八方向、连续 15 帧截图决定参数。分束逻辑继续以 `assets_origin` 的 Comet / CometFragment 为准。

### 当前光束绘制修正

取消五条固定色带和加法混合，改为单个四边形内逐片元计算线条与端点覆盖率。线宽按 OpenRA Comet 的 Width=50、[等距地图 TileScale=1448](https://github.com/OpenRA/OpenRA/blob/dde39344a0f25af15b039e6a2e4dbf046e624c04/OpenRA.Game/Map/MapGrid.cs#L137)及屏幕格宽投影：`width = 50 × tileWidth / 1448`，60 像素格宽对应约 2.072 像素。抗锯齿覆盖以实际帧缓冲的一像素计算，不随地图缩放扩大或缩小；0.5 倍缩小时仍保留亚像素覆盖，2 倍放大时不会产生两像素的额外模糊。

颜色沿用所属 House 的队色，保留 RGB565 的源颜色精度，不使用体素的地图亮度倍率，也不加入白芯。只混合源颜色非零的通道，零通道保持背景，结束后恢复全部通道写入。使用 `SRC_ALPHA / ONE_MINUS_SRC_ALPHA`；交叉束向各自颜色混合，不会加法累积成白色。尝试根据 IsSupported 注释直接把现有体素队色减半时，截图出现暗蓝色实线，说明不能把该字段说明直接套到现有 RGB；最终没有加入此项未经校准的减半。

寿命仍由原版 LaserDuration=15 决定。先计算线性生命强度 `life = 1 - age / duration`，再将 `coverage × life` 送入与 OpenTS 梯度表同形的三次响应 `F(x) = ((2x - 1)^3 + 1) / 2`。这里用连续浮点近似查表，不是原软件光栅器的整数步进重现；开火时保持队色，随后和背景逐渐混合，到期移除。主束和分束共用此路径。

深度继续按两端的逻辑位置插值并测试场景遮挡，关闭光束深度写入；同时显式初始化普通片元的 gl_FragDepth，避免着色器只在逻辑深度分支赋值所留下的未定义值。光束结束后恢复深度、颜色写入和标准混合状态，下一次普通贴图绘制切回纹理模式。每束由五次 draw 降为一次 draw。

当前有明确源码依据的兼容基线，但不是 RA2 1.006 的逐像素复刻：线宽来自 OpenRA 的实现取舍，三次响应参考 TS 同系列像素线，完整 RA2 队色厚束的整数光栅、支援增亮和背景 RGB565 量化仍待原版逐帧比对。本次不采用 Phobos 改写后的厚度公式冒充原版。

同场景修改前后图为 `build/prism-line-fix/comparison.png`，原始帧保留在同目录。`beam-atlas.bmp` 是真实 GPU 输出，前三行分别为 age=0 / 7 / 14 的八方向，末行为八种颜色；传入截图路径运行图形回归时生成。

## 幻影伪装

读取 DefaultMirageDisguises 的 TREE01..04，创建时以实体 ID 稳定选择一种树形，移动后不会反复变换树种。三种剧场分别加载 `.tem` / `.sno` / `.urb` 原版树；主体使用地形调色板，第二帧用作黑色半透明树影，不能把索引 1 当作普通颜色绘制。树形按下述序列偏移定位，并随坦克所在地形高度升降。

逻辑伪装和开火显形分开：MirageGun 的 DisguiseFakeBlinkTime=15 仅暂时显示车体，不删除伪装状态，武器效果为原版 VTMUZZLE 与 IRONFX。己方单位还交替显示树和车体，便于辨认；当前兼容参数为树形 15 帧、车体 15 帧，总周期 30 个游戏帧，尚未获得纯净 RA2 的周期测量。计时起点和开火优先级见下文。树形下仍能选择、显示原版载具血条及下达命令。

新自动索敌会跳过未识破的伪装；移动时可被发现，已经锁定的目标在重新伪装后继续被追踪。附近地面敌步兵触发 InfantryBlinkDisguiseTime=20 的逻辑暴露，DetectDisguise 单位按 DetectDisguiseRange（缺省回退 Sight）识破。未识破的敌方树不提供普通攻击命中框；开火短暂显形、被识破或 AttackCursorOnDisguise=yes 时恢复。强制攻击地面仍可伤害该处实体。

### 幻影渲染源码复核与修正（2026-10-17）

本次确认最大的外形错误来自模型组成：原版 `rules.ini [MGTK]` 为 `Image=RTNK`、`Turret=no`，但运行配置同时加载了 `rtnk`、`rtnktur`、`rtnkbarl`，把闲置的旧炮塔和炮管叠到已经完整的 RTNK 车体上。OpenRA/ra2 的 [MGTK 定义](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/rules/allied-vehicles.yaml#L435)采用 AttackFrontal 与单个 WithVoxelBody；[体素序列](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/sequences/voxels.yaml#L148)只有 `idle: rtnk`，没有炮塔或炮管。这与原版规则一致，修正后只使用 `rtnk.vxl / rtnk.hva`。

运行配置清空了多余部件，定义编译也对载具的 Turret=no 清理炮塔 / 炮管引用，防止旧配置或留存文件再次拼出错误模型。原始闲置文件不删除，其他真正有炮塔的载具仍正常加载。枪口、攻击方向继续由原版 `PrimaryFireFLH=130,0,80` 和无炮塔车身朝向决定。

树形参考 OpenRA 的 [WithMirageSpriteBody](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/OpenRA.Mods.RA2/Traits/Render/WithMirageSpriteBody.cs)：伪装使用目标树的实际序列和地形调色板，车体与树形互斥绘制。[树序列](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/sequences/defaults.yaml#L251)规定 ShadowStart=1、Offset=-1,-16,16。本项目此前忽略了该偏移；现按 60 像素格宽应用 X=-1、Y=-16 和深度偏移 16，并保留完整 SHP 画布及两个帧各自的原始偏移。树影改为 50% 黑色掩码，检查场景深度但不写入深度；主体使用地形调色板，不参与队色重映射。

选择显示也按同一载具处理：删除树形血条额外的 -35 像素偏移，树形和车体共用随地形高度升降的血条锚点；静止在同一位置时不再因形态切换而跳动。树形鼠标范围覆盖偏移后的树冠，并随缩放和地形高度调整；未识破敌军仍没有普通攻击命中框。

OpenRA 的 [Mirage 逻辑](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/OpenRA.Mods.RA2/Traits/Mirage.cs)使用自己的初始 / 显形延迟，不能整套替换 RA2 的伪装机制。Phobos 的 [DisguiseBlinkingVisibility 接口与原引擎挂钩](https://github.com/Phobos-developers/Phobos/blob/c265252b21961345bfaebd3aa928fb721d166d74/src/Ext/Techno/Hooks.cpp#L625)以及[文档](https://phobos.readthedocs.io/en/build-47/New-or-Enhanced-Logics.html#disguise-logic-additions-disguise-based-movement-speed-disguise-blinking-visibility)确认己方识别闪烁有原版依据，因此本次保留，不因 OpenRA 的差异而删除；当前 15 帧交替周期仍是待原版逐帧校准的表现参数。开火视觉闪现、逻辑识破和移动解除伪装没有混为同一逻辑状态。

新增 GPU 回归在三个剧场、八个方向绘制真实 RTNK，检查改变炮塔角度不会改变车体像素；对全部 12 份原版树形检查画布位置、队色不影响像素及 SHP 第二帧的阴影强度。原版车体 / HVA 与 12 份树形共 14 个使用文件逐字节核对 `assets_origin`，全部一致。实际场景 frame 20 / 40 的血条像素行一致，验证形态切换时锚点稳定。截图对比和三剧场八方向体素图位于 `build/mirage-fix/comparison.png`、`mirage-body-atlas.png`。本次是源码与项目实机渲染验证，未运行原版 RA2 做逐像素比对。

### 幻影计时与开火显形修正（2026-10-17）

进一步核对 `assets_origin` 的原版规则及 [YRpp TechnoClass 的伪装字段](https://github.com/Phobos-developers/YRpp/blob/a823ada387386a18ddadf41b4d7b46f295f94fdb/TechnoClass.h#L641)：原引擎分别保存 DisguiseCreationFrame、InfantryBlinkTimer 和 DisguiseBlinkTimer。Phobos 的可见性挂钩仍调用游戏二进制，没有公开己方闪烁的完整时间公式。OpenRA 的 InitialDelay / RevealDelay 各 25 tick 是它自己的逻辑解除伪装机制，不能拿来替换原版仅视觉显形的 15 帧。已查看的 YR 测试录像不能确认纯净 RA2 1.006 版本及独立闲置周期，因此没有用录像推导新的周期常数。

本轮修正的是项目中能够确定的计时和状态混用问题：

- 仿真分别输出伪装开始帧、开火显形截止帧和步兵识破截止帧，删除将两种定时器合并的 `disguiseBlink` 标志。识破只使用逻辑定时器和探测单位；视觉闪烁不改变占位、身份或自动索敌。
- 每次从移动恢复伪装时记录本车的开始帧，取消全体车辆共用的 `world.tick()/15` 相位。新生成或刚停稳的车先完整显示一个树形阶段。
- 开火在实际 ShotSnapshot / VTMUZZLE 创建的游戏帧显形，覆盖 age=0..14；age=15 恢复树形。停止命令不提前清除定时器，重叠射击延长到最新有效截止帧。原始 DisguiseFakeBlinkTime=0 不创建视觉显形。
- 修复事件动画沿用移动插值 `age-1` 的问题：原版 VTMUZZLE 共 11 帧，原来 frame 0 连续占用两个游戏帧，整段晚一帧结束。事件动画现在从实际创建帧按 age 播放，枪口恰好显示 11 游戏帧，随后车体仍保留到独立的 15 帧显形期限。IRONFX 同样按实际命中创建帧播放，保留原版 50% 半透明；共用的其他枪口 / 命中动画也消除了同一个事件计时偏移，投射物移动插值保持原处理。
- 计时结束后从完整树形阶段继续己方识别循环，避免己方闪烁接续车体阶段，造成射击显形看起来多持续一段时间。此项是当前兼容策略，不是已证实的 RA2 原公式。
- `makeWorldView` 支持显式观察方，默认仍为玩家 0。同一实体可以同时验证己方和敌方外观；己方闲置识别闪烁不会向敌方泄漏，开火显形会向敌方提供普通攻击命中框，但不会让其自动获取目标。

所有期限以游戏帧计算。15 游戏帧在多人速度 2（15 FPS）下是 1 秒，在速度 6（60 FPS）下是 0.25 秒；不能把规则中的 15 直接换成固定的现实时间。暂停和渲染刷新不推进这些定时器。当前保留树形 15 / 车体 15 的兼容周期；相位、重入和优先级已经验证，精确原版周期及原版是否在开火后重启闲置相位仍需纯净 1.006 同场景测量。

逻辑 / 表现测试逐帧覆盖开火 0..14 / 15 边界、错时生成两辆车、移动后重新伪装、重复射击、零显形时间、步兵识破与视觉显形分离、手动攻击命中框、敌方不自动锁定，以及 30 / 60 / 144 Hz 渲染与 15 / 60 游戏 FPS 的时序一致性和重放哈希。事件动画只使用实际游戏帧，不把不限速模式下表示“当前姿态”的 interpolation=1 当成额外一帧；额外检查有限速度 / 单人不限速模式的完整 11 帧 VTMUZZLE 和共用 GUNFIRE 回归。

图形回归另外连续读回 66 游戏帧、两种观察方的实际 OpenGL 像素，与树形和完整车体参考图比较，并检查开火当帧确实绘制原版 VTMUZZLE。传入截图路径会生成 `mirage-timing-atlas.bmp`（11 个关键时刻，两行分别为己方 / 敌方）和 `mirage-timing-sequence.bmp`（完整 66 帧，前六行己方、后六行敌方，每行 11 帧）。场景在 frame 35 开火、36 停止、50 恢复树形。标注图和动画输出保存在 `build/mirage-timing-fix/`；这些是本项目 GPU 输出，不是原版对照录像。

本轮 Debug / Release 重新构建并各通过全量 30/30 CTest，包含图形回归；日志为 `build/mirage-timing-fix/ctest-debug.log`、`ctest-release.log`。关键时刻标注图为 `mirage-timing-preview.png`，完整 66 帧动画为 `mirage-timing-60fps.webp` / `mirage-timing-15fps.webp`，分别按两种游戏帧率播放。

## 原版资源与边界

新增 29 个文件均已与 `assets_origin` 逐字节比较，包含 11 份效果 SHP、6 份炮塔 VXL / HVA、12 份剧场树形。基础坦克体素和 SREFICON / RTNKICON / TNKDICON 复用已有文件。清单保存在忽略目录 `build/special-vehicles/assets/manifest.json`。

当前是基础兼容实现，以下细节尚未用纯净 RA2 1.006 逐帧校准：分束原版遍历顺序、随机落点分布、多格建筑重复命中；光束亮度 / 芯线宽度及炮塔切换边界；幻影己方闪烁周期、步兵近距离识破阈值、AI 难度相关识破概率。没有接入老兵 / 精英武器、声音、完整燃烧死亡或通用地图树木实体系统，支持状态仍为 Partial。

原始规则是主要依据，辅助参考为维护这些逻辑的逆向工程资料：[IsChargeTurret](https://modenc.renegadeprojects.com/IsChargeTurret)、[DisguiseWhenStill](https://modenc.renegadeprojects.com/DisguiseWhenStill)、[DefaultMirageDisguises](https://modenc.renegadeprojects.com/DefaultMirageDisguises)、[Phobos 的 Shrapnel 扩展说明](https://phobos.readthedocs.io/en/latest/Fixed-or-Improved-Logics.html#shrapnel-enhancements)。YR / 扩展引擎资料用于识别逻辑约束，不作为 RA2 逐帧一致性的证明。

## 模块和验证

- `src/config/definitions.*`：武器光束 / 分束字段、伪装和炮塔配置、支持状态。
- `src/simulation/world.*`：分束、伤害、识破、开火显形、炮塔快照和重放状态。
- `src/presentation/special_vehicle_view.*`：原版剧场树形、阴影和光束绘制。
- `src/presentation/world_view.*` / `rhino_unit.*`：单向快照、伪装显示、命中框和选择。

Debug / Release 均使用 CMake 构建，启用图形回归后 CTest 各 30/30 通过。`special_vehicle_regressions` 覆盖 SABOT 爆炸事件、光棱主束 / 五分束 / 装甲倍率 / 范围 / 友军 / 空地过滤、悬崖碰撞、ROF / 四阶段炮塔、幻影移动和显形 / 自动索敌 / 已锁目标 / 识破，以及费用、前置、出厂和 30/60/144 Hz 重放。混选与伪装命中框检查加入 `unit_selection_regressions`。

`demo_special_vehicles` 使用真实 OpenGL 跑 360 帧，检查原版效果已加载并进入绘制、主束与分束、四套炮塔、幻影树 / 移动 / 开火 / 实际 SDL 鼠标选取，并切换温带、雪地、城市剧场。保留 GL 错误和纹理 sampler 错误日志的失败判定。

光束复核另外在该图形测试中直接读回 GPU 像素，检查队色与默认宽度、完整 15 帧亮度单调衰减、非加法混色、重复束不会过曝、前束不写深度、场景几何遮挡、到期消失和 GL 状态恢复；覆盖八方向、正反端点、亚像素相机位置及 0.5 / 1 / 2 倍缩放。逻辑测试检查主束 / 分束出现帧，并确认视觉提前显示不会提前结算伤害。

```sh
./cmake-build-debug/ra2_power_demo --special-vehicles-smoke-test build/special-vehicles/scene.bmp
```

关键帧截图和 `scene-contact.png` 位于忽略目录 `build/special-vehicles/`，已人工检查树根锚点、黑色树影、血条、炮塔和光棱分束效果。
