#pragma once

#include "demo_style.h"
#include "imgui_debug_panel.h"
#include "vpl_box_renderer.h"

// 根据当前调试面板状态，组装 Rhino 的渲染状态。
//
// 参考资料：
// - https://modenc.renegadeprojects.com/Voxel
// - https://modenc.renegadeprojects.com/HVA
// - https://modenc.renegadeprojects.com/VPL
// - https://modenc.renegadeprojects.com/Normals
//
// 这里故意把“主项目里的调试参数”翻译成“渲染器真正消费的状态”，
// 让主循环不需要再关心矩阵拼装、角度换算和光照方向归一化这些细节。
[[nodiscard]] VplBoxRendererState buildRhinoTankRenderState(const HouseColorSet& houseColors,
                                                            const ImGuiDebugPanelState& debugPanelState);

[[nodiscard]] VplBoxRendererState buildRhinoTankRenderState(const HouseColorSet& houseColors,
                                                            const ImGuiDebugPanelState& debugPanelState,
                                                            int directionIndex);

[[nodiscard]] VplBoxRendererState buildRhinoTankRenderState(const HouseColorSet& houseColors,
                                                            const ImGuiDebugPanelState& debugPanelState,
                                                            float directionRadians);
