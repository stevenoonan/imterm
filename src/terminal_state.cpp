#include <stdexcept>
#include <algorithm>
#include <future>
#include <assert.h>

//#include "imgui.h"
#include "terminal_state.h"
#include "beep.h"

namespace imterm {

    TerminalGraphicsState::TerminalGraphicsState() : mState(0)
    {
    }

    TerminalGraphicsState::~TerminalGraphicsState()
    {
    }

    TerminalGraphicsState::Flags TerminalGraphicsState::getForegroundColor()
    {
        return Flags(mState & (int)Flags::MaskFgColor);
    }

    TerminalGraphicsState::Flags TerminalGraphicsState::getBackgroundColor()
    {
        return Flags(mState & (int)Flags::MaskBgColor);
    }

    TerminalGraphicsState::Flags TerminalGraphicsState::getTextFormatting()
    {
        return Flags(mState & (int)Flags::MaskFormat);
    }

    uint32_t TerminalGraphicsState::Update(EscapeSequenceParser::GraphicsCommands gfxCmd)
    {
        int gfxBit = (int)gfxCmd;

        typedef EscapeSequenceParser::GraphicsCommands gfx;
        if (gfxCmd == gfx::Reset) {
            mState = 0;
        }
        else if (gfxCmd >= gfx::Bold && gfxCmd <= gfx::Strikethrough) {
            mState |= (int)Flags::Bold << (gfxBit - (int)gfx::Bold);
        }
        else if (gfxCmd == gfx::BoldOrDimReset) {
            mState &= ~((int)Flags::Bold | (int)Flags::Dim);
        }
        else if (gfxCmd >= gfx::ItalicReset && gfxCmd <= gfx::StrikethroughReset) {
            mState &= ~((int)Flags::Italic << (gfxBit - (int)gfx::ItalicReset));
        }
        else if (gfxCmd >= gfx::BlackFg && gfxCmd <= gfx::WhiteFg) {
            // Clear all other colors, then apply this color
            mState &= ~((int)Flags::MaskFgColor);
            mState |= (int)Flags::BlackFg << (gfxBit - (int)gfx::BlackFg);
        }
        else if (gfxCmd == gfx::DefaultFg) {
            mState &= ~((int)Flags::MaskFgColor);
        }
        else if (gfxCmd >= gfx::BlackBg && gfxCmd <= gfx::WhiteBg) {
            // Clear all other colors, then apply this color
            mState &= ~((int)Flags::MaskBgColor);
            mState |= (int)Flags::BlackBg << (gfxBit - (int)gfx::BlackBg);
        }
        else if (gfxCmd == gfx::DefaultBg) {
            mState &= ~((int)Flags::MaskBgColor);
        }
        else {
            throw std::runtime_error("Unknown ANSI graphics command");
        }

        return mState;
    }

    uint32_t TerminalGraphicsState::Update(const std::vector<int>& aCommandData)
    {
        for (int item : aCommandData) {
            {
                EscapeSequenceParser::GraphicsCommands gfxCmd = static_cast<EscapeSequenceParser::GraphicsCommands>(item);
                Update(gfxCmd);
            }
        }
        return mState;
    }

    TerminalState::TerminalState(TerminalData& aTerminalData, NewLineMode aNewLineMode) : mTerminalData(aTerminalData), mNewLineMode(aNewLineMode)
    {
    }

    TerminalState::~TerminalState()
    {
    }

