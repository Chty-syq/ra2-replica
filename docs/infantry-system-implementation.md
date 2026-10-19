# 步兵系统实现

更新：2026-10-18。依据 [调研与素材核验](infantry-system-research-2026-10-16.md)，当前开放美国大兵 E1（GI）、动员兵 E2（CONS）、盟军工程师 ENGINEER、苏军工程师 SENGINEER、狙击手 SNIPE、防空步兵 FLAKT、火箭飞行兵 JUMPJET、磁暴步兵 SHK、辐射工兵 DESO、恐怖分子 TERROR、疯狂伊文 IVAN、海豹 GHOST、谭雅 TANY、间谍 SPY、超时空兵 CLEG，以及双方警犬 DOG / ADOG。其他步兵仍由能力检查拒绝，侧栏灰显并说明原因。

第二批按原方案接入运输与驻军：防空履带车 HTK、IFV 的现有乘员形态，以及 GI / 动员兵进驻民居。第三批先接入 FLAKT / JUMPJET，以及它们所需的空地目标过滤、独立空中占位、飞行和坠落过程；其余特殊乘员继续按后续阶段开放。

## 已接通的流程

- 读取原版 `rules.ini` / `art.ini` 的类型、主副武器、Sequence、开火事件、弹头卧倒倍率和普通死亡动作。GI 使用 M60，部署后切换 Para；动员兵使用 M1Carbine。
- 一个地格容纳三个同阵营步兵；源子格和下一目标子格分别占位、预留。停止完成当前一步，改令后重新寻路，死亡同时释放两端。车辆、建筑保持整格占用。
- 行走、射击、受伤卧倒、爬行、起身、死亡，以及 GI 部署、部署射击和收起。所有发射事件来自仿真帧，画面只读取动作状态。
- 零扩散攻击只伤害指定目标；范围伤害按每个实体的命中体积结算。不可见子弹检查墙体、悬崖和高差；有弹道的零扩散炮弹仍须命中落点附近。
- 坦克可以碾压可碾压的敌方步兵；友军步兵、已部署且不可碾压的 GI 阻挡车辆并促使绕路。
- 盟军兵营 GAPILE / 苏军兵营 NAHAND 使用现有队列、费用、科技前置；出口满员时保留队列重试，腾出子格后只生成一次。兵营可设置集合点。
- 步兵与车辆共用框选、Shift 加减选择和群组命令；步兵有独立命中检测、队色、选中标记和血条。
- 双方工程师沿可达建筑边缘寻路，进入敌方或中立的可占领建筑后转移所有权；进入己方受损建筑后恢复全部生命。成功后消耗工程师，不留下死亡尸体。入口受阻会等待重试，改令、停止、死亡或目标丢失会取消进入并释放占位。
- 狙击手使用原版 `SNIPE` / `AWP` / `HollowPoint` 和 SHP 序列：生命 125、价格 600、射程 14、伤害 125、ROF 150、FireUp 事件延迟 5 帧。自动选敌遵循弹头装甲倍率，排除倍率 ≤1% 的目标；手动攻击仍允许 1% 目标，0% 为非法目标。
- 防空履带车按 Passengers=5、SizeLimit=2 运输，IFV 按 Passengers=1、SizeLimit=1 运输。保留乘员 ID、阵营、生命值，登车后释放外部占位并退出绘制、选取、寻敌与伤害检查；卸载重新分配可用步兵子格，出口受阻则保留乘员并重试。
- GI / 动员兵可进驻 `CanBeOccupied` 建筑，按 MaxNumberOccupants 限制人数。第一人进入时建筑获得驻军归属，最后一人退出时恢复原归属。驻军按所属国家使用 AlliedOccupyW / SovietOccupyW，并按人数发射；空的中立建筑不会自动开火，也不会被闲置单位自动攻击。

## 运输与驻军（第二批）

IFV 从乘员 IFVMode 选取 WeaponN、WeaponNFLH 和原版炮塔：

| 乘员 | 武器 | 炮塔 |
|---|---|---|
| 空载 | HoverMissile，原版 DRAGON 导弹图像；对地 / 对空追踪与 Burst=2 | fvtur |
| GI / 动员兵 | CRM60 | fvtur1 |
| 狙击手 | AWPE | fvtur1 |
| 双方工程师 | RepairBullet；维修友方受损载具，不消耗工程师 | fvtur2 |
| 防空步兵（第三批） | CRFlakGuyGun；原版仅对地，不能据兵种名称额外赋予对空能力 | fvtur1 |

未接通的特殊乘员武器会拒绝登上 IFV。原版 FV 的 CRNuke 死亡武器仅用于伊文乘员形态，不能据此让普通 IFV 自爆；该形态在后续伊文批次接通。空载导弹支持实际高度上的空地追踪和两发连射；对空增加原版 AirRangeBonus=4 格。GI / 动员兵、狙击手及防空步兵乘员形态遵循各自投射物的 AA / AG 标志。

驻军演示使用原版 `CAHSE01` 美国民居，容量为 5。按气候选择 cthse01 / cahse01 / cuhse01，绘制正常、受损、加固三种状态；第 3 帧是废墟，不能当作“受损驻军”帧。枪口读取 art 的 MuzzleFlashN 和 OccupyHeight，使用 UCFLASH 原版效果。建筑进入红血时强制疏散，堵门时不会直接丢弃仍存活的驻军。

载具死亡会清理舱内乘员，避免遗留隐藏实体；建筑倒塌时尝试把驻军放到外围合法子格，无法逃出的乘员随建筑死亡。运输车的原版乘员逃生概率、驻军精确射击分配与离楼节奏尚未用原版逐帧校准。卸载间隔当前为 `[Simulation] UnloadFrames=8`，导弹同一轮两发间隔为 5 逻辑帧，ROF 从该轮第一发起算。

## 武器与防空（第三批，先接入两种步兵）

| 单位 | 原版数据与行为 |
|---|---|
| 防空步兵 FLAKT | HP 100、价格 300、速度 4；要求 NAHAND + NARADR。对地 FlakGuyGun：伤害 20、ROF 20、射程 5；对空 FlakGuyAAGun：伤害 20、ROF 25、射程 8。FireUp=3，使用原版 GUNFIRE、HTRKPUFF / SMKPUFF |
| 火箭飞行兵 JUMPJET | 图像 ROCK、HP 125、价格 600；要求 GAPILE + RADAR。20mm：伤害 25、ROF 30、射程 5，可对地 / 对空；飞行开火事件 FireUp=2，使用 FireFly 与 PIFFPIFF |

目标能力由 Projectile 的 AA / AG 决定。防空兵与 HTK 自动在对地主武器、对空副武器之间选择；不能对空的 GI、狙击手、普通坦克不会接收或执行对空射击。混选攻击只向有效单位发送命令。范围伤害同时检查目标域和三维距离，防空爆炸不会打到正下方地面单位，对地爆炸也不会伤到飞行兵。

JUMPJET 以独立空中状态和占位飞行，可越过地面单位、水面、不可通行地形与建筑，不阻塞地面建造和通行。读取 JumpjetSpeed=30、Climb=20、Crash=25、Accel=10、TurnRate=10、Height=500；起飞先爬升，途中按地形和建筑 art Height 抬高巡航高度，转向后移动，到点悬停。移动、追击、停止、新命令及死亡均维护独立预留。BalloonHover / NoWobbles 对应当前始终悬停、不落地、不摆动的行为，不能登上地面运输车或驻军。

使用 RocketeerSequence 的 Fly / Hover / FireFly 原版方向组。飞行兵的绘制、命中、框选与原版血条均使用实际高度，尸体从 AirDeathStart 经 AirDeathFalling 到 AirDeathFinish，不再保留可选实体或空中占位。飞行开火从空中 FLH 发出，IFV 导弹追踪目标实时位置及高度。

防空散布读取 BallisticScatter；地面散布在一倍半径内，不可见对空散布在两倍半径内。散布落点在发射时确定，使用纳入重放哈希的固定种子整数随机状态，不在命中时吸附回目标。原版散布概率分布、飞行加速度 / 转向换算、建筑避让细节和动画节拍仍属于下文的校准范围。

## 磁暴步兵与磁能坦克（2026-10-18）

| 单位 | 接入内容 |
|---|---|
| 磁暴步兵 SHK | 原版 SHK / ConSequence 八向动作；HP 130，价格 500，不可碾压；ElectricBolt 伤害 50 / ROF 60 / 射程 3，FireUp=2，仅对地 |
| 磁能坦克 TTNK | 原版 ttnk / ttnktur VXL 与 HVA；HP 300，价格 1200；TankBolt 伤害 135 / ROF 75 / 射程 4，仅对地；保留 NAWEAP + NARADR 前置和苏俄 RequiredHouses |
| 磁暴乘员 IFV | IFVMode=6 切到 CRElectricBolt 与 fvtur3；伤害 60 / ROF 45 / 射程 6，仅对地；卸载恢复空载导弹炮塔 |

左键选中磁暴步兵，再点己方磁暴线圈即可充能；混选只下发给具有 ElectricAssault 副武器的步兵。空闲步兵会在副武器射程内优先索敌、无敌人时就近支持线圈。AssaultBolt 读取原版 25 帧 ROF 和 1.83 格射程，使用另一组电弧颜色，只建立支持关系，不按 Damage=10 扣除友军建筑血量。距离不足的手动命令沿现有步兵寻路接近，射击后才计为有效充能者。

正常供电时，1 名充能者使线圈使用 OPCoilBolt（伤害 300 / ROF 100 / 射程 8）；断电时，2 名维持普通 CoilBolt，3 名使用强化武器。改令、停止、移出射程、登车、死亡、归属变化或线圈售卖 / 摧毁均使支持关系失效。S 停止还会抑制自动重新充能，直到收到新的移动或攻击命令。充能只作用于当前线圈，不增加全基地发电量；powered / coilChargers / overpowered 来自 World 快照。

