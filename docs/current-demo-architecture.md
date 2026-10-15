# 当前 Demo 架构说明

本文记录项目目标、后续实现原则和当前入口索引。

## 实现状态更新（2026-09-12）

配置驱动单位系统已接入主程序，新增纯 CPU 模拟核心及只读表现适配。
当前模块和已知限制以 [实现记录](unit-system-implementation.md) 及 [代码结构](code-structure.md) 为准。
工厂内部出车已接入通用移动、通道预留和表现层，见 [出车实现](factory-egress-implementation.md)。
已统一游戏帧并接入对战/单人速度档位，见 [速度实现](game-speed-implementation.md)。
移动、转向、生产和动画已完成基于规则与参考引擎的首轮校准，见 [校准记录](gameplay-calibration-2026-09-12.md)。
2026-09-16 新增通用采矿闭环、矿场泊位和矿车生产接入，见 [采矿实现与还原差异](harvesting-system-implementation.md)。
2026-10-10：普通矩形建筑统一采用占地起始格与 SHP 画布中心定位，各层独立居中并读取配置位移，见 [定位研究与落地记录](building-placement-research-2026-10-10.md)。
本文其余部分保留既有渲染设计背景；旧的 Rhino/MCV 单例玩法流程已被 World 命令系统替换。

## 项目目标

以原版红色警戒 2 的玩家可观察表现为基准，尽量还原画面、交互、游戏节奏和单位行为，
同时通过现代 C++ 和图形技术改善性能、稳定性与可维护性。
原版的源码结构、机器指令和内部算法一致性不属于验收目标。

还原度主要从以下方面衡量：

- 画面：原版素材、调色盘与阵营色、体素明暗、阴影、遮挡、动画时序与整体风格。
- 操作：选中与命令反馈、转向和移动手感、建筑放置、生产与出厂、部署和收起。
- 玩法：移动速度、武器射程与射速、伤害、采矿与经济节奏，以及单位之间的交互。
- 声音：音效选择、触发时机，以及与动作的对应关系。

OpenRA 等项目用于参考架构与实现方法，最终表现以原版对照场景为准。
影响玩家操作和策略的细节应记录差异，避免在性能优化中无意改变规则。

## 后续实现原则

以下是后续实现与优化的方向，不代表当前已全部实现。

1. **分离模拟和显示。** 使用固定模拟步长维持游戏时序，渲染独立运行并按需插值；
   提升显示帧率时，单位速度、开火间隔和建造耗时保持稳定。
2. **用 GPU 实现原版观感。** 可通过调色盘/VPL 查表、着色器、实例化、批次绘制和深度处理
   复现所需结果；光照、阴影和遮挡以原版画面标定。批次合并必须保持正确的透明与遮挡顺序。
3. **共享资源，明确所有权。** 资源解码和上传尽量复用，采用 RAII 管理生命周期；
   根据性能测量改善热路径的数据布局、临时分配和重复计算。
4. **以对照场景验收。** 为坦克转向、建筑前后穿行、工厂出车、开火、采矿等建立可重复场景，
   对齐原版版本、规则、镜头和时间点，比较截图、动作耗时、轨迹和事件顺序。
5. **同时验证性能与表现。** 在相同硬件、分辨率和场景规模下，记录 CPU/GPU 帧耗时、
   帧耗时分布、内存/显存占用及加载耗时，并检查优化后的可观察行为。
6. **用配置组合通用能力。** 类型定义连接 `rules.ini`、`art.ini` 和项目补充配置，
   实例保存运行状态，共享系统实现移动、武器、生产和部署等行为。
   常规单位通过数据差异扩展；新机制通过新增可复用能力支持。表现参数与玩法数值分别管理。

当前阶段优先完善模拟时序、动态占用与移动行为，再依据测量结果推进资源复用和绘制优化。

## 优先阅读

1. [code-structure.md](/F:/projects/ra2-replica/docs/code-structure.md)
   - 当前仍在使用的模块划分
   - 主入口、建筑系统、2D 渲染、VXL 渲染的职责边界

2. [vxl-rendering-design.md](/F:/projects/ra2-replica/docs/vxl-rendering-design.md)
   - VXL 载具渲染设计

3. [tank-shadow-rendering-design.md](/F:/projects/ra2-replica/docs/tank-shadow-rendering-design.md)
   - 载具阴影设计

4. [unit-system-review-2026-09-11.md](/F:/projects/ra2-replica/docs/unit-system-review-2026-09-11.md)
   - 当前单位硬编码、规则接入和实例状态问题
   - 配置驱动单位系统的边界、迁移顺序与验收条件

5. [unit-system-implementation-plan.md](/F:/projects/ra2-replica/docs/unit-system-implementation-plan.md)
   - OpenRA、Chrono Divide、EnTT/Flecs 等调研与选型
   - 配置导入、能力系统、模拟时序、表现分离和分批实施方案

## 当前主入口

当前真正参与构建的主入口是：

- [app_main_ui.cpp](/F:/projects/ra2-replica/src/app_main_ui.cpp)

旧的 `app_main.cpp`、`app_main_clean.cpp`、`app_main_runtime.cpp`、`main.cpp`
已经在结构整理时移除，不再作为当前实现的一部分。
