#include <algorithm>
#include <cstddef>
#include <future>
#include <limits>
#include <span>
#include <stdexcept>

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
        // Unsupported SGR values are intentionally ignored. They are expected
        // terminal input and must not terminate an active capture session.

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

    TerminalState::TerminalState(std::shared_ptr<TerminalData> aTerminalData, NewLineMode aNewLineMode) : mTerminalData(aTerminalData), mNewLineMode(aNewLineMode)
    {
    }

    TerminalState::~TerminalState()
    {
    }

    TerminalState::CommandResult TerminalState::Update(
        const EscapeSequenceParser::ParseResult& aSeq)
    {

        // If mOutputChar is set, it is not an escape sequence. Otherwise, only 
        // continue if the sequence has been parsed successfully.
        if ((aSeq.mOutputChar != 0) || (aSeq.mStage != EscapeSequenceParser::Stage::Inactive) || (aSeq.mError != EscapeSequenceParser::Error::None)) {
            return CommandResult::Ignored;
        }

        SanitizeCursorPosition();

        using enum EscapeSequenceParser::CommandType;
        using enum EscapeSequenceParser::EscapeIdentifier;
        EscapeSequenceParser::CommandType type = None;
        bool processed = false;

        Coordinates eraseBegin;
        Coordinates eraseEnd;

        std::vector<uint8_t> output;

        const auto movePositive = [](int current, int amount, int upper) {
            return amount >= upper - current ? upper : current + amount;
        };
        const auto moveNegative = [](int current, int amount) {
            return amount >= current ? 0 : current - amount;
        };

        if (aSeq.mMode == EscapeSequenceParser::Mode::None) {

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
                    mCursorPos.mLine = moveNegative(
                        mCursorPos.mLine, aSeq.mCommandData[0]);
                }
                break;
            case B_MoveCursorDown:
                if (aSeq.mCommandData.size() == 1) {
                    type = MoveCursorDown;
                    mCursorPos.mLine = movePositive(
                        mCursorPos.mLine, aSeq.mCommandData[0], mBounds.mLine);
                }
                break;
            case C_MoveCursorRight:
                if (aSeq.mCommandData.size() == 1) {
                    type = MoveCursorRight;
                    mCursorPos.mColumn = movePositive(
                        mCursorPos.mColumn, aSeq.mCommandData[0], mBounds.mColumn);
                }
                break;
            case D_MoveCursorLeft:
                if (aSeq.mCommandData.size() == 1) {
                    type = MoveCursorLeft;
                    mCursorPos.mColumn = moveNegative(
                        mCursorPos.mColumn, aSeq.mCommandData[0]);
                }
                break;
            case E_MoveCursorDownBeginning:
                if (aSeq.mCommandData.size() == 1) {
                    type = MoveCursorDownBeginning;
                    mCursorPos.mLine = movePositive(
                        mCursorPos.mLine, aSeq.mCommandData[0], mBounds.mLine);
                    mCursorPos.mColumn = 0;
                }
                break;
            case F_MoveCursorUpBeginning:
                if (aSeq.mCommandData.size() == 1) {
                    type = MoveCursorUpBeginning;
                    mCursorPos.mLine = moveNegative(
                        mCursorPos.mLine, aSeq.mCommandData[0]);
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
                break;
            }
        }
        else if (aSeq.mMode == EscapeSequenceParser::Mode::Screen) {
            // Screen modes are not implemented yet. Ignore both supported and
            // unknown identifiers without treating device input as exceptional.
        }
        else if (aSeq.mMode == EscapeSequenceParser::Mode::Private) {
            // Private modes are not implemented yet.
        }

        if ((type >= MoveCursorToHome && type <= MoveCursorCol) || (type == RestoreCursorPosition)) {
            SanitizeCursorPosition();
            processed = true;
        }

        if ((type >= EraseDisplayAfterCursor) && (type <= EraseLine)) {
            EraseRange(eraseBegin, eraseEnd);
            processed = true;
        }

        if (output.size() > 0) {
            mQueuedTerminalOutput.push(std::move(output));
        }

        return processed ? CommandResult::Applied : CommandResult::Ignored;
    }

    void TerminalState::SetBounds(Coordinates aBounds)
    {
        int lineDelta = aBounds.mLine - mBounds.mLine;
        mBounds = aBounds;
        if (lineDelta > 0) {
            const int lastLine = static_cast<int>(mTerminalData->mLines.size() - 1);
            if (lineDelta > lastLine - mCursorPos.mLine) {
                mCursorPos.mLine = lastLine;
            }
            else {
                mCursorPos.mLine += lineDelta;
            }
        }

        SanitizeCursorPosition();
    }

    void TerminalState::SanitizeCursorPosition()
    {
        mCursorPos.mColumn = std::clamp(mCursorPos.mColumn, 0, mBounds.mColumn);
        mCursorPos.mLine = std::clamp(mCursorPos.mLine, 0, mBounds.mLine);
    }

    Coordinates TerminalState::getPositionRelative(
        size_t totalLines, Coordinates position) const
    {
        position.mLine = std::clamp(position.mLine, 0, mBounds.mLine);
        position.mColumn = std::clamp(position.mColumn, 0, mBounds.mColumn);

        if (totalLines == 0) {
            return Coordinates();
        }

        const size_t lastLine = totalLines - 1;
        if (lastLine >= static_cast<size_t>(mBounds.mLine)) {
            const size_t relativeLine = lastLine
                - static_cast<size_t>(mBounds.mLine - position.mLine);
            const int safeLine = relativeLine > static_cast<size_t>(std::numeric_limits<int>::max())
                ? std::numeric_limits<int>::max()
                : static_cast<int>(relativeLine);
            return Coordinates(safeLine, position.mColumn);
        }

        return position;
    }

    void TerminalState::EraseRange(Coordinates begin, Coordinates end)
    {
        SanitizeCursorPosition();
        begin.mLine = std::clamp(begin.mLine, 0, mBounds.mLine);
        begin.mColumn = std::clamp(begin.mColumn, 0, mBounds.mColumn);
        end.mLine = std::clamp(end.mLine, 0, mBounds.mLine);
        end.mColumn = std::clamp(end.mColumn, 0, mBounds.mColumn);

        begin = getPositionRelative(mTerminalData->mLines.size(), begin);
        end = getPositionRelative(mTerminalData->mLines.size(), end);
        if (end < begin) {
            return;
        }

        const size_t lastRequiredLine = static_cast<size_t>(end.mLine);
        while (mTerminalData->mLines.size() <= lastRequiredLine) {
            mTerminalData->InsertLine(
                static_cast<int>(mTerminalData->mLines.size()));
        }

        while (begin < end) {
            Line& line = mTerminalData->mLines[static_cast<size_t>(begin.mLine)];
            const size_t start = std::min(
                static_cast<size_t>(begin.mColumn), line.size());

            if (begin.mLine < end.mLine) {
                line.erase(line.begin() + static_cast<std::ptrdiff_t>(start), line.end());
                ++begin.mLine;
                begin.mColumn = 0;
                continue;
            }

            if (end.mColumn == mBounds.mColumn) {
                line.erase(line.begin() + static_cast<std::ptrdiff_t>(start), line.end());
            }
            else {
                const size_t finish = std::min(
                    static_cast<size_t>(end.mColumn), line.size());
                for (size_t index = start; index < finish; ++index) {
                    line[index].mChar = ' ';
                }
            }
            begin.mColumn = end.mColumn;
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


    void TerminalState::InputPrintableByte(uint8_t value)
    {
        SanitizeCursorPosition();
        Lines& lines = mTerminalData->mLines;
        if (lines.empty()) {
            lines.emplace_back();
        }

        while (static_cast<size_t>(mCursorPos.mLine) >= lines.size()) {
            mTerminalData->InsertLine(static_cast<int>(lines.size()));
        }

        size_t lineIndex = 0;
        if (lines.size() - 1 < static_cast<size_t>(mBounds.mLine)) {
            lineIndex = static_cast<size_t>(mCursorPos.mLine);
        }
        else {
            lineIndex = (lines.size() - 1)
                - static_cast<size_t>(mBounds.mLine - mCursorPos.mLine);
        }

        mTerminalData->InputGlyph(
            lines[lineIndex], mCursorPos.mColumn, GetPaletteIndex(), value);
        SanitizeCursorPosition();
    }

    int TerminalState::Input(std::span<const uint8_t> bytes)
    {
        if (bytes.empty()) {
            return 0;
        }
        if (mTerminalData->IsReadOnly()) {
            return 0;
        }

        std::vector<uint8_t> joinedInput;
        if (!mPendingUtf8.empty()) {
            joinedInput.reserve(mPendingUtf8.size() + bytes.size());
            joinedInput.insert(
                joinedInput.end(), mPendingUtf8.begin(), mPendingUtf8.end());
            joinedInput.insert(joinedInput.end(), bytes.begin(), bytes.end());
            mPendingUtf8.clear();
            bytes = joinedInput;
        }

        Lines& lines = mTerminalData->mLines;
        if (lines.empty()) {
            lines.emplace_back();
        }

        const auto currentLineIndex = [this, &lines]() {
            SanitizeCursorPosition();
            while (static_cast<size_t>(mCursorPos.mLine) >= lines.size()) {
                mTerminalData->InsertLine(static_cast<int>(lines.size()));
            }
            if (lines.size() - 1 < static_cast<size_t>(mBounds.mLine)) {
                return static_cast<size_t>(mCursorPos.mLine);
            }
            return (lines.size() - 1)
                - static_cast<size_t>(mBounds.mLine - mCursorPos.mLine);
        };

        int totalLines = 0;
        size_t offset = 0;
        while (offset < bytes.size()) {
            SanitizeCursorPosition();
            const uint8_t value = bytes[offset];

            if (value == 0) {
                ++offset;
            }
            else if (value == '\a')
            {
                // beep is blocking, so run it in the background, only if it is not already running.
                static std::future<void> beep_result;
                if (!beep_result.valid() || beep_result.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    beep_result = std::async(std::launch::async, []() {
                        beep(1200, 150);
                        });
                }
                ++offset;

            }
            else if (value == '\r')
            {
                if (mNewLineMode == NewLineMode::AddLfToCr) {
                    if (mCursorPos.mLine == mBounds.mLine) {
                        // At the bottom (end) of the lines, so we need to add
                        mTerminalData->InsertLine(
                            static_cast<int>(currentLineIndex() + 1));
                    }
                    else {
                        // Only advance the terminal row if we are not at the bottom
                        ++mCursorPos.mLine;
                    }
                }
                mCursorPos.mColumn = 0;
                ++offset;
            }
            else if (value == '\n')
            {
                if (mCursorPos.mLine == mBounds.mLine) {
                    // At the bottom (end) of the lines, so we need to add
                    mTerminalData->InsertLine(
                        static_cast<int>(currentLineIndex() + 1));
                }
                else {
                    ++mCursorPos.mLine;
                }
                if (mNewLineMode == NewLineMode::AddCrToLf) {
                    mCursorPos.mColumn = 0;
                }
                
                ++totalLines;
                ++offset;
            }
            else if (value == '\b')
            {
                if (mCursorPos.mColumn > 0) {
                    --mCursorPos.mColumn;
                }
                ++offset;
            }
            else
            {
                const size_t characterLength = static_cast<size_t>(
                    TerminalData::UTF8CharLength(value));
                const size_t remaining = bytes.size() - offset;
                if (characterLength > remaining) {
                    mPendingUtf8.assign(
                        bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                        bytes.end());
                    break;
                }

                if (characterLength > 1) {
                    for (size_t index = 0; index < characterLength; ++index) {
                        InputPrintableByte(bytes[offset + index]);
                    }
                    offset += characterLength;
                }
                else {
                    const auto& escSeq = mAnsiEscSeqParser.Parse(value);

                    if (escSeq.mOutputChar) {
                        InputPrintableByte(escSeq.mOutputChar);
                    }

                    // Update the terminal state, which includes coloring,
                    // clearing, and positioning the cursor. It may also cause 
                    // serial output to be produced which mTermState will queue up
                    Update(escSeq);

                    ++offset;
                }
            }

            mTerminalData->SetTextChanged(true);
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
