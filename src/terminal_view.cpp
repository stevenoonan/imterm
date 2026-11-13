#include "terminal_view.h"
#include "terminal_keyboard.h"
#include "terminal_viewport.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "imgui_internal.h"

namespace imterm {
static_assert(sizeof(ImWchar) == 4, "Build ImGui and consumers with IMGUI_USE_WCHAR32");

TerminalView::TerminalView(std::shared_ptr<TerminalData> data, std::shared_ptr<TerminalState> state, Options options)
    : mData(std::move(data)), mState(std::move(state)), mSelection(*mData), mOptions(options) {}

void TerminalView::Render(const char* title, const ImVec2& size, bool border)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(
        mRenderer.palette[static_cast<size_t>(PaletteIndex::Background)]));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
    if (!mIgnoreImGuiChild) {
        if (mTopVisibleLine) {
            const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
            ImGui::SetNextWindowContentSize({0, static_cast<float>(mData->GetLineCount()) * lineHeight});
            ImGui::SetNextWindowScroll({-1, static_cast<float>(*mTopVisibleLine) * lineHeight});
            mTopVisibleLine.reset();
        }
        ImGui::BeginChild(title, size, border, ImGuiWindowFlags_HorizontalScrollbar
            | ImGuiWindowFlags_AlwaysHorizontalScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNavInputs);
    }
    else if (mTopVisibleLine) {
        ImGui::SetScrollY(static_cast<float>(*mTopVisibleLine) * ImGui::GetTextLineHeightWithSpacing());
        mTopVisibleLine.reset();
    }
    if (mScrollToTop) { ImGui::SetScrollY(0); mScrollToTop = false; }
    // Layout precedes interaction, so hit testing/page navigation uses this
    // frame's font, margins, and scroll position, including on the first frame.
    mGeometry = mRenderer.Layout(*mData, mOptions);
    mState->SetViewportSize(mGeometry.mRows, mGeometry.mColumns);
    if (mHandleKeyboardInputs) HandleKeyboard();
    if (mHandleMouseInputs) HandleMouse();
    if (mStartTime < 0) mStartTime = ImGui::GetTime();
    mStats = mRenderer.Draw(*mData, mSelection, mState->getPositionRelative(mData->GetLineCount()),
        mGeometry, mOptions, ImGui::IsWindowFocused(), ImGui::GetTime() - mStartTime, mShowWhitespaces);
    ApplyReveal();
    mData->SetTextChanged(false);
    if (!mIgnoreImGuiChild) ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void TerminalView::HandleKeyboard()
{
    const int page = std::max(1, mGeometry.mRows - 4);
    for (const auto& action : ReadTerminalKeyboard()) {
        switch (action.result.action) {
        case KeyAction::None: break;
        case KeyAction::Send: AddKeyboardInput(action.result.bytes); break;
        case KeyAction::Copy: Copy(); break;
        case KeyAction::Paste: Paste(); break;
        case KeyAction::SelectAll: SelectAll(); break;
        case KeyAction::PageUp: MoveUp(page, action.extendSelection); break;
        case KeyAction::PageDown: MoveDown(page, action.extendSelection); break;
        case KeyAction::Top: MoveTop(action.extendSelection); break;
        case KeyAction::Bottom: MoveBottom(action.extendSelection); break;
        }
    }
}

void TerminalView::HandleMouse()
{
    if (!ImGui::IsWindowHovered()) return;
    ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    auto& io = ImGui::GetIO();
    if (io.KeyShift || (io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeyAlt)) return;
    const bool shortcut = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
    const auto position = mRenderer.HitTest(*mData, mGeometry, ImGui::GetMousePos());
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const bool twice = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        const bool three = !twice && mLastClick >= 0 && ImGui::GetTime() - mLastClick < io.MouseDoubleClickTime;
        const auto mode = three && !shortcut ? SelectionMode::Line
            : twice || shortcut ? SelectionMode::Word : SelectionMode::Normal;
        mSelection.Begin(position, mode);
        mLastClick = three ? -1 : ImGui::GetTime();
        ImGui::SetWindowFocus();
    }
    else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        if (HasSelection()) { Copy(); ClearSelection(); }
        else Paste();
    }
    else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        io.WantCaptureMouse = true;
        mSelection.Drag(position);
    }
}

