# assets 目录说明

这个目录只保留 **当前项目实际使用** 的资源。
完整原始资源池保留在同级目录：

- `assets_origin`

后续如果要引入新资源，请遵循下面这套流程：

1. 先在 `assets_origin` 中找到原始文件。
2. 再按本目录的分类规则复制到 `assets`。
3. 多文件对象必须建立独立子目录，不要重新回到“大平铺”结构。
4. 优先按“用途”分类，其次再按“阵营 / 单位 / 建筑 ID”分层。

## 当前目录结构

- `settings`
  - 项目实际使用的配置文件
  - 例如 `art.ini`、`rules.ini`

- `palettes`
  - 调色盘和 `vpl` 查表
  - `theater`：剧场相关 palette
  - `resources`：原版矿物覆盖层调色盘 `temperat.pal`、`snow.pal`、`urban.pal`
  - `ui`：界面、侧栏、覆盖层 palette
  - `voxel`：体素渲染相关 `vpl`

- `buildings`
  - 当前项目使用的建筑资源
  - 按阵营分层：
    - `allied`
    - `soviet`
    - `civilian/american_house`：CAHSE01 中立民居，cthse01 / cahse01 / cuhse01 分别用于温带、雪地和城市；前四帧为正常、受损、驻军加固、废墟
  - 每个建筑再按建筑 ID 建目录
  - `buildings/turrets`：爱国者 `sam`、哨戒炮 `laser`、防空炮 `flaktur` 的原版 VXL/HVA；巨炮炮塔和炮管保留在 `allied/gtgcan`
  - 间谍卫星使用 `allied/gaspst`，哨戒炮使用 `soviet/nalasr`；防御建筑同时保留实际使用的温带、雪地、城市主体 / 建造动画
  - 建筑逻辑 ID 与美术 ID 分开，例如 `ATESLA → GAPRIS`、`TESLA → NATSLA`、`GAWEAT → GAWETH`、`GASPYSAT → GASPST`，不要把资源文件名直接作为模拟实体类型
  - 例如：
    - `buildings/allied/gapowr`
    - `buildings/soviet/napowr`

- `vehicles`
  - 当前项目使用的载具资源
  - 多文件载具按对象目录组织
  - `vehicles/ifv`：fv 主体和 fvtur 空载炮塔；fvtur1 为机枪 / 狙击炮塔，fvtur2 为工程师维修臂，fvtur3 为磁暴乘员炮塔，均配套原版 HVA
  - `vehicles/terror_drone`：原版 `dron.shp`（88 主体帧 + 88 阴影帧）和 `dronp.shp`（32 扑击方向 + 32 阴影帧）；保留载具整格占位，按 SHP 绘制
  - `vehicles/demolition_truck`：自爆卡车 DTRUCK 使用原版 `trucka.vxl` / `trucka.hva`，没有附加炮塔
  - `vehicles/apocalypse_tank`：天启逻辑 ID 为 APOC，原版 Image=MTNK；复用逐字节核对的 mtnk / mtnktur / mtnkbarl 及 HVA，与灰熊的 GTNK 图像区分
  - `vehicles/v3_launcher`：原版 v3 载弹车体、v3wo 空架车体、v3rocket 独立火箭及 HVA；火箭不做队色重映射
  - `vehicles/prism_tank`：sref 主体和 sreftur / sreftur1 / sreftur2 / sreftur3 四阶段炮塔，均配套原版 HVA
  - 例如：
    - `vehicles/rhino_tank`
      - `htnk.vxl`
      - `htnk.hva`
      - `htnktur.vxl`
      - `htnktur.hva`
      - `htnkbarl.vxl`
      - `htnkbarl.hva`

  - 海军：`landing_craft/lcrf`（LCRF）、`amphibious_transport/trs`（SAPC）、`submarine/sub`、`sea_scorpion/hyd`、`aegis_cruiser/aegis`，全部原版 VXL / HVA

