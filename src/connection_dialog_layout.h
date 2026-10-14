#pragma once

#include "imgui.h"

namespace imterm {

struct ConnectionDialogLayout {
    float input_width;
    float line_height;
    float extra_vertical_spacing;
};

inline ConnectionDialogLayout MeasureConnectionDialogLayout(int spacingCount)
{
    // Measure the applied font and style every frame. A zoom request may still
    // be pending, and must not be cached alongside the previous font's sizes.
    const ImGuiStyle& style = ImGui::GetStyle();
    return {
        ImGui::CalcTextSize("00000000000000000000").x,
        ImGui::GetFrameHeightWithSpacing(),
        style.PopupBorderSize + style.ItemSpacing.y * spacingCount
            + style.FramePadding.y * 2.0f + 1.0f
    };
}

}