void TerminalView::ApplyReveal()
{
    if (!mRevealPosition) return;
    const auto position = mSelection.Clamp(*mRevealPosition);
    const auto scroll = RevealTerminalPoint({mGeometry.mScrollX, mGeometry.mScrollY},
        mGeometry.mContentRegionAvail.x, mGeometry.mContentRegionAvail.y,
        mGeometry.mTextStart + mRenderer.TextDistance(*mData, position), static_cast<float>(position.mLine) * mGeometry.mCellAdvance.y,
        mGeometry.mCellAdvance.x, mGeometry.mCellAdvance.y);
    ImGui::SetScrollX(scroll.x);
    ImGui::SetScrollY(scroll.y);
    if (mFocusOnReveal) ImGui::SetWindowFocus();
    mFocusOnReveal = false;
    mRevealPosition.reset();
}

void TerminalView::SetCursorPosition(Coordinates position) { mSelection.SetCursor(position); Reveal(position); mFocusOnReveal = true; }
void TerminalView::SetCursorToEnd()
{
    const int line = static_cast<int>(mData->GetLineCount()) - 1;
    SetCursorPosition({line, mData->GetLineMaxColumn(line)});
}
void TerminalView::ClearContents()
{
    mState->ClearContents();
    mSelection.Reset();
    mRevealPosition.reset();
    mTopVisibleLine.reset();
    mScrollToTop = true;
}
void TerminalView::SetSelection(Coordinates start, Coordinates end, SelectionMode mode) { mSelection.SetRange(start, end, mode); }
void TerminalView::Reveal(Coordinates position) { mRevealPosition = mSelection.Clamp(position); mTopVisibleLine.reset(); mScrollToTop = false; mFocusOnReveal = false; }
void TerminalView::SetTopVisibleLine(int line) { mTopVisibleLine = std::max(0, line); mRevealPosition.reset(); mScrollToTop = false; }
void TerminalView::Move(SelectionMove direction, int count, bool select, bool word)
{
    mSelection.Move(direction, count, select, word);
    Reveal(mSelection.Cursor());
    mFocusOnReveal = true;
}
void TerminalView::MoveUp(int count, bool select) { Move(SelectionMove::Up, count, select); }
void TerminalView::MoveDown(int count, bool select) { Move(SelectionMove::Down, count, select); }
void TerminalView::MoveLeft(int count, bool select, bool word) { Move(SelectionMove::Left, count, select, word); }
void TerminalView::MoveRight(int count, bool select, bool word) { Move(SelectionMove::Right, count, select, word); }
void TerminalView::MoveTop(bool select) { Move(SelectionMove::Top, 1, select); }
void TerminalView::MoveBottom(bool select) { Move(SelectionMove::Bottom, 1, select); }
void TerminalView::MoveHome(bool select) { Move(SelectionMove::Home, 1, select); }
void TerminalView::MoveEnd(bool select) { Move(SelectionMove::End, 1, select); }
void TerminalView::Copy() { ImGui::SetClipboardText((HasSelection() ? GetSelectedText() : mSelection.CurrentLineText()).c_str()); }
void TerminalView::Paste() { if (const auto* text = ImGui::GetClipboardText()) AddKeyboardInput(text); }
void TerminalView::AddKeyboardInput(std::string_view input) { for (const unsigned char byte : input) mKeyboardInput.push(byte); }
void TerminalView::AddKeyboardInput(char input) { mKeyboardInput.push(static_cast<uint8_t>(input)); }
void TerminalView::AddKeyboardCodePoint(uint32_t input) { AddKeyboardInput(utf8::Encode(input)); }
uint8_t TerminalView::GetKeyboardInput()
{
    if (mKeyboardInput.empty()) throw std::underflow_error("No keyboard input available");
    const auto byte = mKeyboardInput.front();
    mKeyboardInput.pop();
    return byte;
}
} // namespace imterm
