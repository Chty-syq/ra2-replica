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
  - 每个建筑再按建筑 ID 建目录
  - 例如：
    - `buildings/allied/gapowr`
    - `buildings/soviet/napowr`

- `vehicles`
  - 当前项目使用的载具资源
  - 多文件载具按对象目录组织
  - 例如：
    - `vehicles/rhino_tank`
      - `htnk.vxl`
      - `htnk.hva`
      - `htnktur.vxl`
      - `htnktur.hva`
      - `htnkbarl.vxl`
      - `htnkbarl.hva`

- `icons`
  - 侧栏和建造系统使用的 cameo 图标
  - 当前按用途分类：
    - `base`
    - `defense`
    - `infantry`
    - `vehicles`
  - 同一个分类下可以同时放盟军和苏军图标，运行时按文件名递归查找

- `ui`
  - 侧栏壳体、按钮、覆盖层等 UI 资源
  - 目前侧栏按主题分层：
    - `ui/sidebar/sidec01`：盟军
    - `ui/sidebar/sidec02`：苏军

- `infantry`
  - 预留给步兵资源

- `effects`
  - 战斗及采集特效资源
  - `harvesting/oregath.shp`：原版矿车采集动画，8 个方向 × 15 帧

- `terrain`
  - 地形、地块和地图资源
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
