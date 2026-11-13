#include "terminal_selection.h"

#include <algorithm>

namespace imterm {
namespace {
bool Space(Char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
bool Continuation(Char c) { return (c & 0xc0) == 0x80; }
}

Coordinates TerminalSelection::Clamp(Coordinates position) const
{
    if (mData.GetLineCount() == 0) return {};
    if (position.mLine >= static_cast<int>(mData.GetLineCount())) {
        const int last = static_cast<int>(mData.GetLineCount()) - 1;
        return {last, mData.GetLineMaxColumn(last)};
    }
    position.mLine = std::max(position.mLine, 0);
    position.mColumn = std::clamp(position.mColumn, 0, mData.GetLineMaxColumn(position.mLine));
    // Snap coordinates inside tabs to a stored character boundary.
    position.mColumn = mData.GetCharacterColumn(position.mLine, mData.GetCharacterIndex(position));
    return position;
}

void TerminalSelection::SetCursor(Coordinates position)
{
    mCursor = Clamp(position);
    mPreferredColumn = mCursor.mColumn;
    mAnchor = mCursor;
}

Coordinates TerminalSelection::Step(Coordinates position, bool forward) const
{
    position = Clamp(position);
    const auto& line = mData.GetLine(static_cast<size_t>(position.mLine));
    int index = mData.GetCharacterIndex(position);
    if (forward) {
        if (index < static_cast<int>(line.size()))
            index += static_cast<int>(TerminalData::UTF8CharLength(line, static_cast<size_t>(index)));
        else if (position.mLine + 1 < static_cast<int>(mData.GetLineCount())) return {position.mLine + 1, 0};
    }
    else {
        if (index > 0) {
            --index;
            while (index > 0 && Continuation(line[static_cast<size_t>(index)].mChar)) --index;
        }
        else if (position.mLine > 0) return {position.mLine - 1, mData.GetLineMaxColumn(position.mLine - 1)};
    }
    return {position.mLine, mData.GetCharacterColumn(position.mLine, index)};
}

Coordinates TerminalSelection::WordStart(Coordinates position) const
{
    position = Clamp(position);
    const auto& line = mData.GetLine(static_cast<size_t>(position.mLine));
    int index = mData.GetCharacterIndex(position);
    if (index == static_cast<int>(line.size())) return position;
    const bool spaces = Space(line[static_cast<size_t>(index)].mChar);
    while (index > 0) {
        int previous = index - 1;
        while (previous > 0 && Continuation(line[static_cast<size_t>(previous)].mChar)) --previous;
        if (Space(line[static_cast<size_t>(previous)].mChar) != spaces) break;
        index = previous;
    }
    return {position.mLine, mData.GetCharacterColumn(position.mLine, index)};
}

Coordinates TerminalSelection::WordEnd(Coordinates position) const
{
    position = Clamp(position);
    const auto& line = mData.GetLine(static_cast<size_t>(position.mLine));
    auto index = static_cast<size_t>(mData.GetCharacterIndex(position));
    if (index == line.size()) return position;
    const bool spaces = Space(line[index].mChar);
    while (index < line.size() && Space(line[index].mChar) == spaces)
        index += TerminalData::UTF8CharLength(line, index);
    return {position.mLine, mData.GetCharacterColumn(position.mLine, static_cast<int>(index))};
}

void TerminalSelection::SetRange(Coordinates start, Coordinates end, SelectionMode mode)
{
    mStart = Clamp(start);
    mEnd = Clamp(end);
    if (mEnd < mStart) std::swap(mStart, mEnd);
    if (mode == SelectionMode::Word) {
        mStart = WordStart(mStart);
        // An exclusive end at a word boundary must not include the next word.
        if (mEnd == mStart || (mEnd.mColumn > 0 && WordStart(mEnd) != mEnd)) mEnd = WordEnd(mEnd);
    }
    else if (mode == SelectionMode::Line) {
        mStart.mColumn = 0;
        mEnd.mColumn = mData.GetLineMaxColumn(mEnd.mLine);
    }
}

void TerminalSelection::Clear() { mStart = mEnd = mAnchor = Cursor(); }
void TerminalSelection::Reset() { mCursor = mStart = mEnd = mAnchor = mDragStart = {}; mPreferredColumn = 0; }
void TerminalSelection::SelectAll() { SetRange({}, {static_cast<int>(mData.GetLineCount()), 0}); }

void TerminalSelection::Begin(Coordinates position, SelectionMode mode)
{
    SetCursor(position);
    mDragStart = Cursor();
    mDragMode = mode;
    SetRange(mDragStart, mDragStart, mode);
    if (mode == SelectionMode::Word) mEnd = WordEnd(mDragStart);
}

void TerminalSelection::Drag(Coordinates position)
{
    mCursor = Clamp(position);
    mPreferredColumn = mCursor.mColumn;
    if (mDragMode == SelectionMode::Word) {
        SetRange(WordStart(std::min(mDragStart, mCursor)), WordEnd(std::max(mDragStart, mCursor)));
        return;
    }
    SetRange(mDragStart, mCursor, mDragMode);
}

void TerminalSelection::Move(SelectionMove direction, int count, bool extend, bool word)
{
    const auto old = Cursor();
    auto position = old;
    count = std::max(count, 0);
    switch (direction) {
    case SelectionMove::Up: position.mLine = std::max(0, position.mLine - count); position.mColumn = mPreferredColumn; break;
    case SelectionMove::Down:
        position.mLine += std::min(count, static_cast<int>(mData.GetLineCount()) - 1 - position.mLine);
        position.mColumn = mPreferredColumn; break;
    case SelectionMove::Top: position = {}; break;
    case SelectionMove::Bottom: position = {static_cast<int>(mData.GetLineCount()) - 1, 0}; break;
    case SelectionMove::Home: position.mColumn = 0; break;
    case SelectionMove::End: position.mColumn = mData.GetLineMaxColumn(position.mLine); break;
    case SelectionMove::Left:
    case SelectionMove::Right:
        for (int i = 0; i < count; ++i) {
            position = Step(position, direction == SelectionMove::Right);
            if (word) position = direction == SelectionMove::Right ? WordEnd(position) : WordStart(position);
        }
        break;
    }
    mCursor = Clamp(position);
    if (direction != SelectionMove::Up && direction != SelectionMove::Down) mPreferredColumn = mCursor.mColumn;
    if (extend) {
        if (!HasSelection()) mAnchor = old;
        SetRange(mAnchor, mCursor);
    }
    else Clear();
}

std::string TerminalSelection::SelectedText() const { return mData.GetText(Start(), End()); }
std::string TerminalSelection::CurrentLineText() const
{
    const int line = Cursor().mLine;
    return mData.GetText({line, 0}, {line, mData.GetLineMaxColumn(line)});
}

std::optional<LineSelection> TerminalSelection::OnLine(int line) const
{
    const auto start = Start();
    const auto end = End();
    if (!HasSelection() || line < start.mLine || line > end.mLine) return std::nullopt;
    const int first = line == start.mLine ? start.mColumn : 0;
    const int last = line == end.mLine ? end.mColumn : mData.GetLineMaxColumn(line);
    if (first == last && line == end.mLine) return std::nullopt;
    return LineSelection{first, last, line < end.mLine};
}

} // namespace imterm