人数阈值采用 [Phobos 默认 KeepOnline=2 / ChargeWeapon=1](https://github.com/Phobos-developers/Phobos/blob/c265252b21961345bfaebd3aa928fb721d166d74/src/Ext/BuildingType/Body.h) 及其 [过载判定](https://github.com/Phobos-developers/Phobos/blob/c265252b21961345bfaebd3aa928fb721d166d74/src/Ext/Building/Hooks.cpp)。这是基于 YR 开源扩展的兼容实现，没有对 RA2 1.006 做逐帧实测。OpenRA RA2 的 [磁暴步兵定义](https://github.com/OpenRA/ra2/blob/master/mods/ra2/rules/soviet-infantry.yaml) 提供友军充能和邻近支持的结构参考，具体数值仍以本项目原版 INI 为准。

InfDeath=5 使用原版 ELECTRO，替代普通倒地尸体，使用 anim.pal，不做队色重映射。读取 LoopStart / LoopEnd / LoopCount：15 帧素材重复 0..3 五次后继续剩余帧，共 31 游戏帧。死亡立即释放占位，电击仅保留视觉事件。其他特殊死亡继续待补。

新增 shk.shp、electro.shp、fvtur3.vxl / hva 均从 assets_origin 原样复制，复用已有磁能坦克和图标；完整解码 SHK 的 602 帧与 ELECTRO 的 15 帧，SHK 后半段为空。核验清单、八向动作预览和三剧场实机场景保存在 build/tesla-units/。sidebarUnit 优先匹配明确的阵营资源映射，避免 PRES / PENTGEN 等剧情人物复用 SHKICON 抢占建造入口。

`tesla_unit_regressions` 覆盖定义、前置 / 国别 / 费用 / 出厂、射击事件 / ROF / 装甲倍率、对空拒绝、不可碾压、充能人数及中断、电击循环、IFV 和 30 / 60 / 144 Hz 重放。`unit_selection_regressions` 验证友军线圈混选过滤；`demo_tesla_units` 实际运行 300 帧 OpenGL，覆盖六种电弧武器、SDL 友军线圈点击、电击素材、IFV 炮塔 / 卸载和三剧场。截图命令：

```sh
./cmake-build-debug/ra2_power_demo --tesla-units-smoke-test build/tesla-units/scene.bmp
```

两种新单位仍标为 Partial，提示尚未接入的老兵 / 精英能力；语音、音效和原版逐帧节奏校准继续待补。

最终 Debug / Release 均完成 CMake 构建，CTest 各 37/37 通过。补充了工程师 IFV 自动维修回归，确保充能目标过滤不会屏蔽友军维修目标。构建、测试、素材 SHA-256 清单与画面预览记录在 build/tesla-units/。

## 辐射工兵与 V3 火箭车（2026-10-18）

| 单位 | 接入内容 |
|---|---|
| 辐射工兵 DESO | 原版 DESO / DesoSequence，HP 150、价格 600、伊拉克专属、NAHAND + RADAR 前置；辐射免疫、不可碾压；RadBeamWeapon 伤害 125 / ROF 50 / 射程 6，仅对地 |
| 辐射乘员 IFV | IFVMode=9 使用 CRRadBeamWeapon，伤害 175 / ROF 30 / 射程 7；沿用原版 RadCannonTurretIndex=3 对应 fvtur3，不按乘员编号拼炮塔名；卸载恢复空载形态 |
| V3 火箭车 | HP 150、价格 800、NAWEAP + NARADR 前置；原版载弹 v3、空架 v3wo、独立 v3rocket 及配套 HVA；保留 CanPassiveAquire=no，玩家指定目标后才发射，射程 5..18 格 |

选中辐射工兵按 D 部署，再按 D 收起；部署后直接点空地也会收起并行走，部署过渡中收到移动命令会先完成收起。RadEruptionWeapon 不向敌人发射普通炮弹，而是在自身地格建立半径 10 格、初始等级 500 的辐射区。ROF=60 为最短释放间隔，中心辐射降到初始等级的三分之一以下才再次释放；部署不会每 60 帧把辐射强制刷满。这一条件参考 [Phobos 部署检查](https://github.com/Phobos-developers/Phobos/blob/c265252b21961345bfaebd3aa928fb721d166d74/src/Ext/RadSite/Hooks.cpp#L44)。

持续辐射读取 Radiation 节的 ApplicationDelay=16、LevelMax=500、LevelDelay=90、LightDelay=90、DurationMultiple=1、LevelFactor=0.2、LightFactor=0.1、TintFactor=1 和原版绿色。距离按地格中心线性衰减，重叠区域按等级上限结算，伤害使用 RadSite 装甲倍率。普通友军同样受伤；免疫单位、空中单位和运输车 / 建筑内的乘员不受区域伤害，原版普通建筑倍率为零。收起、移动或死亡后旧区域继续衰减，到期删除。InfDeath=7 使用原版 NUKEDIE 融化动画，逻辑占位即时释放。

`simulation/radiation.*` 保存独立区域与衰减状态，部署者不持有可失效的区域指针。表现层只查询 radiationLevel / radiationLight，生成地面绿光和辐射射线；世界状态与伤害不依赖 GPU 或显示帧率。同一中心 / 半径的释放更新原区域，其他中心分别保留；刷新与灯光是当前兼容实现，尚未与 RA2 1.006 逐帧核对。衰减和叠加边界参考 [Phobos RadSite](https://github.com/Phobos-developers/Phobos/blob/c265252b21961345bfaebd3aa928fb721d166d74/src/Ext/RadSite/Body.cpp)，不把 YR 扩展等同于纯 RA2 原程序。

V3 的 V3Launcher 只下发发射任务，不把 Damage=1 当成实际命中伤害。每车一枚 V3ROCKET，原版 HP 50 / special_2 装甲，可被防空步兵、IFV 等防空武器拦截；火箭使用空中目标分类，不占地面或飞行步兵子格，也不能被框选、编组或玩家直接移动。发射准备使用 PauseFrames=0、TiltFrames=60 和原版起止俯仰；离架后按 SpawnRegenRate=400 计时补弹，仍有在空火箭时不重复生成。准备期间 S 停止、移动、改令或发射车摧毁会取消准备并释放载弹状态；已经飞出的火箭继续飞行。被拦截只播放空中爆炸，不在目标处补结算地面伤害。

实际命中读取 V3RocketDamage=200 和 V3WH（CellSpread=1、PercentAtMax=25%），落点在建立发射任务时锁定，移动目标可离开爆炸区。准备期间底盘保持发射姿态，不因目标移出射程而追逐移动、使火箭与发射架分离。`simulation/missile.*` 使用整数曲线弧长、原版速度、加速度、飞行高度与转弯速率，渲染读取同一位置和俯仰；末段不会从空中瞬移到地面。当前采用 64 段整数抛物线兼容轨迹，尚未重建原版 RocketLocomotor 的精确曲率。V3TRAIL / V3TAKOFF 来自原版 SHP，尾焰位置考虑 V3RocketBodyLength。V3ROCKET 的 Remapable=no 由体素着色器实际执行，不受队色染色。

新增原样复制 DESO（644 帧，前 322 帧为主体、后半段为空）、NUKEDIE（60 帧）、V3TAKOFF / V3TRAIL（各 20 帧）、v3wo / v3rocket 的 VXL / HVA。复用现有 v3、fvtur3、原版图标和 special_vehicles 下的八种原版爆炸效果，没有保留重复副本。资源清单、SHA-256、八向动作预览和实机截图保存在 build/radiation-v3/。

新增 `radiation_v3_regressions`，覆盖定义与异常参数、兵营 / 战车工厂生产前置 / 国别 / 费用 / 出厂、辐射射击事件 / 伤害 / 免疫、部署移动和衰减 / 重叠上限、IFV、火箭准备 / 曲线 / 补弹 / 取消 / 拦截 / 发射车死亡、30 / 60 / 144 Hz 重放。`unit_selection_regressions` 增加火箭不可操控、防空命令过滤与空中命中框。`demo_radiation_v3` 运行 650 帧，实际 SDL 点击选兵、D 部署、点击空地收起移动，检查持续辐射消退、两种辐射射线、原版融化死亡、V3 命中与拦截、补弹和三剧场；另读回 GPU 像素核对八方向的载弹 / 空架 / 火箭模型、禁止重映射、绿光消退和射线寿命。

```sh
./cmake-build-debug/ra2_power_demo --radiation-v3-smoke-test build/radiation-v3/scene.bmp
```

老兵 / 精英、自愈与声音继续待补，侧栏以 Partial 明示；自愈同样为既有 CMIN 等类型补上待办提示，生产与采矿不受影响。原版飞控曲线、辐射光照和刷新细节仍需逐帧校准。

最终 Debug / Release 均完成 CMake 构建，完整 CTest 各 39/39 通过；素材去重后重复核验解码与专项图形回归通过。移动目标下的发射姿态锁定和固定落点已有专项回归。日志、逐字节资源清单与截图位于 build/radiation-v3/。

## 本批基础整理

单位类型 ID 与素材名分开处理：幻影坦克为 `MGTK`（素材 `RTNK`），入侵者为 `ORCA`（素材 `FALC`）；不能直接以文件名作为生产 ID。武器读取覆盖 Primary、Secondary 和 Weapon1..N，例如光棱坦克的 Comet 不再被遗漏。后续已接入光棱与幻影专用行为，见 [特殊载具实现](special-vehicle-implementation.md)。

类型保留 Sprite / Voxel 展示方式和三种支持状态：Ready、Partial、Unsupported。侧栏对未支持单位灰显，悬停列出阻塞原因；部分可用单位列出未接通能力。特殊武器、死亡爆炸或 SHP 车辆尚未实现时，不作为普通坦克误开放。工程师的拆弹器和扫描器属于交互工具，不会生成普通攻击武器。

生产读取 Owner、RequiredHouses、ForbiddenHouses，并匹配工厂类别与阵营条件。`Sandbox=no` 时按玩家国家检查限制；默认 `[Simulation] PlayerHouse=British`，可用 `[Players] 0=British`、`1=Russians` 分别覆盖。通用兵营的 Owner 包含多国，不能单独用它判断工程师阵营。

## 操作

从项目目录启动 `./cmake-build-debug/ra2_power_demo`。普通演示开局加入对应阵营的兵营、三个基础步兵、一名工程师和一辆 IFV / 防空履带车；盟军额外加入狙击手和火箭飞行兵，苏军加入防空步兵。地图 (12,2) 放置敌方电厂供占领测试，(0,9) 放置中立民居供驻军测试。

1. 左键选择或拖动框选，Shift 加减选；可以混选步兵和坦克。
2. 选中后左键空地移动、左键敌人攻击，`S` 停止。
3. 选中停稳的 GI 按 `D` 部署，再按 `D` 收起；单选后再次点击该 GI 也可切换。部署状态不能移动，移动中的 GI 需先停稳。
4. 在侧栏步兵页点击已开放单位图标生产；需要兼容的兵营。选中兵营再左键空地设置集合点。
5. 选中工程师后左键敌方建筑占领，左键己方受损建筑修复；`S` 或移动命令取消。满血、未建成或不可占领的目标不消耗工程师。混选时建筑交互只发送给工程师。
6. 选中步兵后左键己方运输车登车；GI 需先收起部署。GI / 动员兵左键空民居或己方驻军建筑进入，工程师和狙击手不能驻军。
7. 选中运输车或己方驻军建筑后按 `U` / `D` 卸载，再次点击已选中的载具 / 建筑也可卸载；面板列出人数、当前武器及卸载按钮，地图使用原版 pips 人形图标显示容量。`S` 取消尚未完成的卸载，运输车也可通过移动命令取消。
8. 工程师进入 IFV 后，左键己方受损载具进行维修；仅维修车辆，不维修建筑或敌人，不能自修。

默认 `Sandbox=yes`，延续演示的免费生产和宽松科技模式。关闭后按原版 GI 200、动员兵 100、双方工程师 500、狙击手 600、防空步兵 300、火箭飞行兵 600 扣费；狙击手限英国，并要求盟军兵营与雷达科技。

占领保留建筑实体和现有生命值，并更新显示队色。工厂清空原主人的待生产队列并退款，已经驶出的车辆保留原主人并完成出厂。矿场释放旧矿车的卸货队列与泊位；旧矿车保留阵营、货物并另寻己方矿场，占领不再赠送免费矿车。

### 血条与选中状态（2026-10-17）

步兵覆盖层已替换为原版素材：`pipbrd.shp` 第 1 帧裁去透明区后为 18×4 边框，内部拼接 8 个 `pips.shp` 的 2×2 血格；第 15 / 16 / 17 帧分别提供绿、黄、红色。颜色直接来自 `palette.pal`，不通过纯色矩形或队色染色生成。按后续界面要求已移除脚边白色括号，选中仅显示血条。

血量按剩余比例向上取整为血格，存活且只剩 1 点生命仍显示一格；超过最大值时限为满格。颜色阈值读取原版 `[AudioVisual] ConditionYellow / ConditionRed`，当前配置为 ≤50% 黄色、≤25% 红色。选中或悬停显示血条；普通未选中、死亡或尸体不显示覆盖层。敌人血条遵循 `EnemyHealth`，鼠标在侧栏、调试面板或建造预览时不触发悬停提示。

血条在脚底锚点上方 27 像素；坐标对齐整数像素，放大时保持整数倍率和最近邻采样。覆盖层随地形高度和插值位置移动，不随动作帧的透明边界上下跳动。三名同格步兵各自显示小号覆盖层。

已用实际 OpenGL 绘制检查满血、50%、25%、1 HP、悬停、未选中、卧倒、部署、动员兵与三人同格共 12 个预览场景。预览位于忽略目录 `build/infantry-overlays/preview.png`，显示为原始像素的 2 倍，便于核对边缘与血格。

## 代码与资源

| 模块 | 职责 |
|---|---|
| `src/infantry_art.*` | 解析动作序列、方向跨度、固定方向与零长度序列 |
| `src/config/definitions.*` | 类型与素材映射、武器槽、国家限制、能力状态、兵营出口及 SHP 帧边界校验 |
| `src/simulation/world.*` | 三子格预留、步行、战斗、生产、建筑交互、乘载关系、卸载、驻军及 IFV 武器选择 |
| `src/simulation/flight.*` | 与 ECS / 渲染解耦的整数高度、转向与加速移动 |
| `src/simulation/harvesting.*` | 矿场被占领时解除旧泊位与队列，保留原矿车货物 |
| `src/presentation/infantry_view.*` | 索引纹理图集、unit 调色板、队色、深度、选中标记与鼠标命中 |
| `src/presentation/world_view.*` / `unit_view.h` | 从仿真生成步兵和通用可选对象快照，位置插值 |
| `src/interaction/unit_selection.*` / `src/app_main_ui.cpp` | 混选、命令、侧栏出兵及图形冒烟场景 |

运行时使用从 `assets_origin` 原样复制的文件：`assets/infantry/allied/gi/gi.shp`、`assets/infantry/soviet/cons/cons.shp`、`assets/infantry/common/engineer/engineer.shp`、`assets/infantry/allied/sniper/snipe.shp`、`assets/effects/infantry` 中的八向 mgun 与 piff/piffpiff，以及 `assets/icons/cameo/infantry` 下的图标。两方工程师共用原版 ENGINEER 图像，由阵营队色区分。复用现有调色板，未修改原始资源。

SHP 方向组为 N、NW、W、SW、S、SE、E、NE；武器 Anim 列表按 N、NE、E、SE、S、SW、W、NW 排列，分别转换。GI / CONS / ENGINEER / SNIPE / FLAKT / ROCK 仅绘制前 372 / 301 / 253 / 301 / 301 / 427 帧主体；后半段不作为独立阴影绘制。

第二批新增原样复制的资源为 `assets/vehicles/ifv/fvtur{1,2}.{vxl,hva}`、`assets/buildings/civilian/american_house/c{t,a,u}hse01.shp`、`assets/effects/garrison/ucflash.shp`、`assets/effects/projectiles/dragon.shp`。复用现有步兵、IFV / 履带车主体和原版 pips；未修改 `assets_origin`。

第三批原样复制 `assets/infantry/soviet/flak_trooper/flakt.shp`、`assets/infantry/allied/rocketeer/rock.shp`、`assets/effects/flak/{smkpuff,htrkpuff}.shp`。复用现有 FLKTICON / JJETICON、unit 调色板、枪口、弹丸和 PIFFPIFF。已解码核对这四份 SHP 及两个图标共 1,493 帧；两种步兵的后半段均为空，未虚构阴影素材。四份新增文件与资源池逐字节一致。

## 验证

macOS arm64，使用 CLion 内置 CMake、系统 Clang 和 `MacOSX.sdk`。截至伊文 / 天启批次，Debug、Release 均构建成功；Debug 逻辑测试 26/26、Release 含图形的全量 CTest 46/46 通过。复现命令（已配置构建目录）：

```sh
/Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/cmake --build cmake-build-debug -j 6
/Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/ctest --test-dir cmake-build-debug --output-on-failure -j 4 -E '^demo_'
/Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/cmake --build cmake-build-release -j 6
/Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/ctest --test-dir cmake-build-release --output-on-failure
```

新增 `infantry_regressions` 覆盖同格容量、预留与死亡释放、群组移动、直接/范围伤害、GI 部署切枪、CONS 开火事件、卧倒倍率、碾压及保护、兵营阻塞与重试、双方生产费用和集合点、弹道脱靶，以及 30/60/144 Hz 输入下相同逻辑结果。混选测试并入 `unit_selection_regressions`。

`support_infantry_regressions` 覆盖 ID / 素材映射、支持状态、国家与生产费用、双方工程师占领和修复、多人争抢、取消 / 死亡 / 目标丢失、受阻重试、工厂队列退款与出厂车辆、矿场泊位与货物保留、免费矿车防重复、狙击手选敌 / 伤害 / 开火事件 / ROF，以及不同渲染频率下的重放一致性。

`demo_infantry` 使用真实 OpenGL 加载原始资源，运行 180 个逻辑帧，验证部署、行走、射击和 GL 状态。所有图形测试保留对 `GLD_TEXTURE_INDEX_2D is unloadable` 日志的失败判定。人工检查了部署、受击和移动截图：

```sh
./cmake-build-debug/ra2_power_demo --infantry-smoke-test build/infantry-smoke.bmp
```

该命令输出 `build/infantry-smoke-frame-{1,20,70,150}.bmp`，生成文件处于忽略目录。

`demo_support_infantry` 运行真实 OpenGL 场景 320 帧，验证双方兵营生产、工程师进入 / 占领 / 修复、狙击手射击及 GL 状态；测试专用生产加速不改变普通演示配置：

```sh
./cmake-build-debug/ra2_power_demo --support-infantry-smoke-test build/support-infantry.bmp
```

输出 `build/support-infantry-frame-{1,20,70,150,300}.bmp`。已检查实际画面中的原版工程师、狙击手、建筑占领队色和入楼过程。

`transport_regressions` 覆盖满载、Size / SizeLimit、归属、登车 / 卸载占位、取消、移动载具的新入口跟随、堵门重试、载具死亡、双方驻军武器、空民居选敌、红血 / 倒塌疏散、IFV 五种已接入乘员、空载导弹连射、工程师维修和 30/60/144 Hz 重放。`unit_selection_regressions` 验证混选登车、舱内单位不显示 / 不可选、卸载后恢复选取及驻军能力过滤。

`demo_transport` 运行 700 帧真实 OpenGL 场景，实际通过 SDL 鼠标选择及 U 键卸载，验证驻军火力、乘员隐藏、机枪 / 维修 / 狙击炮塔切换、维修满血、导弹及 GL 状态：

```sh
./cmake-build-debug/ra2_power_demo --transport-smoke-test build/transport.bmp
```

输出 `build/transport-frame-{1,20,70,150,220,320,560,660}.bmp`。

`air_defense_regressions` 覆盖原版类型 / 帧数 / 武器域、生产前置与费用、空地占位分离、越水越楼、起飞 / 群移 / 停止 / 改令、自动与手动对空选敌、移动目标追击、射程 / 开火事件 / ROF、三维散布伤害、IFV 原版乘员模式、导弹对空、坠落全过程及 30/60/144 Hz 重放。混选命令过滤和空中框选加入 `unit_selection_regressions`。

`demo_air_defense` 运行 420 帧真实 OpenGL 场景，验证原版飞行与射击资源、悬停、两套防空武器、坠落、实际 SDL 鼠标选取空中单位及 GL 状态。已检查空中血条和建筑上方的绘制：

```sh
./cmake-build-debug/ra2_power_demo --air-defense-smoke-test build/air-defense/scene.bmp
```

输出 `scene-frame-{1,20,70,100,150,220,320,360}.bmp`，资源核验与截图均位于忽略目录 `build/air-defense/`。

## 双方警犬与恐怖机器人（2026-10-18）

本批开放苏军警犬 DOG、盟军警犬 ADOG、恐怖机器人 DRON。沿用混合阵营建造栏；非沙盒按原版兵营 / 战车工厂、费用和阵营限制生产。没有修改原始 rules.ini / art.ini，运行时路径写入 replica.ini。

| 单位 | 原版资源 | 当前行为 |
|---|---|---|
| DOG / ADOG | dog / adog 主体各 344 帧，dogp / adogp 各 64 帧 | 172 帧主体、32 方向扑击，原版 FireUp=6；只扑杀地面步兵，不能攻击建筑、载具或飞行兵 |
| DRON | dron 176 帧、dronp 64 帧 | 88 帧主体 + 88 帧阴影，扑击 32 方向 + 32 帧阴影；扑杀步兵、进入可寄生的敌方地面车辆持续破坏 |

所有六个文件与 assets_origin/sprites 的 SHA-256 相同；复用已有 DOGICON / ADOGICON / DRONICON。共完整解码 1056 帧，核验清单与原图联系表在 `build/dogs-drone/assets.json`、`original-sprites.png`。

- 读取原版 LimboLaunch、Parasite、Parasiteable、SuppressionThreshold、WalkFrames、FiringFrames。VirtualScanner 的五格范围仅用于自动发现并接近敌人，NeverUse 不产生虚拟伤害。实际扑击仍受 BadTeeth / GoodTeeth 的 1.5 格、DroneJump 的 1.83 格射程约束。
- 扑击时释放旧子格 / 载具预留，并改绘制独立的 dogp / adogp / dronp；不额外发射普通炮弹。途中目标死亡、登车、变为友军，或扑击撞墙时，攻击者恢复地面占位。落点受阻向周围查找空位；持续无空位 90 帧后移除，避免永久幽灵实体。
- 狗保持步兵三子格、Size=1、IFVMode=0，并保留原版不卧倒的动物动作。DRON 保持载具整格占位、Size=2、Crusher=no、免疫辐射，使用自己的 SHP 动画；没有附加 Infantry 组件。机器人可进入防空履带车，不能绕过 IFV 的 SizeLimit=1。
- 进入车辆后机器人隐藏并退出选取 / 常规伤害，位置跟随宿主，宿主继续执行原有移动等命令；每个宿主只容纳一个机器人。按原版 DroneJump 的 Damage=50、ROF=60 和 Parasite 装甲倍率持续伤害。宿主被机器人或外部武器摧毁后，机器人在附近落地重新获得整格占位。
- 对宿主的有效维修可清除机器人；维修 IFV 支持对尚未掉血但已经寄生的友军车辆下令。受击达到原版 SuppressionThreshold=5 后暂停内部破坏，再恢复；下述计时仍为兼容参数。
- 共用原版单位调色盘与队色；机器人读取 SHP 后半部绘制地面阴影。警犬使用 pipbrd 第 1 帧，机器人使用第 0 帧的载具血条，血量刻度沿用原版 pips；没有恢复脚边括号。

参考 [OpenRA RA2 的狗扑击配置](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/rules/allied-infantry.yaml)、[机器人 SHP 序列与阴影布局](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/sequences/vehicles.yaml)。该版本 OpenRA 机器人仅开放扑击步兵，未提供车辆寄生实现，不能当作完整原版行为。车辆寄生的独立宿主、伤害计时和抑制状态参考 [YRpp ParasiteClass 接口](https://github.com/Phobos-developers/YRpp/blob/a823ada387386a18ddadf41b4d7b46f295f94fdb/ParasiteClass.h)；维修时清除寄生参考 [Phobos 的负伤害处理](https://github.com/Phobos-developers/Phobos/blob/c265252b21961345bfaebd3aa928fb721d166d74/src/Ext/WarheadType/Hooks.cpp)。没有运行原版逐帧测量。

当前兼容边界：扑击采用原版速度驱动的整数追踪，附加 96 lepton 抛物线高度，最多追踪 90 帧；这不是原版 bullet locomotor 的逐指令还原。机器人按 FiringFrames=4 播放起跳准备，宿主受击后的暂停采用 50 帧兼容值。精确起跳 / 返回落点、Suppress 计时、原版寄生火花与震动、消失后自动恢复选择、闲置花样动作和音效仍待补充或校准。

`dog_drone_regressions` 覆盖原版定义 / 帧数 / 生产费用与出口、五格扫描追击、非法目标、扑杀与尸体、跳跃时释放占位、移动宿主跟随、原版伤害周期、双机器人争抢、宿主死亡脱离、目标消失 / 墙体中断、满血维修清除、受击抑制、双方警犬与机器人登车卸载、SizeLimit 及逐帧重放哈希。`unit_selection_regressions` 增加混选过滤、跳跃图集帧号和隐藏寄生体检查。

`demo_dog_drone` 在真实 SDL / OpenGL 环境逐帧运行场景，验证三类单位的 8 方向站立 / 行走 / 扑击、三个剧场调色盘、原版阴影、实际鼠标选择，以及车辆寄生 / 脱离。

```sh
./cmake-build-debug/ra2_power_demo --dog-drone-smoke-test build/dogs-drone/scene.bmp
```

输出 `native-dog-drone.bmp` 和 `scene-frame-{10,15,50,95,155}.bmp`。已检查相应 PNG 联系表与场景预览。

另修正隐藏图形测试的垂直同步等待：macOS 显示休眠时，旧用例曾阻塞于 SDL 的 Cocoa_GL_SwapWindow / SDL_CondWait（采样见 `build/dogs-drone/tesla-hang.txt`）。隐藏 smoke 模式按逻辑帧推进，直接提交离屏绘制并读取像素，不交换不可见窗口的显示缓冲；正常交互窗口保留既有设置。

验证：Debug / Release 均构建成功，每个配置的 41 项测试均已有通过记录，但本机完整测试调度仍存在间歇性长超时，未取得两种配置均连续无重试的最终结果。最终 Debug 全量中资源解码与旧防空图形测试超时，Release 全量中旧建筑操作图形测试超时，均独立复跑通过；本批新增的两个测试在这两轮全量中通过。完整日志见 `ctest-{debug,release}-verified.log`，对应复跑见 `ctest-{debug,release}-verified-rerun.log`，均在 `build/dogs-drone/`。未放宽 CTest 超时阈值。

## 炮弹落点与步兵命中体积（2026-10-18）

复现发现：指定步兵的 AttackEntity 原本已使用 Transform.current 的子格坐标，并没有统一瞄准大格中心；真正的漏伤发生在格心落弹和附近爆炸。三个步兵子格偏移都是 (±250,±250) milli-cell，中心距格心约 0.354 格。原实现把步兵视为没有大小的点，AP 的 CellSpread=.3 因而完全搜索不到格心附近的三名步兵。

修正将寻路占位、瞄准坐标、命中体积和弹头扩散分开：

- AttackEntity 继续瞄准实际子格，普通炮弹发射后保留落点 / 预判，不在爆炸时追踪或吸附回目标；AttackGround 继续落在指定格心。
- 步兵采用独立的球形命中体积；爆炸到步兵的三维中心距离减去命中半径，最小为零，再参与 CellSpread 与 PercentAtMax 的范围和衰减计算。不把位置取整到大格，也不扩大所有武器的弹头范围。
- 默认兼容半径是 32 RA2 leptons，即 0.125 格。格心到子格身体边缘约 0.229 格，因此原版 .3 格 AP 能产生有限伤害；离目标半格的另一个子格、相邻格和高处飞行兵仍需独立计算。
- 保留原版 Verses 装甲倍率、ProneDamage 卧倒倍率、敌我范围伤害、空地武器过滤，以及墙体 / 高差截断。零扩散直接攻击仍筛选指定实体，并要求实际落点在其身体内；移动步兵可以避开已发射的普通炮弹。

| 复现场景，满血动员兵、单发 | 修正前 | 修正后 |
|---|---:|---:|
| 犀牛向三名步兵所在格心开炮 | 每人 0 | 每人 13 |
| 灰熊向三名步兵所在格心开炮 | 每人 0 | 每人 10 |
| 犀牛直接攻击某个静止子格 | 目标 22，其余 0 | 目标 22，其余 0 |

上述伤害仍包含原版 AP 的 25% 步兵装甲倍率和 50% 远端伤害比例，不是按坦克名义伤害直接扣血。修正前后的相同探针记录在 `build/projectile-impact/{before,ground-before,ground-after}.tsv`。

依据与还原边界：

- 本地原版 rules.ini 的弹头注释区分零扩散直接目标与正扩散距离比较；AP 定义保留 `.3 / .5 / 25% / ProneDamage=50%`。没有修改 rules.ini 或原版资源。
- [Phobos 的 Bullet 目标坐标接口](https://github.com/Phobos-developers/Phobos/blob/c265252b21961345bfaebd3aa928fb721d166d74/src/Ext/Bullet/Hooks.cpp) 区分对象的 GetTargetCoords 与其他目标的 GetCenterCoords；它是原版接口兼容项目，不能据此声称已取得完整原版弹道实现。
- [OpenRA Armament](https://github.com/OpenRA/OpenRA/blob/dde39344a0f25af15b039e6a2e4dbf046e624c04/OpenRA.Mods.Common/Traits/Armament.cs) 使用目标的真实可攻击坐标；[SpreadDamageWarhead](https://github.com/OpenRA/OpenRA/blob/dde39344a0f25af15b039e6a2e4dbf046e624c04/OpenRA.Mods.Common/Warheads/SpreadDamageWarhead.cs) 默认按 HitShape.DistanceFromEdge 计算衰减，而不是按地图占位格或零体积点。
- 半径参考 [OpenRA RA2 的 Infantry Circle / Radius=128](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/rules/defaults.yaml)（OpenRA 一格 1024 单位）；三维边缘距离参考 [CircleShape](https://github.com/OpenRA/OpenRA/blob/dde39344a0f25af15b039e6a2e4dbf046e624c04/OpenRA.Mods.Common/HitShapes/Circle.cs)。这是明确的 OpenRA 兼容值，尚未通过纯净 RA2 1.006 逐帧测得原版物理半径；可用 replica.ini 的 `Simulation.InfantryHitRadiusLeptons`（0..128）校准，0 用于复现旧的点判定。

上游固定版本源码和 URL 清单保存在 `build/projectile-impact/research/`。新增 `projectile_impact_regressions`，覆盖三类坦克 × 八个方向 × 三个步兵子格、格心范围伤害、相邻格、零 / 窄扩散、边缘边界、距离衰减、卧倒、友伤、空中高度、连续移动预判、发射后躲避、墙体阻挡、参数校验和重放一致性。复用原版炮弹与爆炸资源，渲染只读取同一个实际落点。

本轮最终验证：Debug / Release CMake 构建通过，Debug 逻辑测试 24/24、Release 全量测试 42/42（含实际 SDL / OpenGL 用例）通过。日志位于 `build/projectile-impact/build-{debug,release}.log`、`ctest-core-debug-final.log` 和 `ctest-release-final.log`。

## 恐怖分子与自爆卡车（2026-10-18）

这一批接入 TERROR、DTRUCK 及恐怖分子 IFV，复用既有子格、载具移动、运输、生产和辐射系统。

| 单位 | 原版资源与规则 | 已接入行为 |
|---|---|---|
| 恐怖分子 TERROR | Image=TRST；75 HP、flak、Cost=200、Speed=6；TerrorBomb Damage=225、Range=1.5，TerrorBombWH CellSpread=2、PercentAtMax=.5 | 手动攻击后接近目标并引爆；被击杀、被碾压也走死亡爆炸；普通步兵三子格、原版血条、登车 / 卸载 |
| 自爆卡车 DTRUCK | Image=TRUCKA；150 HP、light、Cost=1500、Speed=5、无炮塔；Demobomb Damage=300、Range=1，DemobombWH CellSpread=8、PercentAtMax=.1、RadLevel=100 | 手动接近后自爆、被击毁爆炸、友军误伤与连锁爆炸；留下按原版 Radiation 参数衰减的辐射 |
| 恐怖分子 IFV | IFVMode=11，Weapon12=CRTerrorBomb，高科技炮塔 fvtur3；FV 显式 DeathWeapon=CRNuke | 当前武器的 Suicide 使死亡武器生效；采用 CRNuke 的 200 伤害、5 格范围、RadLevel=500；卸载后恢复普通导弹模式 |

Sandbox 默认仍允许两阵营单位同时生产；非 Sandbox 保留古巴 Confederation / 利比亚 Africans 的国家限制，分别要求苏军兵营 / 战车工厂与雷达。TERROR、DTRUCK 的 CanPassiveAquire=no 和 Trainable=no 保留；不会自动追击附近敌人。

自爆分为两个阶段：Suicide 只令攻击者死亡，死亡解析再选择并引爆 DeathWeapon。因此没有目标格心处的普通炮弹，也不播放武器 Anim=RING1 或越界的 TRST 射击姿势。爆炸位于单位实际坐标，复用装甲、范围衰减和步兵命中体积判定。每个死亡实体只解析一次，按稳定 EntityId 循环处理新产生的死亡，避免递归和连锁重复；死亡对象也不会被后续治疗弹复活。

死亡解析在清除乘员前读取 IFV 模式。仅声明 DeathWeapon=CRNuke 不会令空载、普通步兵、工程师或辐射工兵 IFV 死亡时核爆。隐藏乘员随载具销毁，不独立产生场内爆炸；自爆 IFV 只结算一次，不叠加乘员的 TerrorBomb。普通建筑售卖与工程师消耗仍沿用各自流程。

语义参考 [Phobos 的 Suicide 死亡入口](https://github.com/Phobos-developers/Phobos/blob/c265252b21961345bfaebd3aa928fb721d166d74/src/Ext/WarheadType/Hooks.cpp)、[DeathWeapon 就地引爆修正](https://github.com/Phobos-developers/Phobos/blob/c265252b21961345bfaebd3aa928fb721d166d74/src/Misc/Hooks.BugFixes.cpp) 和 [OpenRA 的 Explodes 场内检查与坐标爆炸](https://github.com/OpenRA/OpenRA/blob/dde39344a0f25af15b039e6a2e4dbf046e624c04/OpenRA.Mods.Common/Traits/Explodes.cs)；同时对照 [OpenRA RA2 恐怖分子](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/rules/soviet-infantry.yaml) 和 [自爆卡车](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/rules/soviet-vehicles.yaml)。数值以本地原版 rules.ini 为准，不照搬 OpenRA 的平衡与额外部署能力。固定版本源文件保存在忽略目录 `build/demolition/research`。

从 `assets_origin` 逐字节复制 7 个资源：`trst.shp`、`trucka.vxl/hva`、`trsticon.shp`、`trkaicon.shp`、`mininuke.shp`、`demtexp.shp`。TRST 为 188 主体帧加 188 个空阴影条目，MININUKE / DEMTEXP 为 39 / 25 帧，使用原版 anim.pal。未修改原始资源或 rules.ini / art.ini；`replica.ini [WarheadVisual.CRNUKEWH]` 仅将原始 `AnimList=MININUKE - added 11/30` 中混入的文字注释修正为有效动画引用。

新增 `demolition_regressions` 覆盖三子格与友军范围伤害、装甲 / 距离衰减、主动 / 被动死亡、自爆追击与停止、对空拒绝、低 ID 连锁死亡、运输销毁、IFV 模式 / 卸载、国家与雷达限制、出厂和重放一致性。`demo_demolition` 检查三剧场 / 八方向原版 SHP 与 VXL、原版血条、爆炸动画像素、实际 SDL 鼠标选取和场景辐射；图集与场景截图见 `build/demolition`。

验证结果：Debug / Release 构建通过；Debug 逻辑 CTest 25/25（13.42 秒），Release 全量 CTest 44/44（38.24 秒），未重试；Debug 专项 SDL 图形测试亦通过，已查看单位、爆炸图集与实际场景截图。日志在 `build/demolition/{build-debug.log,build-release.log,ctest-core-debug-final.log,ctest-release-final.log,smoke-debug.log}`；7 个资源的来源与 SHA-256 清单为 `build/demolition/asset-sha256.json`。

本段接入当时尚未用纯净 RA2 逐帧核对死亡结算顺序、隐藏乘员边界和爆炸光照节拍；烧死（InfDeath=4）、地形起火、碎片、震屏及音效仍待实现。后续伊文和拆弹进展见下一批记录；当时 TANY 的 C4 尚未开放；见后续突击队员批次。

## 疯狂伊文、拆弹与天启补完（2026-10-18）

本批新增 IVAN，接通伊文 IFV，补完此前已能移动 / 战斗的天启 APOC；没有把其他尚缺特殊能力的单位作为普通枪兵开放。

- IVAN 使用原版 `ivan.shp`（267 主体帧）及 IvanSequence / FireUp=6，Cost=600、Strength=125、Speed=4，要求 NAHAND + NARADR。`IvanBomber` 只挂弹，保留 ROF=50、Range=1.5、CellRangefinding 和 FireOnce；投放当帧不扣目标生命，不产生普通炮弹。友军可手动挂弹，自动索敌不选择友军，也不覆盖已有炸弹。
- 炸弹保存源实体、投放阵营、源类型、投放帧与到期帧。普通炸弹使用原版 CombatDamage 的 IvanTimedDelay=450、IvanDamage=400、IvanWarhead=IvanWH；目标移动、基地车部署后仍保留同一炸弹与时间。爆炸发生在目标当前坐标，建筑使用占地中心；源单位死亡不会取消炸弹。目标提前死亡或售卖会就地引爆一次。进入运输车的炸弹到期后等待卸载；隐藏乘员随宿主销毁时不在场内额外引爆。
- ENGINEER / SENGINEER 的 DefuseKit 现在执行拆弹，不扣血、不修复目标、不消耗工程师。保留原版 1.5 格距离与射击事件；停止可取消接近 / 拆弹。普通建筑占领 / 修复仍使用建筑交互流程。
- 标记使用原版 `bombcurs.shp` 和 `mousepal.pal`，六个阶段均分整个 450 帧寿命，每 8 帧切换一对图像。保留相同画布，避免闪烁时图标跳动；图标位置跟随目标渲染插值。投放方始终可见，其他阵营由 BombSight=4 的在场工程师探测。原版第 13 帧死亡炸弹图像不启用。
- IVAN 登入 FV 后 IFVMode=7，切换高科技炮塔和 CRNuke，主动攻击 / 被击毁时自爆并留下辐射；卸载恢复普通模式，乘员死亡不叠加伊文自身爆炸。
- 天启保留原版 800 HP、Cost=1750、NAWEAP + NATECH、120mmx 双发及 MammothTusk 对空双发；补入 APMUZZLE / APOCEXP。自疗读取 SelfHealing 和 General.RepairRate（当前 14 帧），每次恢复 1 HP，到 ConditionYellow（当前 400 HP）停止，独立于 RepairStep=8，免费且不会复活死亡对象。该通用车辆能力也补齐现有采矿车的 SelfHealing；步兵自疗、老兵能力仍未开放。

伊文自身死亡爆炸使用原始 IvanBomber 的 death-only Damage 与 IvanWH。对于天启等没有显式 DeathWeapon 的普通爆炸，使用原版 DefaultDeathWeapon / DeathWH，并用 `replica.ini [Simulation] DefaultDeathDamagePercent=5` 计算最大生命的兼容附带伤害（天启为 40）；没有再把主炮 120mmx 当作死亡炮弹。此百分比参考 OpenRA 的 ApocExplode=40，是待原版测量的兼容值，不代表已经确认原版公式。死亡画面使用原版 Explosion 列表的第一个可配置动画，随机碎片与动画选取尚未还原。InfDeath=6 已接原版 AudioVisual.InfantryExplode=S_BANG34，避免伊文炸死步兵后仍播放普通倒地。

操作：选中伊文点击地面单位 / 建筑挂弹；对友方运输车默认挂弹，Alt+点击登车。先右键取消伊文选择，再选工程师点击挂弹目标拆弹。默认 Sandbox 仍同时提供两阵营建造列表；非 Sandbox 按原版苏军前置与费用生产。未提供远程引爆，符合原配置 CanDetonateTimeBomb=no；Death Bomb 是 Ares 恢复的删减功能，本轮没有加入。

参考 [YRpp BombClass / 时间与所有者](https://github.com/Phobos-developers/YRpp/blob/a823ada387386a18ddadf41b4d7b46f295f94fdb/BombClass.h)、[BombListClass / 挂弹与探测](https://github.com/Phobos-developers/YRpp/blob/a823ada387386a18ddadf41b4d7b46f295f94fdb/BombListClass.h)、[Ares 对原版图标与删减功能的说明](https://ares-developers.github.io/Ares-docs/new/weapons/ivanbombs.html)、[Phobos 的挂弹与 BombSight 处理](https://github.com/Phobos-developers/Phobos/blob/c265252b21961345bfaebd3aa928fb721d166d74/src/Ext/WeaponType/Hooks.Bomb.cpp)。自疗节拍与原版家族行为参考 [OpenTS 维修 / 自疗文档](https://opents-developers.github.io/OpenTS/systems/repair/)；[OpenRA 爆炸定义](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/weapons/explosions.yaml)仅作为机制与兼容数值参考，没有照搬其中的短定时器或平衡数据。源码快照位于忽略目录 `build/ivan-apocalypse/research`。

从 `assets_origin` 逐字节复制 `ivan.shp`、`ivanicon.shp`、`bombcurs.shp`、`crivexp.shp`、`apmuzzle.shp`、`apocexp.shp` 六个资源，另外核对并复用现有天启 MTNK 车体 / 炮塔 / 炮管的六个 VXL/HVA 文件。未修改原始资源或 rules.ini / art.ini；来源与 SHA-256 见 `build/ivan-apocalypse/asset-sha256.json`。单位、13 帧标记、爆炸 / 炮口和实际场景图集位于该目录，已查看原始像素截图。

新增 `ivan_apocalypse_regressions` 和 `demo_ivan_apocalypse`。逻辑覆盖无即时伤害、450 帧边界、移动目标 / 源单位死亡 / 目标先死亡 / 售卖、运输 / MCV 部署、拆弹与取消、友军与 Bombable、伊文 IFV、InfDeath=6、天启双发 / 对空 / 自疗上限 / 费用 / 前置及重放；图形覆盖三剧场八方向、原版图标 / palette / 闪烁 / 探测、实际 SDL 点击拆弹、武器原始特效和场景辐射。

验证：Debug / Release CMake 构建通过；Debug 逻辑 CTest 26/26（13.72 秒），Release 全量 CTest 46/46（38.00 秒）通过，均未重试；Debug 专项图形测试亦通过。日志为 `build/ivan-apocalypse/{build-debug.log,build-release.log,ctest-core-debug-final.log,ctest-release-final.log,smoke-debug.log}`。截图已核对，新增 6 个资源及复用的 6 个 VXL/HVA 均与 assets_origin 逐字节相同。

校准边界：未运行纯净 RA2 逐帧对照。CellRangefinding 当前按双方格位比较距离；挂弹与拆弹的精确动画帧、隐藏炸弹恢复的先后次序、工程师被消耗时的炸弹处理、自疗阈值边界与死亡附带伤害仍需实测。音效、地形对象 / 桥梁挂弹、烧死、碎片与震屏继续待补。当时天启仍标记老兵能力未实现，CLEG / CIVAN 传送与 TANY C4 尚未开放；CLEG / TANY 后续接入见下一批。

## 海豹、谭雅、间谍与超时空兵（2026-10-18）

接入 GHOST / TANY / SPY / CLEG，继续保留原版 rules.ini、art.ini，所有主体和新增特效均来自 assets_origin。GHOST 的 Image=SEAL，雪地使用 AlternateArcticArt 对应 seala.shp；TANY、SPY、CLEG 分别使用 tany.shp、spy.shp、cleg.shp。主图与后半部阴影帧分开读取；建造图标复用原版四个 cameo。GHOST 在原规则中 TechLevel=-1，因此仅默认 Sandbox 开放，非 Sandbox 保持原版不可常规生产的限制。

- 海豹 / 谭雅保留 MP5 / DoublePistols 的原始伤害、射速和 HollowPoint。步兵两栖移动读取 Amphibious 地形倍率；水中使用 Tread / Swim / WetAttack / WetDie1 / WetDie2，受伤不会在水中卧倒。手动点击敌方 CanC4 建筑时切换独立 C4 流程，按最近占地格接近、播放放置动作，延迟 CombatDamage.C4Delay=.03 分钟（27 逻辑帧）强制摧毁建筑；不消耗突击队员，不产生普通炮弹，不使用伊文的 450 帧炸弹或图标。停止可取消尚未完成的放置，已安放的 C4 随目标保存。Sapper 对海军目标的直接爆破分支已接入，完整海军生产 / 作战仍属于后续范围。
- 间谍点击其他步兵取得 MakesDisguise 快照，保留被模仿类型和阵营，不依赖目标继续存活，也不造成伤害。己方看到真实间谍，未识破的敌方看到被模仿步兵和队色；DetectDisguise 警犬在原版探测半径内识破，移动不解除伪装。潜入复用现有建筑入口、接近和取消流程，只允许敌方 Spyable 建筑。电厂断电使用 SpyPowerBlackout=1000 帧；矿场转移敌方当时资金的 50%；兵营 / 战车工厂使己方之后生产的可训练步兵 / 车辆以老兵身份出厂；实验室记录盟军 / 苏军窃取科技条件，非 Sandbox 的 RequiresStolen*Tech 前置读取该条件。建筑不转移所有权，成功潜入后间谍被消耗。窃取科技专属单位仍需其自身能力接入后才会开放。
- 训练加成已实际作用于 VeteranAbilities 指定的 FIREPOWER、STRONGER、ROF 和 FASTER，读取 General 的原倍率；老兵图标复用 pips.shp 原版第 13 帧。此次没有实现战斗经验累积、精英武器、视野升级或步兵自疗，原有单位相应状态仍为 Partial。
- 超时空兵移动直接转移至可用的步兵子格，不沿普通路径行走；附近目的格被占用时确定性选择可落脚位置，不能落入水中、悬崖或建筑。恢复时间按三维距离（leptons）/ ChronoDistanceFactor=48 计算，至少 ChronoMinimumDelay=16 帧，保留 ChronoTrigger / ChronoRangeMinimum。恢复时能被攻击，不能继续移动或开火；期间发出的移动命令在恢复后执行，使用原版 WarpOut / WarpIn。
- NeutronRifle 和 CRNeutronRifle 使用独立 Temporal 通道。目标初始 WarpRemaining=最大 Strength×10，每逻辑帧按武器 Damage 扣除抹除量；最多本体加 50 个协作者，普通 HP 保持不变。冻结目标不能移动、开火、生产、修复或被普通炮弹、辐射、碾压、警犬、恐怖机器人寄生伤害。停止、移动、施法者死亡或 IFV 卸载后释放目标，抹除进度不当作普通受伤保留。完成后播放原版 WarpAway，静默移除目标及隐藏乘员，不引爆死亡武器、伊文炸弹或辐射，不生成普通尸体。支持 Warpable=no。时间光束为持续蓝色，冻结主体带蓝色调，恢复中的超时空兵半透明；渲染不推进权威计时。
- IFV 使用原 IFVMode：海豹 / 谭雅为 4（CRMP5），间谍为 2（机枪），超时空兵为 10（CRNeutronRifle）。通道由当前乘员模式验证，卸载后不会遗留时间武器。

操作提示已加入模拟面板：突击队员点击敌方建筑安放 C4；间谍点击步兵伪装、点击敌方建筑潜入；超时空兵点击地面传送、点击敌方目标抹除。雷达迷雾重置和超武计时重置依赖尚未实现的系统，此类潜入暂时拒绝并保留间谍，不伪造收益；这也延续此前超武充能 / 释放不纳入当前范围的约定。

机制依据：[Phobos TeleportLocomotion 距离恢复计算](https://github.com/Phobos-developers/Phobos/blob/c265252b21961345bfaebd3aa928fb721d166d74/src/Ext/TechnoType/Hooks.Teleport.cpp)、[Temporal 初始值与协作 WarpPerStep](https://github.com/Phobos-developers/Phobos/blob/c265252b21961345bfaebd3aa928fb721d166d74/src/Ext/Techno/Hooks.cpp)、[YRpp TemporalClass 通道 / 生命周期](https://github.com/Phobos-developers/YRpp/blob/a823ada387386a18ddadf41b4d7b46f295f94fdb/TemporalClass.h)。[Ares Temporal 文档](https://ares-developers.github.io/Ares-docs/new/warheads/temporal.html)确认原版默认不按剩余健康调整抹除量；[间谍效果文档](https://ares-developers.github.io/Ares-docs/new/buildings/spyeffects.html)用于区分原有效果与扩展功能。另对照 [OpenRA RA2 步兵定义](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/rules/allied-infantry.yaml)和原版序列资源，未照搬其中另行平衡的步兵数值或 300 帧 PortableChrono 冷却。研究快照位于 build/commandos/research。

验证覆盖：C4 强制销毁高血量建筑 / 27 帧边界 / 取消 / CanC4；游泳与上岸；无限距离伪装、警犬识破；资金实际转移、断电过期、出厂老兵与实际火力加成、科技条件；距离传送和恢复期排队；冻结不扣 HP / 不能移动 / 免疫普通伤害、多人加速、停止 / 死亡释放、Warpable、静默抹除运输车乘员与自爆卡车、IFV 切换以及逐帧重放。图形测试 --commando-smoke-test 检查原版四类主体、雪地海豹、三剧场八方向六姿态、时间特效原始帧、蓝色光束、冻结主体和敌我不同伪装；截图已目视检查。

最终验证：Debug / Release CMake 构建通过，Debug 逻辑 CTest 27/27（22.29 秒），Release 全量 CTest 48/48（41.94 秒），Debug 专项图形检查通过。冻结建筑的施工 / 出口计时中断、寄生恐怖机器人随宿主静默抹除、施法者死亡及训练火力加成均有额外回归。原始 rules.ini / art.ini 未修改，8 个新增 SHP 与 4 个复用图标均与 assets_origin 逐字节相同；日志、校验清单和已查看的 PNG 图集位于 build/commandos/。

校准边界：没有运行纯净 RA2 做逐帧对照。C4 贴近建筑的具体门位、原版时间通道解除后的渐显延迟、传送与光束颜色 / 闪烁的精确节拍仍待校准；当前解除通道即释放目标。尚无战争迷雾，因此 MoveToShroud 限制没有实际隐藏地图可约束；友军强制时间冻结、剩余海军特殊单位、声效与完整老兵 / 精英系统继续待补。下方通用校准项仍适用。

## 第一批海军、船厂与水域（2026-10-18）

按本轮确认的范围，接入盟军船厂 GAYARD、苏军船厂 NAYARD，以及两栖运输艇 LCRF / SAPC、潜艇 SUB、海蝎 HYD、神盾巡洋舰 AEGIS。当批的驱逐舰 DEST、航母 CARRIER、无畏 DRED、海豚 DLPH、乌贼 SQD 尚未开放；其后接入见下一节。

- 水面舰艇使用原版船舶 locomotor 和 FLOAT 地形倍率，只能在水域航行；两种运输艇使用原版 hover locomotor，可跨水陆。示范地图新增原版 water01 / shore09 的 2×2 TMP 地块，读取每格 LandType，依次衔接 ROUGH、BEACH、WATER。TMP 使用原版 52 字节格头、900 像素菱形和额外图像；三剧场分别使用 iso*.pal，不套矿物或单位调色板。
- 船厂按原版 WaterBound 校验整个占地，地面建筑拒绝水域；预览和落地共用 World.canPlaceBuilding。生产按 Naval、Factory、原阵营、科技前置与费用筛选，盟军船厂不能生产苏军潜艇；在外围空闲水格出船，出口堵塞时保留完成的队列并重试。断电暂停海军生产。船只点击己方船厂可进入外围维修泊位，按原版 NumberOfDocks、RepairStep、URepairRate 修复并扣费；停止、船厂死亡或转手取消任务。
- LCRF / SAPC 保留 Passengers=12、SizeLimit=6，按乘客 Size 计容量：可装 12 名普通步兵或 4 辆 Size=3 的坦克。装入后保留实体、生命和阵营，释放场内占位；深水拒绝卸下不能游泳或行水的乘客，返回可通行陆地后逐个卸下。卸下的车辆驶离出口，避免第一批乘客堵死后续卸载；载人运输车不能再次作为嵌套货物装船。
- SUB 保留 600 HP、4 格传感器、CloakDelay=.02 分钟和 9 个 CloakingStages，开火遵循 DecloakToFire=no；受正伤害后重新显形。己方潜艇以半透明船体显示，敌方潜艇在传感器范围外不绘制、不选取、不自动索敌；被探测后可见，但普通海蝎等 NavalTargeting=0 武器仍不能伤害水下目标。原版 SUBT 是 VXL / HVA 鱼雷，按 SubTorpedo 的速度和转向追踪，保留气泡尾迹；遇岸终止，发射者死亡不取消已在水中的鱼雷。
- HYD 使用原版 FlakTrackGun 对地、FlakWeapon 对空，按目标高度选择。AEGIS 保留 Medusa 的 Damage=100、ROF=15、Range=12、Speed=120、TurboBoost 对空 1.5 倍和 OmniFire，不等待缓慢船体旋转；只攻击空中目标。MEDUSA 使用原版 32 方向主体，排除文件后半段 32 个空阴影条目。
- 移动尾流使用 WAKE1，水面命中使用 CombatDamage.SplashList，鱼雷使用 BBBLELRG；动画读取原版 Layer、Translucency / Translucent 和 YDrawOffset，尾流及气泡在船体下绘制。潜艇状态、维修任务、乘员与射弹继续由固定帧仿真维护并参与重放哈希，渲染只读快照。

正常运行后点击模拟面板“查看海岸”前往海军区，“返回基地”复位视角。侧栏仍同时列出双方单位；选中步兵或车辆点击运输艇装载，运输艇靠岸后按 U 卸载。受损船只点击己方船厂即可维修，按 S 停止。

研究依据：本地原版 rules.ini / art.ini 为数值和目标规则来源；对照 [OpenRA RA2 的舰艇共用规则](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/rules/defaults.yaml)、[盟军海军](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/rules/allied-naval.yaml)、[苏军海军](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/rules/soviet-naval.yaml)、[原版动画及调色板映射](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/sequences/misc.yaml)和 [OpenRA 原生 TMP 解码](https://github.com/OpenRA/OpenRA/blob/dde39344a0f25af15b039e6a2e4dbf046e624c04/OpenRA.Mods.Cnc/SpriteLoaders/TmpTSLoader.cs)。隐身与开火的边界另对照 [Phobos Cloak 钩子](https://github.com/Phobos-developers/Phobos/blob/c265252b21961345bfaebd3aa928fb721d166d74/src/Ext/Techno/Hooks.Cloak.cpp)。没有照搬 OpenRA 的另行平衡数值与潜艇隐身时间；缓存见 build/naval/research。

本批从 assets_origin 新复制 20 个二进制资源，复用 12 个船体 / 图标文件，均逐字节核对；清单为 build/naval/asset-sha256.json。未修改原版规则文件，运行目录 INI 与源文件仅有既存的 LF / CRLF 换行差异。

验证：Debug / Release 构建通过；Debug 逻辑 CTest 28/28（21.87 秒），Release 全量 CTest 50/50（51.61 秒），包含 demo_naval。新增 naval_regressions 覆盖水域寻路、地形放置、阵营生产、出口堵塞、潜艇探测 / 受击 / 发射者死亡、鱼雷撞岸、神盾全向防空及速度、双方装卸 / 容量 / 深水限制、付费维修、原版 TMP 损坏文件检查与确定性重放。图形检查覆盖五类船体和鱼雷的八方向 / 三剧场、海岸原始地块、尾流 / 水花 / 气泡、己方潜艇半透明与敌方隐藏，以及真实 SDL 鼠标登船和返岸卸载；图集与场景 PNG 已目视检查。日志和截图在 build/naval。

校准边界：尚未运行纯净 RA2 逐帧对照。当前沿用单格车辆寻路和外围出船 / 维修泊位，尚未复刻船厂内部可通行行数与精确下水门位；悬浮高度、起伏 / 惯性、完整海浪调色板循环、神盾线状导弹尾迹、重舰倾斜下沉和碎片、音效待补。维修费用沿用现有按伤量 / 造价 / RepairPercent 的兼容公式；水花大小按伤害档位选择、移动尾流每 6 帧、鱼雷气泡每 3 帧，均需继续对照原版。以上不影响本批运输、交战和生产闭环。

## 第二批海军：舰载机、无畏导弹与海洋生物（2026-10-18）

本批开放驱逐舰 DEST、航空母舰 CARRIER、无畏战舰 DRED、海豚 DLPH 和巨型乌贼 SQD。十种原版海军类型现均可由对应阵营船厂生产，沿用原版费用、前置、占位、水域寻路和船厂维修；航母与无畏保留 CanPassiveAquire=no，需玩家指定目标。

- **驱逐舰**：主炮 155mm 对陆地 / 水面目标，Damage=60、ROF=110、Range=8；8 格声呐识别潜艇和海洋生物。NavalTargeting=1 对水下目标调度一架原版 ASW 反潜机，投放 ASWBomb 深水炸弹（Damage=50）后返舰；实际攻击来自飞机，虚拟 ASWLauncher 不直接扣血。返航补给等待 150 帧，被击落后等待原版 SpawnRegenRate=400；空甲板使用 DESTWO。
- **航母**：25 格指令射程调度三架原版 HORNET，每架保留 75 HP、Speed=12、ROT=3 和一发弹药。每次出击发射 HornetBomb（Damage=40），返舰后经过 SpawnReloadRate=150 再出击；损失后经过 SpawnRegenRate=600 补充。虚拟 HornetLauncher 的 Damage=1 不作为战斗伤害。停止或失去合法目标时召回机群，返航追踪移动母舰；母舰死亡会清理所属飞机。飞机为可被防空命中的独立实体，不接受玩家独立移动命令。
- **无畏战舰**：保留射程 25、最小射程 8、两个独立 DMISL 发射位及 DREDWO 空架外观。导弹读取 General.DMisl* 的等待 20 帧、抬升 60 帧、初始 / 最终俯仰 0 / .5、加速度 .8、巡航高度 768、机身长度 128，以及 CombatDamage.DMislWarhead 对应 DMISLWH、Damage=300；没有套用 V3 的伤害和发射等待。导弹离架后发射位等待 80 帧重新补充，允许前一轮导弹仍在飞行时补充下一轮。发射前停止 / 移动取消准备；已起飞导弹不因母舰停止或死亡消失，可被防空拦截且被拦截后不再结算落点伤害。航母与无畏均支持 Ctrl 强制攻击地面位置。
- **海豚**：使用原版 dlph.shp，116 个主体帧及后半段阴影；游动和开火的原版方向顺序分别处理。保留 200 HP、4 格声呐、隐身和 TypeImmune。SonicZap 使用独立行进声波及原版 Damage=4 / AmbientDamage=10 / Range=6 / ROF=120；波前沿水域推进，可扫过路径上的目标，并解除乌贼缠绕。同类海豚免疫声波；不会把声波当作普通子弹或光棱激光。
- **乌贼**：原版 sqd.shp 的 296 帧都是主体，NoShadow=yes，不能按步兵习惯截成 148 帧。对普通舰艇使用 SquidGrab：LimboLaunch 释放自己的外部占位，建立双向宿主关系，限制宿主移动 / 开火并持续造成伤害，ParasitePlus 在红血时执行 Culling。宿主死亡后乌贼在邻近合法水格恢复占位，不会作为被载乘客一并销毁。对海豚 / 乌贼等 Organic 目标改用 SquidPunch，不缠绕。声波或达到 SuppressionThreshold=250 的伤害可令乌贼脱离，释放后船只恢复行动，乌贼保留 50 帧兼容抑制恢复时间，避免同一帧立即重新附着；原版硬编码恢复时间仍待校准。

隐身共用原来的 Submerge 状态：CloakingSpeed 作为每阶段帧数，较小值更快，乌贼的 5 比潜艇 / 海豚的 1 慢；己方半透明，敌方依声呐可见性决定绘制与选取。缠绕使用原版 SQDG 合并文件；逐帧核对确认其布局为三个 80 帧块，每块含八方向各 10 帧，进入段与两段循环分别取帧。按主 SQDG 的 AltPalette=yes 使用单位调色板与攻击方队色；没有使用爆炸调色板造成的红橙伪色。另接入原版 SQDWAKE 和 DURASMOKE。

权威状态包括 NavalSpawner 的固定发射位、Docked / Attacking / Returning / Reloading / Regenerating、补给时刻和目标；NavalAircraft 的母舰 / 目标 / 整数轨迹；独立 DMISL；SonicWave 的推进和直接命中状态；原有 leap::State / Infested 的乌贼关系。均参与重放哈希。`simulation/naval_aircraft.*` 只计算确定性飞行几何，World 负责出击、弹药、返航、损失补充及伤害；渲染不会推进这些计时。

研究对照 [OpenRA RA2 盟军海军](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/rules/allied-naval.yaml)、[苏军海军](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/rules/soviet-naval.yaml)、[海豚主体与方向序列](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/sequences/allied-infantry.yaml)、[乌贼序列](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/sequences/soviet-infantry.yaml)、[声波 AreaBeam](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/weapons/zaps.yaml)。OpenRA 的当前乌贼只有 SquidPunch，故没有将其当作原版缠绕实现；寄生生命周期另据 [YRpp ParasiteClass](https://github.com/Phobos-developers/YRpp/blob/a823ada387386a18ddadf41b4d7b46f295f94fdb/ParasiteClass.h)、舰载发射位据 [YRpp SpawnManagerClass](https://github.com/Phobos-developers/YRpp/blob/a823ada387386a18ddadf41b4d7b46f295f94fdb/SpawnManagerClass.h)及 [Phobos 的发射 / 补充钩子](https://github.com/Phobos-developers/Phobos/blob/c265252b21961345bfaebd3aa928fb721d166d74/src/Ext/Techno/Hooks.Misc.cpp)。数值仍取本地原版 INI，不照搬开源项目的另行平衡值。研究源文件缓存于 build/naval-special/research。

本批核对 34 个原版资源文件：三个舰体及 DESTWO / DREDWO、ASW / HORNET / DMISL 的 VXL / HVA，海豚 / 乌贼 SHP，SQDG / SQDWAKE / 八个方向参考动画、DURASMOKE 和五个图标。来源及 SHA-256 见 build/naval-special/asset-sha256.json。复用已有船体和图标，缺失文件从 assets_origin 原样复制；源资源和原始 rules.ini / art.ini 未修改。

验证包括：五类船厂生产；非 Sandbox 航母科技与 2000 费用；虚拟发射器不直接伤害；三机投弹 / 返航 / 停止、精确 600 帧损失补充、母舰死亡清理；驱逐舰空海武器切换；双 DMISL 的独立实体、最小射程、离架后停止、防空拦截与强制地面攻击；缠绕限制行动 / 持续伤害 / 红血处决 / 邻水退出、Organic 拳击、己方声波解救；隐身速度与声呐及逐帧重放。`--naval-special-smoke-test [截图.bmp]` 另检查八方向 / 三剧场原版舰体、空甲板、飞机 / 导弹、海豚 / 乌贼三个姿态、原版血条、SQDG 与单位调色板、青色波前、真实 SDL 点击海豚及 900 帧联合战斗；图集和场景截图已目视核对。

最终验证：Debug 逻辑 CTest 29/29（25.39 秒），Release 全量 CTest 52/52（59.88 秒）通过；专项 Debug 图形检查通过。构建、回归日志与已查看的 PNG 图集位于 build/naval-special。

校准边界：尚未运行纯净 RA2 逐帧对照。飞机转弯 / 进场减速、分批起飞的 10 帧间隔、无畏分段爬升 / 平飞 / 俯冲曲线仍为可重放的兼容实现；飞行单位尚无完整空中避让、坠机碰撞伤害与音效。声波有行进范围伤害和青色弧形波前，尚无原版背景折射；半宽使用 NavalCombat.SonicWidthLeptons=128。乌贼原版硬编码伤害周期未从公开接口确定，当前明确配置 NavalCombat.SquidDamageFrames=11，依据原版方向动画的 9..19 循环，不将已注明“ignored”的 ROF=99 当周期。重舰倾斜下沉、完整摇晃 / 翻转、缠绕结束过渡帧和精英武器仍待校准；现有死亡保留水花及正常移除。独立可指挥的固定翼 / 直升机系统已在下一批接入，见下文。

## 独立空军与四机位机场（2026-10-19）

接入 ORCA 入侵者、BEAG 黑鹰、SHAD 夜鹰运输机、ZEP 基洛夫。使用 assets_origin 的 FALC / BEAG / SHAD / ZEP 原版 VXL、HVA、四种 Cameo；基洛夫炸弹使用 ZBOMB 原版 VXL / HVA，弹药标记使用 PIPS2 第 13 帧和 palette.pal。15 个复用 / 新增文件已逐字节校验，清单在 build/aircraft/asset-sha256.json；未改动原始 rules.ini / art.ini。

- GAAIRC / AMRADR 读取 NumberOfDocks=4 和原版 art 的四个 DockingOffset。飞机在生产、出击、返航期间保留各自机位，队列也占预定容量，满四架后拒绝继续排队；战损释放机位。飞机在机场停靠时正常显示，不再被地面载具的建筑前景重绘覆盖。
- ORCA / BEAG 从机场生产，保留生命 150 / 200、价格 1200、Speed=14、ROT=3、FlightLevel=1500、Ammo=1，分别使用 Maverick / Maverick2 的原版双导弹。第一枚发射即扣一格弹药，后续 Burst 完成后返航；取消齐射不会退回已发射弹药。落地按 ReloadRate=.3，即 270 个供电逻辑帧补一格，并按原版维修费率维修。停电暂停补给；恢复后继续剩余帧数。
- 非沙盒保留黑鹰仅韩国（Alliance）、入侵者禁止韩国的限制，以及原版科技 / 扣费规则；默认混合沙盒同时显示两种飞机。S 或点击己方兼容机场可返航 / 转场；机场损失后，空中飞机优先重新预定其他己方机场，无可用机场且需降落时坠毁。停靠飞机随机场出售 / 毁坏移除。
- SHAD / ZEP 从对应战车工厂生产，复用开厂准备 / 顶部出厂 / 收尾流程，出厂期间命令延后到离开屋顶再执行。两者读取各自 JumpjetSpeed / Climb / Crash / Accel / TurnRate / Height。夜鹰移动到目的地后寻找可用陆地降落，五格步兵容量、机枪、登机、U 落地卸载及乘员死亡清理完整贯通；水面、悬崖、建筑、地面单位阻挡降落。基洛夫 BalloonHover 永不落地，保留 2000 生命、自疗、慢速移动和 BlimpBomb 250 伤害 / ROF=50。
- BlimpBombP 的 Vertical / Acceleration 独立解析，炸弹从原版 FLH 下落，保持发射时的平面坐标，不追踪目标，也不套用坦克抛物线。地面命中继续复用三维溅射、护甲、建筑占地及步兵子格身体半径。
- 空中实体可框选、点击、编组移动、攻击及停止；落地与升空使用真实高度切换 AG / AA 命中域。机身和地面阴影分离，SHAD 采用 ShadowIndex=2，HVA 原版两帧旋翼随逻辑时间播放；血条 / 弹药 / 乘员图标随机身高度。被击落保留原版体素下坠，再显示地面爆炸 / 水花。

`simulation/aircraft.*` 负责确定性整数飞行几何；World 的 Pilot 保存任务、目标点、机场 / 机位、弹药、补给计数和维修时刻，连同坠落状态参与重放哈希。AircraftMission 为 Landed / Moving / Attacking / Returning / Landing / Holding，表现层不推进任务计时。

正常演示已放置机场、额外电厂、两种战斗机、夜鹰、基洛夫及登机步兵，点击“查看机场”可定位。侧栏沿用载具页，飞机提示缺机场，运输机 / 飞艇提示缺对应战车工厂。

参考依据：本地原版 rules.ini / art.ini；[OpenRA RA2 aircraft.yaml](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/rules/aircraft.yaml) 的机场补给、直升机运输与空军生产分类；[Phobos Aircraft Hooks](https://github.com/Phobos-developers/Phobos/blob/c265252b21961345bfaebd3aa928fb721d166d74/src/Ext/Aircraft/Hooks.cpp) 的攻击 / 齐射 / 返航任务与 ShouldLoseAmmo；[Phobos Aircraft Body](https://github.com/Phobos-developers/Phobos/blob/c265252b21961345bfaebd3aa928fb721d166d74/src/Ext/Aircraft/Body.cpp) 的机位与降落朝向；[Phobos Pips](https://github.com/Phobos-developers/Phobos/blob/c265252b21961345bfaebd3aa928fb721d166d74/src/Ext/Techno/Hooks.Pips.cpp) 的 PIPS2 / AmmoPipFrame=13。仅参考机制，未复制其引擎代码。

验证：aircraft_regressions 覆盖四机位和队列上限、生产分类、双弹 / 弹药 / 取消、返航 / 补给、运输与水域降落、垂直炸弹、防空击落 / 下坠、转场与机场损失、战车工厂顶部出厂和命令延后、重放一致性。demo_aircraft 运行 1200 帧真实 SDL / OpenGL，检查四类体素八方向 × 三剧场、夜鹰旋翼两帧差异、实际点击选择、双机生产 / 投弹 / 返航 / 补给、运输卸载及 ZBOMB；截图已目视检查。

最终验证：Debug 逻辑 CTest 30/30（68.88 秒）、空军专项图形与完整练习场启动通过；Release 全量 CTest 55/55（41.66 秒）通过。15 个原版二进制资源逐字节匹配，原始规则未改动；日志、来源清单和已查看图集位于 build/aircraft。

校准边界：未运行纯净 RA2 逐帧轨迹对照。起降 / 转弯 / 进场末段减速、机场维修顺序、战车工厂顶部离场高度和坠落姿态为可重放的兼容实现；尚无完整空域避让、飞机碰撞、坠机附带伤害、音效、战斗经验和精英武器。PDPLANE / CARGOPLANE 属空投支援流程，未加入可生产列表；原版 TechLevel=-1 的 HIND 等保留未开放。

## 校准边界与后续范围

这些是当前可测试的兼容参数，尚未用纯净 RA2 1.006 逐帧校准：

| 项目 | 当前实现 |
|---|---|
| 子格偏移 | 相对格心使用 milli-cell 坐标 (-250,-250)、(-250,250)、(250,-250) |
| 空中占位 | 独立复用三子格偏移，按空中层避让其他飞行兵；尚非完整原版空域 / 机群模型 |
| 飞行 / 坠落 | 原版每单位参数驱动整数高度、加速和转向，提前一格爬升避楼，死亡按 Crash 下降；精确惯性、避楼轨迹、Fly / Hover 每帧延迟 3 与死亡节拍尚待原版逐帧校准 |
| 防空散布 | 固定种子、均匀角度及整数半径；BallisticScatter 确定范围，概率分布和对空倍数尚待纯净 RA2 对照 |
| 恐惧 / 起身 | 受到正伤害触发卧倒，最后一次受击后 120 个逻辑帧恢复；不是完整原版恐惧阈值模型 |
| 爬行速度 | Crawls 步兵使用行走速度的 67%，地形 Foot 倍率继续生效 |
| 动作 / 开火 | Walk 每帧延迟 3 个逻辑帧，Fire 每帧延迟 1；GI / CONS 在动作起点后 2 / 6 帧发射，CONS 保留末帧直到事件触发 |
| 兵营出口 | GAPILE 偏移 (1,2)，NAHAND 偏移 (2,2)，直接出现在建筑外可用子格，再走向集合点；未实现门内遮挡过渡 |
| 工程师进入 | 走至可达的外围子格，再按步速进入最近的基础格；至少 12 逻辑帧，由 EngineerEntryFrames 配置。进入期间保留外部子格占位，取消后走回；尚未逐帧校准原版门位与进入时长 |
| 碾压 | 车辆移动线段到步兵距离小于 380 milli-cell；部署过渡阶段的保护时机仍待校准 |

Idle 等附加序列已解析，但闲置花样动作尚未调度。死亡支持 Die1 / Die2、InfDeath=5 电击、InfDeath=6 爆炸及 InfDeath=7 辐射融化；烧死等其他特殊死亡继续待补。当前规则没有工程师占领血量阈值配置，可占领的完整建筑由一名工程师直接占领。剩余 IFV 特殊乘员、其他特殊兵种、维修焊接粒子、驻军建筑废墟持久化、升级和声音仍在后续范围。双方工程师已补齐拆弹；间谍取得的出厂老兵已有基础能力，战斗经验 / 精英武器与步兵自疗仍有待补充。原始资源池尚缺所需语音和枪声音频包。
