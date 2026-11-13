#include <gtest/gtest.h>
#include <functional>

#include "terminal_view.h"
#include "test_support.h"

namespace {
std::string clipboard;
class TerminalViewUiTest : public testing::Test {
protected:
    std::shared_ptr<imterm::TerminalData> data;
    std::shared_ptr<imterm::TerminalState> state;
    std::unique_ptr<imterm::TerminalView> view;
    void SetUp() override {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {800, 600};
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        auto& platform = ImGui::GetPlatformIO();
        platform.Platform_GetClipboardTextFn = [](ImGuiContext*) { return clipboard.c_str(); };
        platform.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* value) { clipboard = value; };
        clipboard.clear();
        data = std::make_shared<imterm::TerminalData>();
        state = std::make_shared<imterm::TerminalState>(data, imterm::TerminalState::NewLineMode::Strict);
        view = std::make_unique<imterm::TerminalView>(data, state, imterm::TerminalView::Options{false, false});
        view->SetImGuiChildIgnored(true);
    }
    void TearDown() override { view.reset(); ImGui::DestroyContext(); }
    void Frame(std::function<void()> inspect = {}) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({800, 600});
        ImGui::SetNextWindowFocus();
        ImGui::Begin("View test");
        view->Render("Terminal");
        if (inspect) inspect();
        ImGui::End();
        ImGui::Render();
    }
    std::string Drain() {
        std::string bytes;
        while (view->KeyboardInputAvailable()) bytes += static_cast<char>(view->GetKeyboardInput());
        return bytes;
    }
    void Key(ImGuiKey key, bool shortcut = false) {
        auto& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, shortcut);
        io.AddKeyEvent(key, true);
        Frame();
        io.AddKeyEvent(key, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        Frame();
    }
};

TEST_F(TerminalViewUiTest, FirstLayoutIsCompleteAndMarginsFollowCurrentOptions)
{
    EXPECT_FALSE(view->GetLastRenderGeometry().mValid);
    data->SetTextLines({"first", "second"});
    Frame();
    Frame();
    const auto plain = view->GetLastRenderGeometry();
    EXPECT_TRUE(plain.mValid);
    EXPECT_GT(plain.mCellAdvance.x, 0);
    EXPECT_GT(plain.mCellAdvance.y, 0);
    EXPECT_EQ(plain.mTotalLines, 2);
    EXPECT_FLOAT_EQ(plain.mTextScreenPos.y, plain.mCursorScreenPos.y + plain.mFirstVisibleLineNo * plain.mCellAdvance.y);
    EXPECT_EQ(view->GetLastRenderStats().marginLines, 0);
    EXPECT_EQ(view->GetLastRenderStats().textRuns, 2);
    view->SetOptions({true, true});
    Frame();
    EXPECT_GT(view->GetLastRenderGeometry().mTextStart, plain.mTextStart);
    EXPECT_EQ(view->GetLastRenderStats().marginLines, 2);
    view->SetOptions({false, false});
    Frame();
    EXPECT_FLOAT_EQ(view->GetLastRenderGeometry().mTextStart, plain.mTextStart);
}

TEST_F(TerminalViewUiTest, HitTestingUsesUtf8TabsAndCurrentFrameZoomAndMargins)
{
    data->SetText("A\xC3\xA9\tZ");
    data->SetTabSize(4);
    const auto base = ImGui::GetStyle();
    for (float scale : {1.0f, 1.5f, 2.0f, 0.75f}) {
        ImGui::GetStyle() = base;
        ImGui::GetStyle().ScaleAllSizes(scale);
        ImGui::GetStyle().FontScaleMain = scale;
        view->SetOptions({true, true});
        Frame([&] {
            const auto& geometry = view->GetLastRenderGeometry();
            imterm::TerminalRenderer renderer;
            for (int column : {0, 1, 2, 4, 5}) {
                const float x = geometry.mCursorScreenPos.x + geometry.mTextStart
                    + renderer.TextDistance(*data, {0, column}) + 0.1f;
                EXPECT_EQ(renderer.HitTest(*data, geometry, {x, geometry.mTextScreenPos.y + 1}), Coordinates(0, column));
            }
        });
    }
}

TEST_F(TerminalViewUiTest, UiCaretAndCopyStayIndependentOfTheDeviceCursor)
{
    state->SetViewportSize(24, 80);
    state->Input(imterm::test::Bytes("first\r\nsecond"));
    view->SetCursorPosition({0, 2});
    Frame();
    Frame();
    EXPECT_EQ(view->GetCursorPosition(), Coordinates(0, 2));
    EXPECT_EQ(state->getPositionRelative(data->GetLineCount()), Coordinates(1, 6));
    view->Copy();
    EXPECT_EQ(clipboard, "first");
    view->SetSelection({0, 1}, {1, 3});
    view->Copy();
    EXPECT_EQ(clipboard, "irst\nsec");
}

