#pragma once

#include "imgui.h"
#include "terminal_palette.h"
#include "terminal_selection.h"

namespace imterm {
struct TerminalViewOptions { bool LineNumbers = true; bool TimeStamps = true; };

// A complete layout result for one frame. mCursorScreenPos is the content
// origin; mTextScreenPos is the first visible text row, not the last drawn row.
struct TerminalRenderGeometry {
    bool mValid = false;
    ImVec2 mCursorScreenPos{};
    float mScrollX = 0;
    float mScrollY = 0;
    int mFirstVisibleLineNo = 0;
    int mLastVisibleLineNo = 0;
    int mTotalLines = 0;
    ImVec2 mWindowSize{};
    ImVec2 mContentRegionAvail{};
    ImVec2 mTextScreenPos{};
    ImVec2 mCellAdvance{};
    float mTextStart = 0;
    int mRows = 1;
    int mColumns = 1;
};
struct TerminalRenderStats {
    int marginLines = 0;
    int selectionRectangles = 0;
    int cursorRectangles = 0;
    int textRuns = 0;
};

// Reads text and paints it; never owns/mutates protocol state or transport.
class TerminalRenderer {
public:
    TerminalRenderGeometry Layout(const TerminalData& data, TerminalViewOptions options) const;
    TerminalRenderStats Draw(const TerminalData& data, const TerminalSelection& selection,
        Coordinates deviceCursor, const TerminalRenderGeometry& layout,
        TerminalViewOptions options, bool focused, double elapsedSeconds, bool showWhitespace) const;
    Coordinates HitTest(const TerminalData& data, const TerminalRenderGeometry& layout, ImVec2 point) const;
    float TextDistance(const TerminalData& data, Coordinates position) const;
    TerminalPalette palette = GetDarkTerminalPalette();
private:
    void DrawMargin(const TerminalData& data, int line, ImVec2 origin,
        float textStart, TerminalViewOptions options, const TerminalPalette& colors) const;
    bool DrawSelection(const TerminalData& data, const TerminalSelection& selection,
        int line, ImVec2 origin, const TerminalRenderGeometry& layout, const TerminalPalette& colors) const;
    bool DrawCursor(const TerminalData& data, const TerminalSelection& selection,
        Coordinates cursor, int line, ImVec2 origin, const TerminalRenderGeometry& layout,
        const TerminalPalette& colors, bool focused, double elapsedSeconds) const;
    int DrawText(const TerminalData& data, const Line& line, ImVec2 origin,
        const TerminalPalette& colors, bool showWhitespace) const;
};
} // namespace imterm