    void TerminalState::Update(EscapeSequenceParser::ParseResult aSeq)
    {

        // If mOutputChar is set, it is not an escape sequence. Otherwise, only 
        // continue if the sequence has been parsed successfully.
        if ((aSeq.mOutputChar != 0) || (aSeq.mStage != EscapeSequenceParser::Stage::Inactive) || (aSeq.mError != EscapeSequenceParser::Error::None)) {
            return;
        }

        using enum EscapeSequenceParser::CommandType;
        using enum EscapeSequenceParser::EscapeIdentifier;
        EscapeSequenceParser::CommandType type = None;
        bool processed = false;

        Coordinates eraseBegin;
        Coordinates eraseEnd;

        std::vector<uint8_t> output;

        switch (aSeq.mIdentifier) {
        case H_MoveCursor:
            [[fallthrough]];
        case f_MoveCursor:
            if ((aSeq.mIdentifier == H_MoveCursor) && aSeq.mCommandData.size() == 0) {
                type = MoveCursorToHome;
                mCursorPos.mColumn = 0;
                mCursorPos.mLine = 0;
            }
            else if (aSeq.mCommandData.size() == 2) {
                type = MoveCursorAbs;
                mCursorPos.mColumn = aSeq.mCommandData[0];
                mCursorPos.mLine = aSeq.mCommandData[1];
            }
            break;
        case A_MoveCursorUp:
            if (aSeq.mCommandData.size() == 1) {
                type = MoveCursorUp;
                mCursorPos.mLine -= aSeq.mCommandData[0];
            }
            break;
        case B_MoveCursorDown:
            if (aSeq.mCommandData.size() == 1) {
                type = MoveCursorDown;
                mCursorPos.mLine += aSeq.mCommandData[0];
            }
            break;
        case C_MoveCursorRight:
            if (aSeq.mCommandData.size() == 1) {
                type = MoveCursorRight;
                mCursorPos.mColumn += aSeq.mCommandData[0];
            }
            break;
        case D_MoveCursorLeft:
            if (aSeq.mCommandData.size() == 1) {
                type = MoveCursorLeft;
                mCursorPos.mColumn -= aSeq.mCommandData[0];
            }
            break;
        case E_MoveCursorDownBeginning:
            if (aSeq.mCommandData.size() == 1) {
                type = MoveCursorDownBeginning;
                mCursorPos.mLine += aSeq.mCommandData[0];
                mCursorPos.mColumn = 0;
            }
            break;
        case F_MoveCursorUpBeginning:
            if (aSeq.mCommandData.size() == 1) {
                type = MoveCursorUpBeginning;
                mCursorPos.mLine -= aSeq.mCommandData[0];
                mCursorPos.mColumn = 0;
            }
            break;
        case G_MoveCursorCol:
            if (aSeq.mCommandData.size() == 1) {
                type = MoveCursorCol;
                mCursorPos.mColumn = aSeq.mCommandData[0];
            }
            break;
        case s_SaveCursorPosition:
            if (aSeq.mCommandData.size() == 0) {
                type = SaveCursorPosition;
                mSavedCursorPos = mCursorPos;
                processed = true;
            }
            break;
        case u_RestoreCursorPosition:
            if (aSeq.mCommandData.size() == 0) {
                type = RestoreCursorPosition;
                mCursorPos = mSavedCursorPos;
            }
            break;
        case J_EraseDisplay:

            if (aSeq.mCommandData.size() == 0) {
                type = EraseDisplayAfterCursor;
                eraseBegin = mCursorPos;
                eraseEnd = mBounds;
            }
            else if (aSeq.mCommandData.size() == 1) {
                switch (aSeq.mCommandData[0]) {
                case 0:
                    type = EraseDisplayAfterCursor;
                    eraseBegin = mCursorPos;
                    eraseEnd = mBounds;
                    break;
                case 1:
                    type = EraseDisplayBeforeCursor;
                    eraseEnd = mCursorPos;
                    break;
                case 2:
                    type = EraseDisplay;
                    eraseEnd = mBounds;
                    break;
                case 3:
                    type = EraseSavedLines;
                    // ??
                }
            }
            break;
        case K_EraseLine:

            if (aSeq.mCommandData.size() == 0) {
                type = EraseLineAfterCursor;
                eraseBegin = mCursorPos;
                eraseEnd.mLine = eraseBegin.mLine;
                eraseEnd.mColumn = mBounds.mColumn;
            }
            else if (aSeq.mCommandData.size() == 1) {
                switch (aSeq.mCommandData[0]) {
                case 0:
                    type = EraseLineAfterCursor;
                    eraseBegin = mCursorPos;
                    eraseEnd.mLine = eraseBegin.mLine;
                    eraseEnd.mColumn = mBounds.mColumn;
                    break;
                case 1:
                    type = EraseLineBeforeCursor;
                    eraseBegin.mLine = mCursorPos.mLine;
                    eraseEnd = mCursorPos;
                    break;
                case 2:
                    type = EraseLine;
                    eraseBegin.mLine = mCursorPos.mLine;
                    eraseEnd.mLine = mCursorPos.mLine;
                    eraseEnd.mColumn = mBounds.mColumn;
                }
            }
            break;

        case m_SetGraphics:
            mGraphics.Update(aSeq.mCommandData);
            type = SetGraphics;
            processed = true;
            break;

        case n_RequestReport:
            if (aSeq.mCommandData.size() == 1) {
                if (aSeq.mCommandData[0] == 5) {

                    std::string out("\x1b[0n");
                    output = std::vector<uint8_t>(out.begin(), out.end());
                    type = DeviceStatusReport;

                }
                else if (aSeq.mCommandData[0] == 6) {

                    std::string out("\x1b[" + std::to_string(mCursorPos.mLine + 1) + ";" + std::to_string(mCursorPos.mColumn + 1) + "R");
                    output = std::vector<uint8_t>(out.begin(), out.end());
                    type = CursorPositionReport;
                }
            }
            break;
        default:
            assert(0);
        }

        if ((type >= MoveCursorToHome && type <= MoveCursorCol) || (type == RestoreCursorPosition)) {
            SanitzeCursorPosition();
            processed = true;
        }

        if ((type >= EraseDisplayAfterCursor) && (type <= EraseLine)) {
            Coordinates begin = eraseBegin;
            Coordinates end = eraseEnd;

            // Begin and end are in localized Terminal coordinates, not global mLines
            // The last line of the terminal is the max mLine
            begin = getPositionRelative(mTerminalData.mLines.size(), begin);
            end = getPositionRelative(mTerminalData.mLines.size(), end);

            while (begin < end) {
                auto& eraseLine = mTerminalData.mLines[begin.mLine];
                if (begin.mLine < end.mLine) {
                    eraseLine.erase(eraseLine.begin() + begin.mColumn, eraseLine.end());
                    begin.mLine++;
                    begin.mColumn = 0;
                }
                else {
                    // begin and end are on the same line
                    if (end.mColumn == GetBounds().mColumn) {
                        // the end of the erase is the end of the line, so we can erase
                        eraseLine.erase(eraseLine.begin() + begin.mColumn, eraseLine.end());
                    }
                    else {
                        // end of the erase is in the middle of the last line, replace printables with spaces
                        for (auto it = eraseLine.begin() + begin.mColumn; it != eraseLine.begin() + end.mColumn; ++it) {
                            (*it).mChar = ' ';
                        }
                    }
                    begin.mColumn = end.mColumn;
                }
            }

            processed = true;
        }

        if (output.size() > 0) {
            mQueuedTerminalOutput.push(std::move(output));
        }
    }