- `infantry`
  - `allied/seal`：GHOST 使用 seal.shp，雪地切换 seala.shp，均使用原版 SealSequence，含水中动作
  - `allied/tanya`、`allied/spy`、`allied/chrono_legionnaire`：原样复制 tany.shp、spy.shp、cleg.shp，主体与后半部阴影分开加载
  - `allied/attack_dog`：`adog.shp` / `adogp.shp`
  - `soviet/attack_dog`：`dog.shp` / `dogp.shp`
  - 两种警犬主体各 172 帧，独立扑击图各 32 方向；后半部均为空阴影。六个新 SHP 原样复制自 `assets_origin/sprites`，不裁剪或重绘

- `icons`
  - 侧栏和建造系统使用的 cameo 图标
  - 当前按用途分类：
    - `base`
    - `defense`
    - `infantry`
  - `allied/seal`：GHOST 使用 seal.shp，雪地切换 seala.shp，均使用原版 SealSequence，含水中动作
  - `allied/tanya`、`allied/spy`、`allied/chrono_legionnaire`：原样复制 tany.shp、spy.shp、cleg.shp，主体与后半部阴影分开加载
    - `vehicles`
  - 同一个分类下可以同时放盟军和苏军图标，运行时按文件名递归查找

- `ui`
  - 侧栏壳体、按钮、覆盖层等 UI 资源
  - 目前侧栏按主题分层：
    - `ui/sidebar/sidec01`：盟军
    - `ui/sidebar/sidec02`：苏军
  - `ui/unit_overlays/common`：单位血条和选中标记，内容与 `assets_origin/sprites` 一致
  - `ui/building_actions`：原样复制的 `wrench.shp` 和 `mouse.shp`；维修标记与维修 / 售卖光标均使用 `palettes/ui/mousepal.pal`，不参与队色重映射
  - `ui/ivan_bombs/bombcurs.shp`：原版 13 帧炸弹标记，使用 mousepal.pal 并保留各帧相同画布；前 12 帧为六组闪烁图像，第 13 帧为原版未启用的死亡炸弹图像
    - `pipbrd.shp`：第 0 帧为车辆边框，第 1 帧为步兵 18×4 小号边框（帧号从 0 起）
    - `pips.shp`：第 15 / 16 / 17 帧为绿 / 黄 / 红的 2×2 血格
    - `select.shp`：原版选中标记备选素材；当前按界面要求不绘制步兵脚边括号
    - 以上 UI 使用 `palettes/ui/palette.pal`，保持素材原色，不参与队色重映射

- `infantry`
  - `allied/seal`：GHOST 使用 seal.shp，雪地切换 seala.shp，均使用原版 SealSequence，含水中动作
  - `allied/tanya`、`allied/spy`、`allied/chrono_legionnaire`：原样复制 tany.shp、spy.shp、cleg.shp，主体与后半部阴影分开加载
  - 原版步兵 SHP，按阵营 / 单位分目录
  - `allied/gi/gi.shp`：美国大兵 E1，744 帧，前 372 帧为主体动作
  - `soviet/cons/cons.shp`：动员兵 E2，602 帧，前 301 帧为主体动作
  - `common/engineer/engineer.shp`：盟军 ENGINEER / 苏军 SENGINEER 共用，506 帧，前 253 帧为主体动作
  - `allied/sniper/snipe.shp`：英国狙击手 SNIPE，602 帧，前 301 帧为主体动作
  - `soviet/flak_trooper/flakt.shp`：防空步兵 FLAKT，602 帧，前 301 帧为主体动作
  - `allied/rocketeer/rock.shp`：火箭飞行兵 JUMPJET，854 帧，前 427 帧含飞行、悬停、空中开火与坠落动作
  - `soviet/tesla_trooper/shk.shp`：磁暴步兵 SHK，602 帧，前 301 帧为 ConSequence 主体动作，后半段为空
  - `soviet/desolator/deso.shp`：辐射工兵 DESO，644 帧，前 322 帧含步行、开火、部署、释放和收起；后半段为空
  - `soviet/terrorist/trst.shp`：恐怖分子 TERROR，376 帧，前 188 帧为主体、后 188 帧为空阴影；原版自爆不播放 FireUp / FireProne，不使用其越界射击序列
  - `soviet/crazy_ivan/ivan.shp`：疯狂伊文 IVAN，534 帧，前 267 帧为主体；按 IvanSequence 播放步行、卧倒和挂弹动作，原版 FireUp=6
  - 后半段不作为独立阴影绘制；主体使用 unit 调色板和队色重映射，工程师、狙击手、防空步兵与飞行兵文件均与 `assets_origin/sprites` 原文件逐字节一致

