#include "terminal_viewport.h"
#include <algorithm>
#include <cmath>

namespace imterm {
VisibleLines CalculateVisibleLines(size_t count, float lineHeight, float scrollY, float viewportHeight)
{
    if (count == 0 || lineHeight <= 0) return {};
    const int last = static_cast<int>(count) - 1;
    const int first = std::clamp(static_cast<int>(std::floor(std::max(0.0f, scrollY) / lineHeight)), 0, last);
    return {first, std::clamp(static_cast<int>(std::floor(
        (std::max(0.0f, scrollY) + std::max(0.0f, viewportHeight)) / lineHeight)), first, last)};
}

ViewportScroll RevealTerminalPoint(ViewportScroll scroll, float width, float height,
    float x, float y, float cellWidth, float lineHeight)
{
    const float verticalMargin = std::min(lineHeight * 3, std::max(0.0f, height - lineHeight) / 2);
    if (y < scroll.y + verticalMargin) scroll.y = std::max(0.0f, y - verticalMargin);
    else if (y + lineHeight > scroll.y + height - verticalMargin)
        scroll.y = std::max(0.0f, y + lineHeight + verticalMargin - height);
    const float horizontalMargin = std::min(cellWidth, std::max(0.0f, width - cellWidth) / 2);
    if (x < scroll.x + horizontalMargin) scroll.x = std::max(0.0f, x - horizontalMargin);
    else if (x + cellWidth > scroll.x + width - horizontalMargin)
        scroll.x = std::max(0.0f, x + cellWidth + horizontalMargin - width);
    return scroll;
}
} // namespace imterm
