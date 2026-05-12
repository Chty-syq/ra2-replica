# 当前代码结构

本文档描述 **当前仍在使用** 的主项目代码结构，方便后续继续扩功能时快速定位入口。

## 入口层

- [app_main_ui.cpp](/F:/projects/ra2-replica/src/app_main_ui.cpp)
  - 主循环
  - SDL 初始化
  - 事件分发
  - 世界渲染与 UI 渲染编排

- [demo_app_support.h](/F:/projects/ra2-replica/src/demo_app_support.h)
- [demo_app_support.cpp](/F:/projects/ra2-replica/src/demo_app_support.cpp)
  - 项目资源根路径定位
  - OpenGL 上下文配置
  - 剧院调色盘装载
  - 建筑资源按剧院/国家色重载

- [demo_scene.h](/F:/projects/ra2-replica/src/demo_scene.h)
- [demo_scene.cpp](/F:/projects/ra2-replica/src/demo_scene.cpp)
  - 建筑放置辅助逻辑
  - 预览更新
  - 渲染队列生成
  - 侧栏状态更新

## UI 与调试层

- [imgui_debug_panel.h](/F:/projects/ra2-replica/src/imgui_debug_panel.h)
- [imgui_debug_panel.cpp](/F:/projects/ra2-replica/src/imgui_debug_panel.cpp)
  - ImGui 调试面板
  - Rhino 相关调试参数分组
  - 剧院 / 国家色切换

- [sidebar.h](/F:/projects/ra2-replica/src/sidebar.h)
- [sidebar.cpp](/F:/projects/ra2-replica/src/sidebar.cpp)
  - 右侧建造栏资源加载
  - 按钮布局
  - tab、按钮、图标点击逻辑

## 建筑系统

- [building_system.h](/F:/projects/ra2-replica/src/building_system.h)
- [building_system_runtime.cpp](/F:/projects/ra2-replica/src/building_system_runtime.cpp)
  - 建筑资源装载
  - 建造动画与完成态层
  - 图标到建筑 ID 的映射

- [art_ini.h](/F:/projects/ra2-replica/src/art_ini.h)
- [art_ini.cpp](/F:/projects/ra2-replica/src/art_ini.cpp)
  - `art.ini` 解析

## 2D 世界渲染

- [renderer2d.h](/F:/projects/ra2-replica/src/renderer2d.h)
- [renderer2d.cpp](/F:/projects/ra2-replica/src/renderer2d.cpp)
  - 主项目 2D 贴图渲染器
  - 世界层与 UI 层 pass

- [iso_world.h](/F:/projects/ra2-replica/src/iso_world.h)
- [iso_world.cpp](/F:/projects/ra2-replica/src/iso_world.cpp)
  - 等距坐标换算
  - 地面网格
  - 建筑绘制命令

- [map_grid.h](/F:/projects/ra2-replica/src/map_grid.h)
- [map_grid.cpp](/F:/projects/ra2-replica/src/map_grid.cpp)
  - 建筑占格
  - 放置判定

## VXL 载具渲染

- [voxel_unit.h](/F:/projects/ra2-replica/src/voxel_unit.h)
- [voxel_unit.cpp](/F:/projects/ra2-replica/src/voxel_unit.cpp)
  - 把地图格坐标翻译成 Rhino 的场景锚点与深度范围

- [rhino_render_state.h](/F:/projects/ra2-replica/src/rhino_render_state.h)
- [rhino_render_state.cpp](/F:/projects/ra2-replica/src/rhino_render_state.cpp)
  - 从调试面板状态组装 Rhino 渲染状态

- [vpl_box_renderer.h](/F:/projects/ra2-replica/src/vpl_box_renderer.h)
- [vpl_box_renderer.cpp](/F:/projects/ra2-replica/src/vpl_box_renderer.cpp)
  - 共享 VXL 渲染器
  - 车体 / 炮塔 / 炮管部件组合
  - 法线表、VPL、palette、remap、阴影

- [vxl_file.h](/F:/projects/ra2-replica/src/vxl_file.h)
- [vxl_file.cpp](/F:/projects/ra2-replica/src/vxl_file.cpp)
  - `VXL/HVA` 解析

- [vpl_file.h](/F:/projects/ra2-replica/src/vpl_file.h)
- [vpl_file.cpp](/F:/projects/ra2-replica/src/vpl_file.cpp)
  - `voxels.vpl` 解析

- [voxel_normals.h](/F:/projects/ra2-replica/src/voxel_normals.h)
  - TS / RA2 法线表

## 调色盘与颜色

- [palette.h](/F:/projects/ra2-replica/src/palette.h)
- [palette.cpp](/F:/projects/ra2-replica/src/palette.cpp)
  - 调色盘读取
  - RGB565 量化
  - remap 颜色处理

- [rules_colors.h](/F:/projects/ra2-replica/src/rules_colors.h)
- [rules_colors.cpp](/F:/projects/ra2-replica/src/rules_colors.cpp)
  - `rules.ini [Colors]` 解析

## OpenGL 装载

- [gl_loader.h](/F:/projects/ra2-replica/src/gl_loader.h)
- [gl_loader.cpp](/F:/projects/ra2-replica/src/gl_loader.cpp)
  - `SDL + GLAD` 的统一入口

## 参考资料

- [ModEnc: Voxel](https://modenc.renegadeprojects.com/Voxel)
- [ModEnc: HVA](https://modenc.renegadeprojects.com/HVA)
- [ModEnc: VPL](https://modenc.renegadeprojects.com/VPL)
- [ModEnc: Normals](https://modenc.renegadeprojects.com/Normals)
