# 红警2建筑定位规则与实现依据

核查日期：2026-10-10。

建筑定位可以使用统一的地图坐标和 SHP 画布中心规则，无需为每种占地尺寸保存一组屏幕偏移。OpenTS、CNC Maps Renderer 和 WorldAlteringEditor 的对应实现共同支持：普通建筑以占地起始格的上顶点作为绘图中心，SHP 以完整画布中心对准这个点，局部帧偏移只负责在画布中恢复像素。

本项目现有偏移表可以分解成“旧占地锚点转换到起始格”与额外的 1 像素差异。前者能用几何关系替代，后者尚无足够依据保留为原版规则。以下公式以本项目 60×30 地块、逻辑整数坐标表示格子中心、平地且无缩放为前提。它们是可实施的坐标基准；原版 RA2 的逐像素取整仍需要版本明确的对照。

## 源码支持的绘图中心

OpenTS 的 `BuildingClass::Render_Coord` 返回建筑位置减去两个地图轴上的半格。`Coord(Cell)` 将逻辑格转换成格中心，因此相减后得到起始格的上顶点。`Draw_It` 把这个位置交给 `Techno_Draw_Object`，后者通过 `SHAPE_CENTER` 居中绘制。占地尺寸进入深度图位置计算，并未按尺寸移动建筑彩色图片。[Render_Coord](https://github.com/OpenTS-Developers/OpenTS/blob/103ff23b32a7214c304d4141a6e83eb255a74d01/code/building.cpp#L10486)、[格中心换算](https://github.com/OpenTS-Developers/OpenTS/blob/103ff23b32a7214c304d4141a6e83eb255a74d01/code/coord.h#L77)、[居中绘制](https://github.com/OpenTS-Developers/OpenTS/blob/103ff23b32a7214c304d4141a6e83eb255a74d01/code/techno.cpp#L6109)。OpenTS 对应 TS，不能单独证明 RA2 所有细节相同。

两套支持 RA2 的地图绘制项目给出相同的几何关系：

| 项目 | 实现中的坐标处理 | 对本项目的意义 |
|---|---|---|
| CNC Maps Renderer | 从格子外接矩形左上角加 `(TileWidth/2, 0)`，再减 SHP 完整宽高的一半，加局部帧 X/Y | 对齐格子上顶点，基本位置不需要尺寸分类表 |
| WorldAlteringEditor | 通用图片先对齐格中心；建筑的 `GetYDrawOffset()` 再返回 `-CellSizeY/2` | 同样从格中心回到上顶点 |
| OpenRA RA2 mod | Actor 位置取占地中心，序列另配 `Offset`；电厂为 `(0,-31,31)` | 采用不同的坐标基准，不能直接抄偏移值 |

对应源码：[CNC Maps 对象默认偏移](https://github.com/zzattack/ccmaps-net/blob/56712d7c9c5048afbfe9bddb01c0ec43f6fc008a/CNCMaps.Engine/Game/ObjectCollection.cs#L143)、[SHP 绘制位置](https://github.com/zzattack/ccmaps-net/blob/56712d7c9c5048afbfe9bddb01c0ec43f6fc008a/CNCMaps.Engine/Rendering/ShpRenderer.cs#L115)、[WorldAlteringEditor 图片位置](https://github.com/Rampastring/WorldAlteringEditor/blob/36209485b8bc389bb6aaa08dd09e54eca3d8765b/src/TSMapEditor/Rendering/ObjectRenderers/ObjectRenderer.cs#L258)、[建筑半格偏移](https://github.com/Rampastring/WorldAlteringEditor/blob/36209485b8bc389bb6aaa08dd09e54eca3d8765b/src/TSMapEditor/Models/Structure.cs#L286)、[OpenRA 占地中心](https://github.com/OpenRA/OpenRA/blob/e7e9e4ad270221d7fb9725b348c75504d48c1ebd/OpenRA.Mods.Common/Traits/Buildings/Building.cs#L153)、[RA2 电厂序列](https://github.com/OpenRA/ra2/blob/61e24e3c1d7b586aa55a86096d29e1559aa9b994/mods/ra2/sequences/allied-structures.yaml#L58)。OpenRA 的 YAML 偏移是该模组的配置，并非原版 art.ini 提供的字段。

## 适用于本项目的公式

令起始格坐标为 `(i,j)`，地块宽高为 `(Tw,Th)`，地图原点为 `O`。格中心的屏幕坐标为：

\[
C=O+\left((i-j)\frac{T_w}{2},\ (i+j)\frac{T_h}{2}\right)
\]

起始格的上顶点，也就是建筑绘图中心，为：

\[
D=C-\left(0,\frac{T_h}{2}\right)
\]

完整 SHP 画布宽高为 `(W,H)`，绘制左上角为：

\[
P=D-\left(\left\lfloor\frac W2\right\rfloor,\left\lfloor\frac H2\right\rfloor\right)
\]

这里显式使用整数除法的中心约定，与所对照的 CPU 渲染实现一致。若上传的是裁剪后的局部帧，额外加 SHP 帧表中的 `(frameX,frameY)`；若像本项目一样已恢复完整画布，不再加第二次。地图高程另作用于 `C`，不能用 art.ini 中的建筑 `Height` 代替。

对第二篇的电厂，`C=(640,360)`、`Th=30`、`W=142`、`H=110`：

\[
D=(640,345),\qquad P=(569,290)
\]

等价地，以格中心为目标时，图片内锚点为 `(71,70)`。当前 `(71,69)` 会得到 `(569,291)`，比上述基准向下 1 像素。不能仅因为旧图看起来正常就保留这 1 像素，也不能把对齐开源渲染器说成已通过原版像素对照。

## 现有偏移表的几何分解

本项目 [foundation.cpp](../src/foundation.cpp) 为不同尺寸定义了不同的锚点：2×2 的格子从 `(0,0)` 开始，3×3 从 `(-1,-1)` 开始，5×3 从 `(-2,-1)` 开始。因此 [iso_world.cpp](../src/iso_world.cpp) 中的 `placement.topLeft` 并不总是占地矩形的起始格。

设旧占地相对锚点的最小格坐标为 `m=(mx,my)`。对于当前表中的这些矩形，把旧锚点转换到起始格，需要的图片内偏移为：

\[
\Delta_x=-30(m_x-m_y),\qquad
\Delta_y=15-15(m_x+m_y)
\]

从当前两个函数中提取分支并计算，得到：

| Foundation 宽×高 | 旧占地最小格 m | 当前图片中心偏移 | 几何推导偏移 |
|---|---|---|---|
| 1×1 | `(0,0)` | `(0,14)` | `(0,15)` |
| 2×2 | `(0,0)` | `(0,14)` | `(0,15)` |
| 3×2 | `(-1,0)` | `(30,29)` | `(30,30)` |
| 3×3 | `(-1,-1)` | `(0,44)` | `(0,45)` |
| 4×3 | `(-1,-1)` | `(0,44)` | `(0,45)` |
| 4×4 | `(-1,-1)` | `(0,44)` | `(0,45)` |
| 5×3 | `(-2,-1)` | `(30,59)` | `(30,60)` |

这七种占地的格集合都是矩形。表中的大数值来自锚点定义变化，并非建筑高度、可见像素包围盒或不同尺寸各自具有某种特殊像素规则。这是由当前代码推导的关系，不是对旧代码作者意图的追溯。

所有旧分支都比推导结果少 1 个 Y 单位。所查源码没有支持继续使用这个统一差值的依据；精确处理应核对原版屏幕投影与整数取整，而不是再引入一个“看起来对”的补偿。

未列入旧表的尺寸会退回零偏移。本地 art.ini 有 20 种 Foundation 值，包括 `2x3`、`1x2`、`2x1`、`3x4`。即使使用正常的起始格，这些尺寸仍然需要从格中心到上顶点的转换，说明旧表在覆盖范围上也不完整。

## 原版配置字段的职责

本地 [art.ini 的字段注释](../assets/settings/art.ini) 区分了图片位置、遮挡排序和可见占用。应按这些职责解析，避免把带有 Offset 或 Z 的字段统统加到屏幕位置上。

| 字段或数据 | 实际用途 | 对绘图位置的处理 |
|---|---|---|
| SHP 完整宽高及局部帧 X/Y | 画布中心与裁剪帧定位 | 用完整画布中心定位；局部偏移应用一次 |
| `Foundation` | 逻辑占地及相关几何 | 确定格集合和坐标基准；不直接生成经验像素偏移 |
| `Height` | 建筑高度信息，例如选框、血条等 | 不当成整张建筑图片的向上位移 |
| `OccupyHeight`、`AddOccupy*`、`RemoveOccupy*` | 建筑遮挡其他对象的可见占用 | 不用于搬动图片或替代放置占地 |
| `ActiveAnimX/Y` 及其他动画槽位的 X/Y | 子动画相对建筑绘图位置的偏移 | 进入对应图层的位置计算 |
| 动画 `YDrawOffset` | 动画图像的屏幕 Y 偏移 | 按动画类型处理，不能扩成所有建筑本体的通用键 |
| `NormalZAdjust`、`ActiveAnimZAdjust`、`ActiveAnimYSort` | 深度或绘制排序 | 保持独立于屏幕 X/Y 的处理 |
| `ZShapePointMove` | 共享建筑深度图的采样基准 | 移动深度图参照点，不移动建筑彩色图片 |
| `BibShape` | 附加地台图片 | 使用共同绘图中心和该 SHP 自身的画布尺寸 |
| `TurretAnimX/Y` | 炮塔动画位置，位于 rules.ini | 只作用于对应炮塔图层 |

`GAPOWR` 的配置包含 `Foundation=2x2`、`Height=4`、`OccupyHeight=3`、`ActiveAnimZAdjust=-32`、`ActiveAnimYSort=362`；没有用 `ActiveAnimX/Y` 配置图片位移。`-32` 和 `362` 都不能解释成向上或向下移动对应像素。[本地电厂配置](../assets/settings/art.ini#L1905)

本地 `GAHPAD` 则明确配置 `ActiveAnimY=-12`，这是可以使用的附层位移。该值位于现有配置中，不代表该 TS 遗留对象已在本项目中启用。[本地配置](../assets/settings/art.ini#L1260)

OpenTS 在建立建筑动画时把槽位位置换算成世界坐标，并分别设置 `ZAdjust` 和 `YSortAdjust`；YRpp 的 `BuildingAnimStruct` 也分别保存 `Position`、`ZAdjust`、`YSort`。这些独立字段支持分开建模。[OpenTS 动画创建](https://github.com/OpenTS-Developers/OpenTS/blob/103ff23b32a7214c304d4141a6e83eb255a74d01/code/building.cpp#L7942)、[YRpp 字段](https://github.com/Phobos-developers/YRpp/blob/b22f1860dd7b100cad57123d4ca5d459b9020d88/BuildingTypeClass.h#L7)

`ZShapePointMove` 的用途还可从 OpenTS 实际绘制代码确认：它加入 `zdrawpoint` 后传给深度图，而彩色图片继续使用 `drawpoint`。本地战车工厂的 `ZShapePointMove=30,15` 不能作为图片偏移直接照搬。[OpenTS Draw_It](https://github.com/OpenTS-Developers/OpenTS/blob/103ff23b32a7214c304d4141a6e83eb255a74d01/code/building.cpp#L874)、[该项目字段说明](https://github.com/OpenTS-Developers/OpenTS/blob/103ff23b32a7214c304d4141a6e83eb255a74d01/manual/content/keys/zshapepointmove.md)

## 替换现有实现的范围

采用统一的起始格坐标，再将每层 SHP 的画布中心对齐到共同的绘图中心。普通矩形占地从 `(0,0)` 到 `(width-1,height-1)` 枚举；原版特殊占地或 Ares 自定义占地按各自的明确定义处理。不能把 OpenTS 的特殊 Foundation 枚举不加区分地移植成 RA2 规则。[WorldAlteringEditor 的矩形与自定义占地解析](https://github.com/Rampastring/WorldAlteringEditor/blob/36209485b8bc389bb6aaa08dd09e54eca3d8765b/src/TSMapEditor/Models/ArtConfig/BuildingArtConfig.cs#L29)

迁移需要同时处理以下关系：

1. 将 `placement.topLeft` 统一定义为占地起始格。若保留现有场景的占地位置，旧实例需加上原格集合的最小相对坐标，再转换为新定义。
2. 统一放置预览、选框与血条、出厂位置、寻路占用以及深度坐标所引用的建筑位置；只更换绘制锚点会留下这些系统的错位。
3. 让主体、地台、附层各自减去自己的画布半宽高，再叠加有效的动画槽位位移。当前 `drawBuildingInstance` 给附层复用主体左上角，仅在画布相同且附层偏移为零时成立。
4. 补齐图层 X/Y 与深度字段解析；当前 `ArtDefinition` 没有保存对应槽位的坐标信息。缺省 Foundation 也应按本地 art.ini 注释的 `1x1` 核对，现有资源加载路径使用了 `2x2` 回退。
5. 第二篇只展示电厂，可直接说明“格中心减半格高度，再减 SHP 画布中心”，无需引入完整的建筑配置系统。

位置验证应覆盖 1×1、2×2、3×2、2×3、3×3、4×3、5×3，附层画布尺寸不同、建造帧和完成帧交接、地图高程及奇数画布尺寸。以 OpenTS 和 RA2 地图编辑器作为坐标基准交叉检查；原版逐像素对齐须另用明确版本、相同地块与帧做对照。对比当前主项目得到的像素一致，只能证明实现相同，不能用于证明这些旧规则正确。

## 本轮落地结果

上述旧偏移表已从代码中删除。普通矩形占地采用统一的西北起始格，`MapGrid`、模拟层和绘制共用 `foundationTiles`，修复了未知尺寸落入默认分支时轴向不一致的问题。缺省 Foundation 使用 1×1；不能识别的特殊格式由渲染配置读取明确报错。

主体、地台、附层、建造帧以及工厂前景重绘都使用起始格上顶点作为共同绘图中心，每层按自身完整画布尺寸居中。逻辑深度纹理同步改为相对此中心的像素偏移，深度占地坐标也使用同一基准。现有简化阴影则使用占地中心，不跟随 SHP 上顶点移动。

`Anim.<槽位>` 图层读取对应的 X/Y，叠加动画 `YDrawOffset`。槽位 `ZAdjust`、`YSort` 与动画 `NormalZAdjust` 独立解析并保存，尚未接入原版深度图算法。巨炮炮塔的位置从 rules.ini 的 `TurretAnimX/Y` 读取。

联动迁移：

- GAWEAP/NAWEAP 出厂路径变为 `2:1,3:1,4:1,5:1`；GAREFN/NAREFN 外部泊位变为 `4:1`。内置场景及回归场景起始格同步迁移，原有世界路线不变。这些路线与时序依然是待原版校准的项目配置。
- 基地车展开时，将车辆所在格换算为目标建筑起始格；收起时做相反转换。偶数占地当前取两个中心格中坐标较小者，4×4 基地从车辆格减去 `(1,1)`，保持原有演示占地。这是项目的转换策略，不能单凭这次坐标研究认定已还原原版的展开选格规则。
- 第二篇保持独立编译，使用“格中心减半格高度，再减每层画布中心”的最小代码；同步更新底稿、素材说明及实拍截图。

验证覆盖不同矩形尺寸与轴向、奇数画布、不同大小附层和负偏移、缺省占地、动画位置/深度字段、两种基地车展开与收起、工厂通道、采矿泊位及确定性回放。根工程 14 项 CTest 检查通过；图形场景检查了展开、完成、收起和工厂出车。第二篇在 1280×720 与 900×600 下对比主项目 `Renderer2D` 的静态颜色/位置绘制，RGB 差异均为 0。

本轮范围是平地上的普通矩形建筑定位。特殊占地、地图高程、原版 Z-shape 遮挡、原版 SHP 阴影及原版逐帧截图对齐仍未完成；不能以内部像素一致性代替原版还原度验收。
