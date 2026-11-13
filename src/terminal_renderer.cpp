#include "terminal_renderer.h"
#include "terminal_viewport.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

#include "imgui_internal.h"

namespace imterm {
namespace {
float Measure(std::string_view text)
{
    if (text.empty()) return 0;
    return ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1,
        text.data(), text.data() + text.size()).x;
}
float TabStop(float x, float space, int tabSize)
{
    const float width = std::max(1.0f, space * static_cast<float>(tabSize));
    return (1 + std::floor((1 + x) / width)) * width;
}
std::string GlyphText(const Line& line, size_t index)
{
    std::string text;
    const auto length = TerminalData::UTF8CharLength(line, index);
    for (size_t i = 0; i < length; ++i) text += static_cast<char>(line[index + i].mChar);
    return text;
}
std::string Margin(const TerminalData& data, int line, TerminalViewOptions options)
{
    std::ostringstream text;
    if (options.LineNumbers)
        text << std::setfill('0') << std::setw(static_cast<int>(std::to_string(data.GetLineCount()).size())) << line + 1 << ' ';
    if (options.TimeStamps) {
        const auto timestamp = std::chrono::system_clock::to_time_t(data.GetLine(static_cast<size_t>(line)).GetTimestamp());
        if (const auto* time = std::localtime(&timestamp)) text << std::put_time(time, "%H:%M:%S ");
        else text << "00:00:00 ";
    }
    return text.str();
}
TerminalPalette ApplyAlpha(const TerminalPalette& palette)
{
    auto result = palette;
    for (auto& color : result) {
        auto rgba = ImGui::ColorConvertU32ToFloat4(color);
        rgba.w *= ImGui::GetStyle().Alpha;
        color = ImGui::ColorConvertFloat4ToU32(rgba);
    }
    return result;
}
uint32_t GlyphColor(const Glyph& glyph, const TerminalPalette& colors)
{
    const auto index = static_cast<size_t>(glyph.mColorIndex);
    return colors[index < colors.size() ? index : static_cast<size_t>(PaletteIndex::Default)];
}
}

TerminalRenderGeometry TerminalRenderer::Layout(const TerminalData& data, TerminalViewOptions options) const
{
    const auto* window = ImGui::GetCurrentWindow();
    const ImVec2 content(std::max(0.0f, window->InnerRect.GetWidth() - window->WindowPadding.x * 2),
        std::max(0.0f, window->InnerRect.GetHeight() - window->WindowPadding.y * 2));
    const ImVec2 cell(std::max(1.0f, Measure("#")), std::max(1.0f, ImGui::GetTextLineHeightWithSpacing()));
    const auto origin = ImGui::GetCursorScreenPos();
    const float scrollX = ImGui::GetScrollX();
    const float scrollY = ImGui::GetScrollY();
    const auto visible = CalculateVisibleLines(data.GetLineCount(), cell.y, scrollY, content.y);
    float textStart = 10;
    if (options.LineNumbers) textStart += Measure(std::to_string(data.GetLineCount()) + " ");
    if (options.TimeStamps) textStart += Measure("12:12:59 ");
    return {true, origin, scrollX, scrollY, visible.first, visible.last, static_cast<int>(data.GetLineCount()),
        ImGui::GetWindowSize(), content, {origin.x + textStart, origin.y + visible.first * cell.y},
        cell, textStart, std::max(1, static_cast<int>(std::ceil(content.y / cell.y))),
        std::max(1, static_cast<int>(std::ceil(content.x / cell.x)))};
}

float TerminalRenderer::TextDistance(const TerminalData& data, Coordinates position) const
{
    const TerminalSelection bounds(data);
    position = bounds.Clamp(position);
    const auto& line = data.GetLine(static_cast<size_t>(position.mLine));
    const auto end = static_cast<size_t>(data.GetCharacterIndex(position));
    const float space = Measure(" ");
    float distance = 0;
    for (size_t index = 0; index < end;) {
        if (line[index].mChar == '\t') distance = TabStop(distance, space, data.GetTabSize());
        else distance += Measure(GlyphText(line, index));
        index += TerminalData::UTF8CharLength(line, index);
    }
    return distance;
}

Coordinates TerminalRenderer::HitTest(const TerminalData& data, const TerminalRenderGeometry& layout, ImVec2 point) const
{
    if (!layout.mValid || data.GetLineCount() == 0) return {};
    const int row = std::clamp(static_cast<int>(std::floor((point.y - layout.mCursorScreenPos.y) / layout.mCellAdvance.y)),
        0, static_cast<int>(data.GetLineCount()) - 1);
    const auto& line = data.GetLine(static_cast<size_t>(row));
    const float x = point.x - layout.mCursorScreenPos.x - layout.mTextStart;
    const float space = Measure(" ");
    float distance = 0;
    size_t index = 0;
    while (index < line.size()) {
        const float next = line[index].mChar == '\t' ? TabStop(distance, space, data.GetTabSize())
            : distance + Measure(GlyphText(line, index));
        if (x < (distance + next) / 2) break;
        distance = next;
        index += TerminalData::UTF8CharLength(line, index);
    }
    return {row, data.GetCharacterColumn(row, static_cast<int>(index))};
}

void TerminalRenderer::DrawMargin(const TerminalData& data, int line, ImVec2 origin,
    float textStart, TerminalViewOptions options, const TerminalPalette& colors) const
{
    const auto text = Margin(data, line, options);
    ImGui::GetWindowDrawList()->AddText({origin.x + textStart - Measure(text), origin.y},
        colors[static_cast<size_t>(PaletteIndex::LineNumber)], text.c_str());
}