- `effects`
  - 战斗及采集特效资源
  - `harvesting/oregath.shp`：原版矿车采集动画，8 个方向 × 15 帧
  - `infantry/mgun-*.shp`：八方向机枪火光；`piff.shp`、`piffpiff.shp`：步兵武器命中特效
  - `infantry/electro.shp`：15 帧原版电击死亡，使用 anim.pal；按 art 的 LoopEnd=3 / LoopCount=5 重复前段后播完；电弧复用程序化电弧绘制
  - `radiation/nukedie.shp`：60 帧原版辐射融化死亡，使用 anim.pal
  - `demolition/mininuke.shp` / `demtexp.shp`：39 / 25 帧原版自爆动画，使用 anim.pal；恐怖分子、特殊乘员 IFV 使用前者，自爆卡车使用后者
  - `ivan_apocalypse/crivexp.shp`、`apmuzzle.shp`、`apocexp.shp`：23 / 13 / 16 帧原版伊文爆炸、天启炮口及命中动画，使用 anim.pal
  - `v3/v3takoff.shp`、`v3/v3trail.shp`：各 20 帧原版火箭发射与尾焰；命中爆炸复用 special_vehicles 内现有文件
  - `flak/smkpuff.shp` / `htrkpuff.shp`：原版对空 / 对地防空爆炸，20 / 15 帧，使用 anim 调色板和 art 的 50% 半透明
  - `special_vehicles`：坦克杀手 S_CLSN30、光棱 CometWH 的八种命中效果、幻影 VTMUZZLE / IRONFX；原样复制自 `assets_origin/sprites`
  - `defense`：磁暴命中 `tstimpct.shp`、巨炮炮口 `gcmuzzle.shp`，原样复制自 `assets_origin/sprites`；电弧本身为程序绘制，颜色取原版 `palette.pal` 的 10 / 15 号色
  - `garrison/ucflash.shp`：原版驻军窗户枪口闪光，7 帧
  - `naval/{wake1,h2o_exp1,h2o_exp2,h2o_exp3,bbblelrg}.shp`：原版水面尾流、三档水花和鱼雷气泡，使用 anim.pal 与 art 的图层 / 半透明 / 偏移
  - `projectiles_voxel/subt.vxl` / `subt.hva`：原版潜艇鱼雷，独立 VXL 绘制，不用普通炮弹 SHP 代替
  - `projectiles/medusa.shp`：神盾导弹，前 32 帧为方向主体，后 32 帧为空阴影，不参与方向索引
  - `projectiles/dragon.shp`：IFV 导弹，32 方向，按实际飞行方向取帧

- `terrain`
  - 地形、地块和地图资源
  - `naval/{temperate,snow,urban}/{water01,shore09}.{tem,sno,urb}` 与 `iso.pal`：原样复制的原版 2×2 水面 / 海岸 TMP 及对应 isotem / isosno / isourb 调色板，按原始 LandType 驱动示范地图
  - `disguises/{temperate,snow,urban}/tree01..04.{tem,sno,urb}`：幻影坦克原版四种树形；文件内部为 SHP，第一帧主体使用剧场地形调色板，第二帧索引掩码绘制黑色半透明树影
  - `resources/temperate`、`resources/snow`、`resources/urban`：原版 `tib01..12` 矿石与 `gem01..12` 宝石覆盖层
  - `.tem/.sno/.urb` 保留原名和二进制内容；这些矿物文件的内部格式是 SHP，不是 TMP 地块
  - 每个文件有 12 帧矿量和 12 帧空白阴影；矿物使用 `palettes/resources` 下对应主题的覆盖层调色盘，不应用阵营重映射或建筑亮度缩放
  - 采集动画 `oregath.shp` 使用 `palettes/effects/anim.pal`；普通 TMP 地块使用 `iso*.pal`，不是矿物调色盘

