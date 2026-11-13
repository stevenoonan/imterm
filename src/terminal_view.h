#pragma once

#include <memory>
#include <optional>
#include <queue>
#include <string_view>

#include "terminal_renderer.h"
#include "terminal_state.h"

namespace imterm {
// ImGui/session adapter. Selection, input policy, theme, layout, and painting
// have separate components; this facade only coordinates their lifetimes.
class TerminalView {
public:
    using Options = TerminalViewOptions;
    using SelectionMode = imterm::SelectionMode;
    using Palette = TerminalPalette;
    using RenderGeometry = TerminalRenderGeometry;
    TerminalView(std::shared_ptr<TerminalData> data, std::shared_ptr<TerminalState> state, Options options);
    void Render(const char* title, const ImVec2& size = {}, bool border = false);
    const Palette& GetPalette() const { return mRenderer.palette; }
    void SetPalette(const Palette& palette) { mRenderer.palette = palette; }
    static const Palette& GetDarkPalette() { return GetDarkTerminalPalette(); }
    static const Palette& GetLightPalette() { return GetLightTerminalPalette(); }
    static const Palette& GetRetroBluePalette() { return GetRetroBlueTerminalPalette(); }
    Options GetOptions() const { return mOptions; }
    void SetOptions(Options options) { mOptions = options; }
    void SetHandleMouseInputs(bool value) { mHandleMouseInputs = value; }
    bool IsHandleMouseInputsEnabled() const { return mHandleMouseInputs; }
    void SetHandleKeyboardInputs(bool value) { mHandleKeyboardInputs = value; }
    bool IsHandleKeyboardInputsEnabled() const { return mHandleKeyboardInputs; }
    void SetImGuiChildIgnored(bool value) { mIgnoreImGuiChild = value; }
    bool IsImGuiChildIgnored() const { return mIgnoreImGuiChild; }
    void SetShowWhitespaces(bool value) { mShowWhitespaces = value; }
    bool IsShowingWhitespaces() const { return mShowWhitespaces; }
    bool IsTextChanged() const { return mData->IsTextChanged(); }
    Coordinates GetCursorPosition() const { return mSelection.Cursor(); }
    void SetCursorPosition(Coordinates position);
    void ClearContents();
    void SetCursorToEnd();
    void MoveUp(int count = 1, bool select = false);
    void MoveDown(int count = 1, bool select = false);
    void MoveLeft(int count = 1, bool select = false, bool word = false);
    void MoveRight(int count = 1, bool select = false, bool word = false);
    void MoveTop(bool select = false);
    void MoveBottom(bool select = false);
    void MoveHome(bool select = false);
    void MoveEnd(bool select = false);
    void SetSelection(Coordinates start, Coordinates end, SelectionMode mode = SelectionMode::Normal);
    Coordinates GetSelectionStart() const { return mSelection.Start(); }
    Coordinates GetSelectionEnd() const { return mSelection.End(); }
    bool HasSelection() const { return mSelection.HasSelection(); }
    std::string GetSelectedText() const { return mSelection.SelectedText(); }
    void ClearSelection() { mSelection.Clear(); }
    void SelectAll() { mSelection.SelectAll(); }
    void Reveal(Coordinates position);
    const RenderGeometry& GetLastRenderGeometry() const { return mGeometry; }
    const TerminalRenderStats& GetLastRenderStats() const { return mStats; }
    void SetTopVisibleLine(int line);
    void Copy();
    void Paste();
    // This is the intentional outbound byte queue, including UTF-8 paste.
    void AddKeyboardInput(std::string_view input);
    void AddKeyboardInput(char input);
    void AddKeyboardCodePoint(uint32_t input);
    uint8_t GetKeyboardInput();
    bool KeyboardInputAvailable() const { return !mKeyboardInput.empty(); }
private:
    void Move(SelectionMove direction, int count, bool select, bool word = false);
    void HandleKeyboard();
    void HandleMouse();
    void ApplyReveal();
    std::shared_ptr<TerminalData> mData;
    std::shared_ptr<TerminalState> mState;
    TerminalSelection mSelection;
    TerminalRenderer mRenderer;
    Options mOptions;
    RenderGeometry mGeometry;
    TerminalRenderStats mStats;
    bool mHandleKeyboardInputs = true;
    bool mHandleMouseInputs = true;
    bool mIgnoreImGuiChild = false;
    bool mShowWhitespaces = false;
    bool mScrollToTop = false;
    bool mFocusOnReveal = false;
    std::optional<int> mTopVisibleLine;
    std::optional<Coordinates> mRevealPosition;
    double mLastClick = -1;
    double mStartTime = -1;
    std::queue<uint8_t> mKeyboardInput;
};
} // namespace imterm