bool TerminalRenderer::DrawSelection(const TerminalData& data, const TerminalSelection& selection,
    int line, ImVec2 origin, const TerminalRenderGeometry& layout, const TerminalPalette& colors) const
{
    const auto range = selection.OnLine(line);
    if (!range) return false;
    const float first = TextDistance(data, {line, range->firstColumn});
    const float last = TextDistance(data, {line, range->lastColumn})
        + (range->includesNewline ? layout.mCellAdvance.x : 0);
    if (last <= first) return false;
    ImGui::GetWindowDrawList()->AddRectFilled({origin.x + layout.mTextStart + first, origin.y},
        {origin.x + layout.mTextStart + last, origin.y + layout.mCellAdvance.y},
        colors[static_cast<size_t>(PaletteIndex::Selection)]);
    return true;
}

bool TerminalRenderer::DrawCursor(const TerminalData& data, const TerminalSelection& selection,
    Coordinates cursor, int line, ImVec2 origin, const TerminalRenderGeometry& layout,
    const TerminalPalette& colors, bool focused, double elapsedSeconds) const
{
    if (cursor.mLine != line) return false;
    auto* draw = ImGui::GetWindowDrawList();
    if (!selection.HasSelection()) {
        const ImVec2 first(origin.x + layout.mScrollX, origin.y);
        const ImVec2 last(first.x + layout.mContentRegionAvail.x + layout.mScrollX, first.y + layout.mCellAdvance.y);
        draw->AddRectFilled(first, last, colors[static_cast<size_t>(focused
            ? PaletteIndex::CurrentLineFill : PaletteIndex::CurrentLineFillInactive)]);
        draw->AddRect(first, last, colors[static_cast<size_t>(PaletteIndex::CurrentLineEdge)], 1);
    }
    if (!focused || std::fmod(std::max(0.0, elapsedSeconds), 0.8) < 0.4) return false;
    const float x = origin.x + layout.mTextStart + TextDistance(data, cursor);
    draw->AddRectFilled({x, origin.y}, {x + 1, origin.y + layout.mCellAdvance.y},
        colors[static_cast<size_t>(PaletteIndex::Cursor)]);
    return true;
}

int TerminalRenderer::DrawText(const TerminalData& data, const Line& line, ImVec2 origin,
    const TerminalPalette& colors, bool showWhitespace) const
{
    auto* draw = ImGui::GetWindowDrawList();
    const float space = Measure(" ");
    float x = 0;
    std::string run;
    uint32_t color = colors[static_cast<size_t>(PaletteIndex::Default)];
    int runs = 0;
    const auto flush = [&] {
        if (run.empty()) return;
        draw->AddText({origin.x + x, origin.y}, color, run.c_str());
        x += Measure(run);
        run.clear();
        ++runs;
    };
    for (size_t index = 0; index < line.size();) {
        const auto& glyph = line[index];
        const auto nextColor = GlyphColor(glyph, colors);
        if (color != nextColor || glyph.mChar == '\t' || glyph.mChar == ' ') flush();
        color = nextColor;
        if (glyph.mChar == '\t') {
            const float next = TabStop(x, space, data.GetTabSize());
            if (showWhitespace) {
                const float y = origin.y + ImGui::GetFontSize() / 2;
                const float tip = origin.x + next - 1;
                const float arrow = ImGui::GetFontSize() * 0.2f;
                draw->AddLine({origin.x + x + 1, y}, {tip, y}, 0x90909090);
                draw->AddLine({tip, y}, {tip - arrow, y - arrow}, 0x90909090);
                draw->AddLine({tip, y}, {tip - arrow, y + arrow}, 0x90909090);
            }
            x = next;
        }
        else if (glyph.mChar == ' ') {
            if (showWhitespace) draw->AddCircleFilled({origin.x + x + space / 2,
                origin.y + ImGui::GetFontSize() / 2}, 1.5f, 0x80808080, 4);
            x += space;
        }
        else run += GlyphText(line, index);
        index += TerminalData::UTF8CharLength(line, index);
    }
    flush();
    return runs;
}

TerminalRenderStats TerminalRenderer::Draw(const TerminalData& data, const TerminalSelection& selection,
    Coordinates deviceCursor, const TerminalRenderGeometry& layout,
    TerminalViewOptions options, bool focused, double elapsedSeconds, bool showWhitespace) const
{
    TerminalRenderStats stats;
    const auto colors = ApplyAlpha(palette);
    float longest = layout.mTextStart;
    for (int line = layout.mFirstVisibleLineNo; line <= layout.mLastVisibleLineNo; ++line) {
        const ImVec2 origin(layout.mCursorScreenPos.x, layout.mCursorScreenPos.y + static_cast<float>(line) * layout.mCellAdvance.y);
        longest = std::max(longest, layout.mTextStart + TextDistance(data, {line, data.GetLineMaxColumn(line)}));
        stats.selectionRectangles += DrawSelection(data, selection, line, origin, layout, colors);
        if (options.LineNumbers || options.TimeStamps) {
            DrawMargin(data, line, origin, layout.mTextStart, options, colors);
            ++stats.marginLines;
        }
        stats.cursorRectangles += DrawCursor(data, selection, deviceCursor, line, origin, layout, colors, focused, elapsedSeconds);
        stats.textRuns += DrawText(data, data.GetLine(static_cast<size_t>(line)),
            {origin.x + layout.mTextStart, origin.y}, colors, showWhitespace);
    }
    ImGui::Dummy({longest + 2, static_cast<float>(data.GetLineCount()) * layout.mCellAdvance.y});
    return stats;
}
} // namespace imterm
