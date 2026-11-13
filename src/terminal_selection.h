#pragma once

#include <optional>
#include <string>

#include "terminal_data.h"

namespace imterm {

enum class SelectionMode { Normal, Word, Line };
enum class SelectionMove { Up, Down, Left, Right, Top, Bottom, Home, End };

struct LineSelection {
    int firstColumn = 0;
    int lastColumn = 0;
    bool includesNewline = false;
};

// UI caret and selection only. Reads terminal text without modifying it, and
// has no dependency on ImGui, fonts, clipboard services, or the device cursor.
class TerminalSelection {
public:
    explicit TerminalSelection(const TerminalData& data) : mData(data) {}
    Coordinates Clamp(Coordinates position) const;
    Coordinates Cursor() const { return Clamp(mCursor); }
    Coordinates Start() const { return Clamp(mStart); }
    Coordinates End() const { return Clamp(mEnd); }
    bool HasSelection() const { return End() > Start(); }
    void SetCursor(Coordinates position);
    void SetRange(Coordinates start, Coordinates end, SelectionMode mode = SelectionMode::Normal);
    void Clear();
    void Reset();
    void SelectAll();
    void Begin(Coordinates position, SelectionMode mode);
    void Drag(Coordinates position);
    void Move(SelectionMove direction, int count = 1, bool extend = false, bool word = false);
    std::string SelectedText() const;
    std::string CurrentLineText() const;
    std::optional<LineSelection> OnLine(int line) const;
private:
    const TerminalData& mData;
    Coordinates mCursor, mStart, mEnd, mAnchor;
    int mPreferredColumn = 0;
    Coordinates mDragStart;
    SelectionMode mDragMode = SelectionMode::Normal;
    Coordinates WordStart(Coordinates position) const;
    Coordinates WordEnd(Coordinates position) const;
    Coordinates Step(Coordinates position, bool forward) const;
};

} // namespace imterm