TEST_F(TerminalViewUiTest, ShortcutPasteAndSelectAllNeverSendExtraControlBytes)
{
    data->SetText("hello");
    Frame(); Frame();
    clipboard = "paste\xC3\xA9";
    Key(ImGuiKey_V, true);
    EXPECT_EQ(Drain(), clipboard);
    Key(ImGuiKey_A, true);
    EXPECT_TRUE(Drain().empty());
    EXPECT_EQ(view->GetSelectedText(), "hello");
    Key(ImGuiKey_Z, true);
    EXPECT_EQ(Drain(), "\x1a");
    Key(ImGuiKey_C, true);
    EXPECT_EQ(Drain(), "\x03");
    Key(ImGuiKey_End);
    EXPECT_EQ(Drain(), "\x1b[4~");
}

TEST_F(TerminalViewUiTest, MouseAndKeyboardEnableFlagsAreIndependent)
{
    view->SetHandleMouseInputs(false);
    EXPECT_FALSE(view->IsHandleMouseInputsEnabled());
    EXPECT_TRUE(view->IsHandleKeyboardInputsEnabled());
    view->SetHandleMouseInputs(true);
    view->SetHandleKeyboardInputs(false);
    EXPECT_TRUE(view->IsHandleMouseInputsEnabled());
    EXPECT_FALSE(view->IsHandleKeyboardInputsEnabled());
    Frame(); Frame();
    ImGui::GetIO().AddInputCharacter('A');
    Frame();
    EXPECT_TRUE(Drain().empty());
    view->SetHandleKeyboardInputs(true);
    ImGui::GetIO().AddInputCharacter('B');
    Frame();
    EXPECT_EQ(Drain(), "B");
}

TEST_F(TerminalViewUiTest, AltGrTextDoesNotAlsoSendAnAlphabeticControlByte)
{
    Frame(); Frame();
    auto& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiMod_Alt, true);
    io.AddKeyEvent(ImGuiKey_E, true);
    io.AddInputCharacter(0x20ac);
    Frame();
    io.AddKeyEvent(ImGuiKey_E, false);
    io.AddKeyEvent(ImGuiMod_Alt, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    Frame();
    EXPECT_EQ(Drain(), "\xE2\x82\xAC");
}

TEST_F(TerminalViewUiTest, RenderPassesReportSelectionColorRunsAndDeterministicCursor)
{
    state->SetViewportSize(24, 80);
    state->Input(imterm::test::Bytes("red\x1b[32mgreen\x1b[0m\r\nlast"));
    view->SetSelection({0, 1}, {1, 2});
    Frame([&] {
        const auto& geometry = view->GetLastRenderGeometry();
        EXPECT_EQ(view->GetLastRenderStats().selectionRectangles, 2);
        EXPECT_EQ(view->GetLastRenderStats().textRuns, 3);
        imterm::TerminalRenderer renderer;
        imterm::TerminalSelection selection(*data);
        auto on = renderer.Draw(*data, selection, {1, 2}, geometry, {false, false}, true, 0.5, false);
        auto off = renderer.Draw(*data, selection, {1, 2}, geometry, {false, false}, true, 0.1, false);
        auto unfocused = renderer.Draw(*data, selection, {1, 2}, geometry, {false, false}, false, 0.5, false);
        EXPECT_EQ(on.cursorRectangles, 1);
        EXPECT_EQ(off.cursorRectangles, 0);
        EXPECT_EQ(unfocused.cursorRectangles, 0);
    });
}

TEST_F(TerminalViewUiTest, EmptyBufferAndClearHaveDefinedGeometryAndResetSelection)
{
    Frame();
    EXPECT_EQ(view->GetLastRenderStats().textRuns, 0);
    EXPECT_GE(view->GetLastRenderGeometry().mRows, 1);
    EXPECT_GE(view->GetLastRenderGeometry().mColumns, 1);
    data->SetTextLines({"first", "second"});
    view->SelectAll();
    view->ClearContents();
    Frame(); Frame();
    EXPECT_EQ(data->GetTextLines(), std::vector<std::string>{""});
    EXPECT_FALSE(view->HasSelection());
    EXPECT_EQ(view->GetLastRenderGeometry().mTotalLines, 1);
    EXPECT_EQ(view->GetLastRenderGeometry().mFirstVisibleLineNo, 0);
}

TEST_F(TerminalViewUiTest, MouseDragAndRightClickCopyPasteUseTheSelectionModel)
{
    data->SetText("abcdef");
    Frame(); Frame();
    const auto geometry = view->GetLastRenderGeometry();
    auto& io = ImGui::GetIO();
    const auto point = [&](int column) {
        io.AddMousePosEvent(geometry.mTextScreenPos.x + column * geometry.mCellAdvance.x + 0.1f,
            geometry.mTextScreenPos.y + geometry.mCellAdvance.y / 2);
    };
    point(2);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    Frame();
    EXPECT_EQ(view->GetCursorPosition(), Coordinates(0, 2));
    point(5);
    Frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    Frame();
    EXPECT_EQ(view->GetSelectedText(), "cde");
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
    Frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
    Frame();
    EXPECT_EQ(clipboard, "cde");
    EXPECT_FALSE(view->HasSelection());
    EXPECT_TRUE(Drain().empty());
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
    Frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
    Frame();
    EXPECT_EQ(Drain(), "cde");
}
} // namespace
