# RA2 体素载具渲染设计稿

本文档描述当前项目中 `VXL/HVA` 载具单位的目标渲染方案，目标是尽量靠近《红色警戒 2》原版的体素单位表现方式，同时兼容当前工程已经存在的 `SDL2 + OpenGL + 等距世界` 结构。

当前相关代码与文档：

- [vxl_file.h](/F:/projects/ra2-replica/src/vxl_file.h)
- [vxl_file.cpp](/F:/projects/ra2-replica/src/vxl_file.cpp)
- [voxel_unit.h](/F:/projects/ra2-replica/src/voxel_unit.h)
- [voxel_unit.cpp](/F:/projects/ra2-replica/src/voxel_unit.cpp)
- [iso_world.h](/F:/projects/ra2-replica/src/iso_world.h)
- [iso_world.cpp](/F:/projects/ra2-replica/src/iso_world.cpp)
- [renderer2d.h](/F:/projects/ra2-replica/src/renderer2d.h)
- [renderer2d.cpp](/F:/projects/ra2-replica/src/renderer2d.cpp)
- [voxel-rhino-demo.md](/F:/projects/ra2-replica/docs/voxel-rhino-demo.md)

## 1. 目标

本设计稿的目标不是“随便把 VXL 显示出来”，而是做出一条能够逐步逼近原版的技术路线。

目标分为三层：

1. 正确解析 `VXL + HVA + VPL + unit palette`
2. 正确渲染车体、炮塔、炮管等多部件载具
3. 正确接入当前等距世界、深度遮挡、阵营色和剧院风格

最终希望达到的效果：

- 载具使用原版体素资源，而不是预烘焙 2D 朝向图
- 炮塔与炮管可以独立旋转
- 颜色来自原版调色盘与 `VPL` 光照查表
- 单位能够自然地站在当前地图格子里，并参与遮挡排序

## 2. 现状

当前项目已经具备的能力：

- 能解析 `VXL` 基本结构
- 能解析 `HVA` 矩阵
- 能把犀牛坦克先在 CPU 侧预渲染为一张固定 `128x128` 贴图
- 能把这张贴图挂进当前等距世界中显示

当前项目尚未具备的能力：

- 没有 `VPL` 光照查表
- 没有真正的 `32` 向或 `360` 向渲染
- 没有独立炮塔/炮管实时朝向
- 没有单位级动画状态机
- 没有和 `rules.ini / art.ini` 的单位定义完整打通
- 没有原版风格的体素国家色与剧院 palette 切换

也就是说，当前版本只是“体素资源可见”，还不是“原版式体素单位渲染系统”。

## 3. 原版思路摘要

按现有公开资料，RA2 体素载具大体遵循这条链路：

1. `VXL` 存体素颜色索引与法线索引
2. `HVA` 存每个部件、每一帧的变换矩阵
3. `VPL` 根据法线和亮度段，把原始 palette index 映射到受光后的 palette index
4. 最终再从 `unittem / unitsno / uniturb.pal` 取颜色
5. 结果画到等距世界中

这意味着原版重点不是“直接把体素转成 RGB”，而是：

- 体素颜色首先是调色盘索引
- 光照是查表，不是现代逐像素光照模型
- 车体、炮塔、炮管是独立部件，靠 `HVA` 拼装

## 4. 设计原则

这套系统建议遵循下面几条原则。

### 4.1 保留索引语义，避免过早拍平成 RGBA

建筑 `SHP` 当前已经走了 `Palette -> RGBA` 的路径，但体素如果想更像原版，最好在更晚阶段再转成 RGBA。

原因：

- `VPL` 光照本质上就是“索引映射”
- 阵营色 remap 也更适合发生在索引层
- 剧院切换也是先换调色盘，再换最终颜色

所以对体素系统，建议尽量走：

`VXL 索引 -> VPL 映射 -> Palette 取色 -> RGBA 输出`

而不是：

`VXL -> 直接 RGB -> 再做现代光照`

### 4.2 部件独立渲染，再做合成

不要把 `body / turret / barrel` 在资源加载阶段烘成一张大图后固定死。

应该：

- 每个部件保留自己的几何与矩阵
- 每帧按当前朝向与矩阵重新投影
- 再在单位级做 2D 合成

这样后面才容易支持：

- 炮塔追踪目标
- 炮管抬升
- 旋翼/雷达/特效部件

### 4.3 先做 CPU 原型，再考虑 GPU 化

对当前项目来说，最稳妥的路线仍然是先做 CPU 侧体素绘制器。

原因：

