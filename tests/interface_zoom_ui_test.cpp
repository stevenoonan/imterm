#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "connection_dialog_layout.h"
#include "terminal_view.h"

namespace {

class InterfaceZoomUiTest : public testing::Test {
protected:
    void SetUp() override
    {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800, 600);
        // The legacy atlas lets these tests run without GPU texture updates.
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        mBaseStyle = ImGui::GetStyle();

        auto data = std::make_shared<imterm::TerminalData>();
        data->SetTextLines(std::vector<std::string>(1000, "line"));
        auto state = std::make_shared<imterm::TerminalState>(
            data, imterm::TerminalState::NewLineMode::Strict);
        mView = std::make_unique<imterm::TerminalView>(
            data, state, imterm::TerminalView::Options{false, false});
        mView->SetHandleKeyboardInputs(false);
        mView->SetHandleMouseInputs(false);
        RenderFrame();
        RenderFrame();
    }

    void TearDown() override
    {
        mView.reset();
        ImGui::DestroyContext();
    }

    void ApplyZoom(float factor)
    {
        const int topLine = mView->GetLastRenderGeometry().mFirstVisibleLineNo;
        auto& style = ImGui::GetStyle();
        style = mBaseStyle;
        style.ScaleAllSizes(factor);
        style.FontScaleMain = factor;
        mView->SetTopVisibleLine(topLine);
    }

    void RenderFrame()
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(800, 600));
        ImGui::Begin("Zoom test");
        mView->Render("Terminal");
        ImGui::End();
        ImGui::Render();
    }

    imterm::ConnectionDialogLayout MeasureDialog(int spacingCount)
    {
        ImGui::NewFrame();
        ImGui::Begin("Dialog layout test");
        const auto layout = imterm::MeasureConnectionDialogLayout(spacingCount);
        ImGui::End();
        ImGui::Render();
        return layout;
    }

    ImGuiStyle mBaseStyle;
    std::unique_ptr<imterm::TerminalView> mView;
};

TEST_F(InterfaceZoomUiTest, PreservesTopLineOnTheFirstZoomFrame)
{
    mView->SetTopVisibleLine(300);
    RenderFrame();
    RenderFrame();
    ASSERT_EQ(mView->GetLastRenderGeometry().mFirstVisibleLineNo, 300);

    ApplyZoom(1.10f);
    RenderFrame();
    EXPECT_EQ(mView->GetLastRenderGeometry().mFirstVisibleLineNo, 300);
    RenderFrame();
    EXPECT_EQ(mView->GetLastRenderGeometry().mFirstVisibleLineNo, 300);
}

TEST_F(InterfaceZoomUiTest, ConsecutiveZoomStepsKeepTheSameTopLine)
{
    mView->SetTopVisibleLine(300);
    RenderFrame();
    RenderFrame();

    for (float factor : {1.10f, 1.25f, 1.50f, 1.25f, 1.00f, 0.50f}) {
        ApplyZoom(factor);
        RenderFrame();
        ASSERT_EQ(mView->GetLastRenderGeometry().mFirstVisibleLineNo, 300);
    }
    RenderFrame();
    EXPECT_EQ(mView->GetLastRenderGeometry().mFirstVisibleLineNo, 300);
}

TEST_F(InterfaceZoomUiTest, ZoomInNearTheBottomUsesTheNewScrollMaximum)
{
    mView->SetTopVisibleLine(900);
    RenderFrame();
    RenderFrame();
    ASSERT_EQ(mView->GetLastRenderGeometry().mFirstVisibleLineNo, 900);

    ApplyZoom(2.0f);
    RenderFrame();
    const auto geometry = mView->GetLastRenderGeometry();
    EXPECT_EQ(geometry.mFirstVisibleLineNo, 900);
    EXPECT_LT(geometry.mLastVisibleLineNo, geometry.mTotalLines);
    EXPECT_LT(geometry.mContentRegionAvail.y, 600.0f);
    RenderFrame();
    EXPECT_EQ(mView->GetLastRenderGeometry().mFirstVisibleLineNo, 900);
}

TEST_F(InterfaceZoomUiTest, ZoomOutClampsToTheNewBottomOnTheFirstFrame)
{
    mView->SetTopVisibleLine(990);
    RenderFrame();
    RenderFrame();

    ApplyZoom(0.50f);
    RenderFrame();
    const auto geometry = mView->GetLastRenderGeometry();
    EXPECT_LT(geometry.mFirstVisibleLineNo, geometry.mTotalLines);
    EXPECT_GE(geometry.mLastVisibleLineNo, geometry.mFirstVisibleLineNo);
    EXPECT_EQ(geometry.mLastVisibleLineNo, geometry.mTotalLines - 1);
    EXPECT_LT(geometry.mContentRegionAvail.y, 600.0f);
    RenderFrame();
    EXPECT_EQ(mView->GetLastRenderGeometry().mFirstVisibleLineNo,
        geometry.mFirstVisibleLineNo);
}

TEST_F(InterfaceZoomUiTest, BothDialogLayoutsFollowTheAppliedFontAndStyle)
{
    for (int spacingCount : {4, 2}) {
        ImGui::GetStyle() = mBaseStyle;
        const auto beforeRequest = MeasureDialog(spacingCount);
        // A keyboard/menu request precedes the frame which applies its zoom.
        const auto pending = MeasureDialog(spacingCount);
        EXPECT_FLOAT_EQ(pending.input_width, beforeRequest.input_width);

        ApplyZoom(1.50f);
        const auto applied = MeasureDialog(spacingCount);
        EXPECT_GT(applied.input_width, beforeRequest.input_width);
        EXPECT_GT(applied.line_height, beforeRequest.line_height);
        EXPECT_GT(applied.extra_vertical_spacing,
            beforeRequest.extra_vertical_spacing);

        ApplyZoom(0.50f);
        const auto reduced = MeasureDialog(spacingCount);
        EXPECT_LT(reduced.input_width, beforeRequest.input_width);
        EXPECT_LT(reduced.line_height, beforeRequest.line_height);
    }
}

}
