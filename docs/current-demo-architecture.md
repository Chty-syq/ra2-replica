# 当前 Demo 架构说明

这份文档现在只保留为“当前入口索引”。

## 优先阅读

1. [code-structure.md](/F:/projects/ra2-replica/docs/code-structure.md)
   - 当前仍在使用的模块划分
   - 主入口、建筑系统、2D 渲染、VXL 渲染的职责边界

2. [vxl-rendering-design.md](/F:/projects/ra2-replica/docs/vxl-rendering-design.md)
   - VXL 载具渲染设计

3. [tank-shadow-rendering-design.md](/F:/projects/ra2-replica/docs/tank-shadow-rendering-design.md)
   - 载具阴影设计

## 当前主入口

当前真正参与构建的主入口是：

- [app_main_ui.cpp](/F:/projects/ra2-replica/src/app_main_ui.cpp)

旧的 `app_main.cpp`、`app_main_clean.cpp`、`app_main_runtime.cpp`、`main.cpp`
已经在结构整理时移除，不再作为当前实现的一部分。