    void TerminalState::SetBounds(Coordinates aBounds)
    {
        mBounds = aBounds;
        SanitzeCursorPosition();
    }

    void TerminalState::SanitzeCursorPosition()
    {
        if (mCursorPos.mColumn > mBounds.mColumn) {
            mCursorPos.mColumn = mBounds.mColumn;
        }

        if (mCursorPos.mLine > mBounds.mLine) {
            mCursorPos.mLine = mBounds.mLine;
        }
    }

    std::vector<uint8_t> TerminalState::GetTerminalOutput()
    {
        if (mQueuedTerminalOutput.empty()) {
            throw std::underflow_error("No terminal output to get. Check TerminalOutputAvailable() before calling.");
        }
        else {
            auto item = mQueuedTerminalOutput.front();
            mQueuedTerminalOutput.pop();
            return item;
        }
    }

    bool TerminalState::TerminalOutputAvailable()
    {
        return !mQueuedTerminalOutput.empty();
    }


    int TerminalState::Input(const std::vector<uint8_t>& aVector)
    {
        /*
         * We want to provide terminal scrollback and the ability to directly modify
         * the terminal 'screen' / buffer.
         *
         * The terminal cursor will be a coordinate (0,0) representing the top left
         * of the terminal to (x,y) representing the bottom right. The maximum x and
         * y will be the total columns and rows viewable without scrolling the GUI
         * control. This will be calculated each frame, so if the GUI is resized then
         * the next frame will have a different terminal size.
         *
         * The terminal row and column when reported will be (1,1) based.
         *
         * When the screen is empty at the start, each new line will increment the the
         * terminal cursor row (y) position. When the terminal cursor is at the last
         * (highest numbered, bottom most) row visible, the terminal cursor row will
         * maintain that value. New lines will now cause row 0 (at the top) to no
         * longer be a part of the terminal buffer. It will still be visible to the GUI
         * and the user can scroll up to see it. However, terminal escape sequences
         * will not be able to modify that text any longer.
         *
         * If terminal escape sequences want to modify row Y before Y lines have been
         * inputted by the source, new empty lines will be added to accommodate. If
         * terminal escape sequences want to modify column X and column X on that row
         * doesn't exist, spaces will generated in the backing data structure to allow
         * for it.
         *
        */

        // TODO: Figure out what to do with mReadOnly from TerminalView
        //assert(!mReadOnly);
        assert(!mTerminalData.IsReadOnly());

        // Get a reference for easy... uhm, reference, to the data.
        int& termRowI = getRowIndex();
        int& termColI = getColumnIndex();

        //auto height = ImGui::GetWindowHeight();
        //auto width = ImGui::GetWindowWidth();

        //int termRowMaxI = std::max((int)ceil(mLastRenderGeometry.mContentRegionAvail.y / mCharAdvance.y) - 1, 0);
        //int termColMaxI = std::max((int)ceil((mLastRenderGeometry.mContentRegionAvail.x - mLastRenderGeometry.mTextScreenPos.x) / mCharAdvance.x) - 1, 0);
        //mTermState.SetBounds(Coordinates(termRowMaxI, termColMaxI));

        int& termRowMaxI = mBounds.mLine;
        int& termColMaxI = mBounds.mColumn;

        size_t mLinesI = 0;
        Lines& mLines = mTerminalData.mLines;

        const uint8_t* aValue = aVector.data();
        size_t dataLength = aVector.size();

        int totalLines = 0;
        while (dataLength-- > 0)
        {
            assert(!mLines.empty());

            if (termRowI > termRowMaxI) {
                termRowI = termRowMaxI;
            }

            // Terminal row is greater than the total number lines we have. This can
            // occur at the start of running.
            while (termRowI >= mLines.size()) {
                mLines.push_back(Line());
            }

            // Convert termRowI to mLinesI
            if ((mLines.size() - 1) < termRowMaxI) {
                mLinesI = termRowI;
            }
            else {
                // When the terminal is simply adding lines, termRowI will be at the bottom
                // so it will equal termRowMaxI, thus mLinesI will equal (mLines.size()-1) 
                mLinesI = (mLines.size() - 1) - (termRowMaxI - termRowI);
            }

            if (*aValue == 0) {
                ++aValue;
            }
            else if (*aValue == '\a')
            {
                // beep is blocking, so run it in the background, only if it is not already running.
                static std::future<void> beep_result;
                if (!beep_result.valid() || beep_result.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    beep_result = std::async(std::launch::async, []() {
                        beep(1200, 150);
                        });
                }
                ++aValue;

            }
            else if (*aValue == '\r')
            {
                if (mNewLineMode == NewLineMode::AddLfToCr) {
                    mTerminalData.InsertLine(++mLinesI);
                }
                termColI = 0;
                ++aValue;
            }
            else if (*aValue == '\n')
            {

                mTerminalData.InsertLine(++mLinesI);
                if (mNewLineMode == NewLineMode::AddCrToLf) {
                    termColI = 0;
                }
                ++termRowI;
                ++totalLines;
                ++aValue;
            }
            else
            {

                auto& line = mLines[mLinesI];
                auto d = TerminalData::UTF8CharLength(*aValue);
                auto pi = GetPaletteIndex();
                if (d > 1) {
                    while (d-- > 0 && *aValue != '\0') {
                        mTerminalData.InputGlyph(line, termColI, pi, *aValue++);
                    }
                }
                else {

                    const auto escSeq = mAnsiEscSeqParser.Parse(*aValue);

                    if (escSeq.mOutputChar) {
                        mTerminalData.InputGlyph(line, termColI, pi, escSeq.mOutputChar);
                    }

                    // Update the terminal state, which includes coloring,
                    // clearing, and positioning the cursor. It may also cause 
                    // serial output to be produced which mTermState will queue up
                    Update(escSeq);

                    aValue++;
                }

            }

            mTerminalData.SetTextChanged(true);
        }

        return totalLines;
    }

    PaletteIndex TerminalState::GetPaletteIndex()
    {
        using enum TerminalGraphicsState::Flags;

        int icolor = 0;

        if (IsInverse()) {
            icolor = (int)getBackgroundColor();
        }
        else {
            icolor = (int)getForegroundColor();
        }

        PaletteIndex pal = PaletteIndex::Default;

        if (icolor & (int)BlackFg) {
            pal = PaletteIndex::Black;
        }
        else if (icolor & (int)RedFg) {
            pal = PaletteIndex::Red;
        }
        else if (icolor & (int)GreenFg) {
            pal = PaletteIndex::Green;
        }
        else if (icolor & (int)YellowFg) {
            pal = PaletteIndex::Yellow;
        }
        else if (icolor & (int)BlueFg) {
            pal = PaletteIndex::Blue;
        }
        else if (icolor & (int)MagentaFg) {
            pal = PaletteIndex::Magenta;
        }
        else if (icolor & (int)CyanFg) {
            pal = PaletteIndex::Cyan;
        }
        else if (icolor & (int)WhiteFg) {
            pal = PaletteIndex::White;
        }

        return pal;
    }

}