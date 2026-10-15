# SHP render experiment

这是一个固定渲染盟军电厂 `gapowr.shp` 第 0 帧的教学示例。

代码集中在一个文件中，按下面的顺序执行：

1. 读取 SHP 文件头和 24 字节帧表。
2. 根据 `flags=0/1/3` 解码第 0 帧。
3. 将局部帧放回完整的 `142x110` 索引画布。
4. 读取 `unitsno.pal`，展开 6 位通道并模拟 RGB565。
5. 在 CPU 上把每个颜色索引转换为 RGBA。
6. 将 RGBA 数据上传为一张 SDL 纹理。
7. 把第 0 帧显示在 SDL 窗口中央。

示例没有动画、切帧、OpenGL shader、ImGui 或地图系统。关闭窗口或按 `Esc` 即可退出。

为了只讲清楚 SHP 的基础渲染链路，示例也不处理国家色 remap 和地图亮度；这些属于游戏运行时的扩展步骤，不是解码 SHP 所必需的部分。

## CMake target

```text
shp_render_experiment
```

默认资源：

```text
assets/buildings/allied/gapowr/gapowr.shp
assets/palettes/theater/unitsno.pal
```
