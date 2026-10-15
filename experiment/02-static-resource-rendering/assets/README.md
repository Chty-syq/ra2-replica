# 本篇素材

这里保存教学示例使用的独立副本。程序只读取可执行文件旁的 `assets` 目录，不回退到主项目或 `assets_origin`。

| 文件 | 仓库内复制来源 | 用途 |
|---|---|---|
| `ggpowr.shp` | `assets/buildings/allied/gapowr/ggpowr.shp` | 读取全部 6 帧，常态绘制第 0 帧 |
| `ggpowr_a.shp` | `assets/buildings/allied/gapowr/ggpowr_a.shp` | 读取全部 32 帧，常态循环第 0～7 帧 |
| `unittem.pal` | `assets/palettes/theater/unittem.pal` | 温带单位调色盘 |
| `rules.ini` | `assets/settings/rules.ini` 的 `[GAPOWR]` | 单位 ID 和可选 `Image`；玩法参数仅保留供参考 |
| `art.ini` | `assets/settings/art.ini` 的 `[GAPOWR]`、`[GAPOWR_A]`、`[GAPOWR_AD]` | 主体、正常和损坏工作动画；原字段值不变 |

两个 SHP 文件的完整画布都是 142×110。主体有 6 帧，上层文件有 32 帧；启动时将所有帧解码并上传为纹理。`art.ini [GAPOWR_A]` 配置 `LoopStart=0`、`LoopEnd=8`，常态播放 `[0,8)`；损坏态 `[GAPOWR_AD]` 从第 8 帧开始。后半部分为对应的阴影帧，另有绘制用途。

颜色处理顺序为 PAL 查表、国家色、RGB565。国家色使用 `assets/settings/rules.ini` 中默认 DarkBlue 经 `src/rules_colors.cpp` 转换后的 RGB `(32,105,213)`。当前 `main.cpp` 读取两份 SHP 的全部帧，绘制主体第 0 帧。各层完整画布中心对齐到占地起始格上顶点，即格中心向上半格高度。运行时不依赖主项目文件。

两个 INI 保留原版电厂的参考字段。`ActiveAnim` 引用工作动画节，`NewTheater` 对应温带 `ggpowr.shp` 和 `ggpowr_a.shp` 的命名。损坏节保留供后续实现，`Buildup` 指向的施工素材未加入本篇。`Height` 表示建筑高度；`ZAdjust` 与 `YSort` 是深度/排序参数，不加到图片位置。

RGB565 在 shader 查表、替换国家色之后执行，保留 R/B 的高 5 位与 G 的高 6 位，再展开为现代颜色缓冲中的 RGB。

SHP 和 PAL 为原版游戏素材，原有权利归属不变；它们与教学代码的授权应分别处理。

动画节奏固定采用主项目默认对战 6 档：`Rate=220`、`Normalized=yes` 对应每 7 个游戏帧换图，60 游戏帧/秒。来源见 `assets/settings/art.ini` 和 `src/compat/rules_timing.h`；教学程序仍不链接或读取这些主项目文件。