- 原版逻辑本来就更接近软件渲染/查表渲染
- CPU 版更容易验证 `VPL / palette / HVA` 是否正确
- 现有世界渲染已经是 2D 贴图通道，CPU 预渲染输出一张 sprite 就能无缝接入

在视觉正确之前，不建议过早转成复杂 GPU 体素光栅化方案。

## 5. 目标渲染管线

建议的目标渲染链路如下：

```text
VXL 文件
  -> section 体素数据
  -> color index / normal index

HVA 文件
  -> 每部件矩阵

单位状态
  -> 车体朝向
  -> 炮塔朝向
  -> 炮管朝向
  -> 当前剧院
  -> 当前阵营色

渲染阶段
  -> 逐部件应用 HVA 变换
  -> 投影到单位局部画布
  -> 根据 normal + light level 选择 VPL section
  -> 根据 VPL 输出新的 palette index
  -> 根据 unit palette 取最终颜色
  -> 合成 body/turret/barrel
  -> 输出单位 sprite

世界阶段
  -> 将单位 sprite 贴到等距世界
  -> 参与深度遮挡
  -> 绘制阴影/选中框/血条
```

## 6. 推荐模块划分

建议把体素系统拆成下面几层。

### 6.1 `vxl_asset.*`

职责：

- 解析 `VXL`
- 提供 section、体素尺寸、颜色索引、法线索引访问

建议结构：

- `VoxelSample`
- `VxlSection`
- `VxlFile`

当前 [vxl_file.h](/F:/projects/ra2-replica/src/vxl_file.h) 和 [vxl_file.cpp](/F:/projects/ra2-replica/src/vxl_file.cpp) 已经有一部分基础，可以继续扩展，不必推倒重来。

### 6.2 `hva_asset.*`

职责：

- 解析 `HVA`
- 提供按 `frame + section` 查询矩阵

当前 `HvaFile` 已经具备最小可用实现，后续主要是和渲染器的矩阵使用方式对齐。

### 6.3 `vpl_palette.*`

职责：

- 解析 `VPL`
- 提供 `lightLevel + paletteIndex -> remappedPaletteIndex`

建议新增的数据结构：

```cpp
struct VplFile {
  std::array<std::array<std::uint8_t, 256>, 32> tables;
};
```

说明：

- `32` 个亮度段是核心
- 每段都是 `256` 色映射
- 输出仍然是 palette index，而不是 RGB

### 6.4 `voxel_renderer_cpu.*`

职责：

- 输入部件体素、矩阵、朝向、palette、vpl
- 输出单位级 2D 贴图

建议阶段：

1. 逐部件把体素点变换到模型空间
2. 逐体素投影到 2D 单位画布
3. 使用单位级 z-buffer 解决遮挡
4. 在落点处根据法线与光照段选择 `VPL`
5. 再通过 palette 取色，写入 `RGBA`

### 6.5 `voxel_unit_system.*`

职责：

- 定义单位实例
- 维护朝向、炮塔朝向、状态
- 管理当前帧是否需要重渲染 sprite

建议结构：

```cpp
struct VoxelUnitAsset {
  std::string id;
  std::vector<VoxelPartAsset> parts;
};

struct VoxelUnitInstance {
  const VoxelUnitAsset* asset = nullptr;
  TileCoord cell{};
  int bodyFacing = 0;
  int turretFacing = 0;
  int barrelFacing = 0;
  std::uint32_t lastRenderTick = 0;
};
```

## 7. 数据模型设计

建议新增这几类数据。

### 7.1 部件资源

```cpp
struct VoxelPartAsset {
  std::string id;
  VxlFile vxl;
  HvaFile hva;
  int hvaSectionIndex = 0;
};
```

### 7.2 单位资源

```cpp
struct VoxelUnitAsset {
  std::string id;
  std::vector<VoxelPartAsset> parts;
  int spriteWidth = 128;
  int spriteHeight = 128;
  int anchorX = 64;
  int anchorY = 64;
};
```

这里保留 `128x128 + 64,64`，是为了兼容当前参考项目验证结果，也便于逐步替换当前犀牛坦克 demo。

### 7.3 渲染输入

```cpp
struct VoxelRenderContext {
  const Palette* unitPalette = nullptr;
  const VplFile* vpl = nullptr;
  TheaterStyle theater = TheaterStyle::Temperate;
  Rgba houseColor{};
  Vec3 lightDirection{};
  int lightLevelCount = 32;
};
```

### 7.4 渲染输出

```cpp
struct VoxelSpriteFrame {
  std::vector<std::uint8_t> rgba;
  int width = 0;
  int height = 0;
  int anchorX = 0;
  int anchorY = 0;
};
```

## 8. 渲染算法建议

### 8.1 体素可见性

第一版不必急着做复杂的体素面剔除，可以先：

