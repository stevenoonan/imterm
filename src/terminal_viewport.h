#pragma once

#include <cstddef>

namespace imterm {
struct VisibleLines { int first = 0; int last = 0; };
struct ViewportScroll { float x = 0; float y = 0; };
VisibleLines CalculateVisibleLines(size_t count, float lineHeight, float scrollY, float viewportHeight);
ViewportScroll RevealTerminalPoint(ViewportScroll scroll, float width, float height,
    float x, float y, float cellWidth, float lineHeight);
} // namespace imterm