## 当前约定

- 建筑 `shp` 会在 `buildings` 目录树中递归按文件名查找，因此建筑可以安全放在阵营/建筑 ID 子目录下。
- cameo 图标会在 `icons/cameo` 下递归按文件名查找，因此同一分类目录里可以同时放多个阵营的图标。
- 侧栏壳资源按主题目录组织，切换阵营时直接切到对应主题目录。
- 侧栏 palette 单独放在 `palettes/ui/sidebar`，名称与主题目录对应：
  - `sidec01.pal`
  - `sidec02.pal`

## 维护目标

我们希望新的 `assets` 始终满足这几个目标：

- 清晰：一眼能看懂资源属于什么系统。
- 收敛：只保留当前项目确实用到的资源。
- 可扩展：后续继续接入尤里、步兵、特效时，有明确落点。
- 可维护：避免重复拷贝和无层级的大平铺目录。

突击队员批次的 `effects/chrono/{warpin,warpout,warpaway}.shp` 原样复制自 `assets_origin/sprites`。四种单位继续使用已有 `icons/cameo/infantry/{sealicon,tanyicon,spyicon,clegicon}.shp`；老兵标识复用 `ui/unit_overlays/common/pips.shp` 第 13 帧。新增 8 个 SHP 与复用的 4 个 cameo 已逐字节核对，清单位于忽略目录 `build/commandos/asset-sha256.json`。

首批海军新复制 20 个原版二进制资源（含海蝎 hovricon.shp），复用 12 个船体 / 图标；逐字节来源和 SHA-256 见忽略目录 `build/naval/asset-sha256.json`。船厂沿用已有原版三剧场建筑资源，未修改 assets_origin 或原始 rules.ini / art.ini。

第二批海军核对 34 个原版文件，来源 / SHA-256 清单为 `build/naval-special/asset-sha256.json`：

- `vehicles/destroyer/{dest,destwo}`、`aircraft_carrier/carrier`、`dreadnought/{dred,dredwo}`：原版船体和空甲板 VXL / HVA。
- `vehicles/naval_aircraft/{asw,hornet,dmisl}`：原版舰载反潜机、航母飞机与独立无畏导弹 VXL / HVA；DMISL 不重映射队色。
- `vehicles/dolphin/dlph.shp`：116 主体帧及阴影；`vehicles/giant_squid/sqd.shp`：296 主体帧，NoShadow=yes，禁止截掉后半段。
- `effects/naval_special/sqdg.shp`：原版合并缠绕资源，布局为三块 × 八方向 × 10 帧；按 art 的 AltPalette=yes 使用单位调色板。八个 sqdg_* 原版方向文件保留作方向与旧动画参考，不能将 SQDG 合并文件误当作八段连续的 30 帧；`sqdwake.shp`、`durasmoke.shp` 分别用于缠绕水花和导弹烟迹。
- `icons/cameo/vehicles/{desticon,carricon,dredicon,dlphicon,sqdicon}.shp`：原版侧栏图标。

海豚 SonicZap 原版为程序生成的 WaveClass，当前使用青色行进弧线绘制，不生成替代 SHP，也不使用坦克激光素材。

独立空军接入使用原版 `vehicles/intruder/falc`、`black_eagle/beag`、`blackhawk/shad`、`kirov_airship/zep` 的 VXL / HVA 及四种原版 Cameo；保留 SHAD 的两帧 HVA 旋翼和 ShadowIndex=2。新补入 `effects/projectiles_voxel/zbomb.vxl/.hva`，用于基洛夫垂直炸弹；`ui/unit_overlays/common/pips2.shp` 的第 13 帧使用原版 palette.pal 绘制弹药，不生成替代资源。共核对 15 个文件的原始来源 / SHA-256，清单位于忽略目录 `build/aircraft/asset-sha256.json`；rules.ini / art.ini 与 assets_origin 原本仅有换行差异。