- 逐体素投影
- 逐像素比较 z-buffer

这样虽然慢一些，但逻辑稳定，容易验证。

### 8.2 法线与光照段

建议先把法线索引转换成单位向量，再与固定光照方向做点积：

```text
brightness = clamp(dot(normal, lightDir), 0, 1)
lightLevel = brightness * 31
```

然后：

```text
paletteIndex' = vpl[lightLevel][paletteIndex]
```

这比直接在 RGB 上做光照更接近原版。

### 8.3 阵营色

如果 palette index 落在 remap 区间：

- 先应用阵营色 remap palette
- 再做 `VPL`

或：

- 先根据 `VPL` 得到最终索引
- 再查 remapped palette

这两种顺序都需要在实现时实测。第一版推荐先统一走“remapped palette + vpl index”。

### 8.4 阴影

第一版阴影不必做体素真实投影，可延续当前建筑/载具 demo 的风格化阴影：

- 单位 sprite 投影到地面
- 压黑并拉伸
- 本体略微压在阴影前方

待主体渲染稳定后，再考虑原版味道更浓的阴影做法。

## 9. 与当前工程的接入方式

建议接入分三步走。

### 9.1 替换当前犀牛坦克原型

当前 [voxel_unit.cpp](/F:/projects/ra2-replica/src/voxel_unit.cpp) 还是“体素转一张静态 2D 贴图”的最小原型。

第一步先把它升级成：

- body/turret/barrel 分开
- 支持指定朝向
- 使用固定 `128x128` 画布
- 输出单位 sprite

这样不改世界层也能先看到正确方向的进展。

### 9.2 接入世界渲染

沿用当前 [iso_world.cpp](/F:/projects/ra2-replica/src/iso_world.cpp) 的 2D 世界 pass，把单位 sprite 作为世界对象画进去：

- 锚点仍然对齐格子中心
- 继续参与当前深度缓冲
- 后续再补单位选中框和移动阴影

### 9.3 再接入单位逻辑

当体素 sprite 输出稳定后，再接：

- `rules.ini` 的单位定义
- 鼠标选中
- 移动
- 炮塔转向
- 开火

## 10. 实施顺序建议

建议按下面顺序推进。

### 阶段 1：资源链路打通

- 新增 `VPL` 解析器
- 把当前体素颜色来源从 `VXL` 内置 palette 升级为 `unit*.pal + VPL`
- 验证犀牛坦克静态朝向

### 阶段 2：朝向系统

- 车体支持多朝向渲染
- 炮塔与炮管独立旋转
- 单位 sprite 缓存按朝向复用

### 阶段 3：世界集成

- 接入移动
- 接入选中与阴影
- 接入简单武器与 muzzle flash

### 阶段 4：数据驱动

- 从 `rules.ini / art.ini` 自动决定单位资源与部件
- 不再为 `HTNK` 写死加载逻辑

## 11. 关键风险

### 11.1 `VPL` 实装细节

这是最可能让“看起来不像原版”的部分。

如果 `VPL` 的使用顺序、法线到亮度段的映射、palette 选择不对，画面会很快偏离原版风格。

### 11.2 性能

CPU 逐体素投影在第一版是合理的，但单位数量一多就会顶不住。

因此要尽早考虑缓存策略：

- 相同朝向的 body sprite 可缓存
- 炮塔 sprite 也可单独缓存
- 最终只在朝向变化时重渲染

### 11.3 资源命名与变体

原版不同单位常常有：

- body
- turret
- barrel
- 额外部件

不能假设所有载具都只有三个部件，因此后续最好走 `art.ini` 数据驱动。

## 12. 推荐的近期落地任务

如果按当前项目节奏，下一步最适合直接做的是：

1. 新增 `vpl_file.h/.cpp`
2. 让犀牛坦克改用 `VPL + unittem.pal`
3. 给犀牛坦克增加 `bodyFacing / turretFacing`
4. 保持仍然输出到 `128x128` 单位画布
5. 继续复用当前世界渲染

这样工作量可控，而且每一步都能在画面里看到明确结果。

## 13. 参考资料

- [ModEnc: Voxel](https://modenc.renegadeprojects.com/Voxel)
- [ModEnc: HVA](https://modenc.renegadeprojects.com/HVA)
- [ModEnc: Normals](https://modenc.renegadeprojects.com/Normals)
- [ModEnc: VPL](https://modenc.renegadeprojects.com/VPL)
- [ModEnc: File Types](https://modenc.renegadeprojects.com/File_Types)

以上资料主要用于确认原版体素资源的职责分工与渲染方向；真正的工程实现仍需要结合当前项目结构做适配。
