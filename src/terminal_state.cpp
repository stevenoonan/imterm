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
        return Flags(mState & static_cast<uint32_t>(Flags::MaskFgColor));
    }

    TerminalGraphicsState::Flags TerminalGraphicsState::getBackgroundColor()
    {
        return Flags(mState & static_cast<uint32_t>(Flags::MaskBgColor));
    }

    TerminalGraphicsState::Flags TerminalGraphicsState::getTextFormatting()
    {
        return Flags(mState & static_cast<uint32_t>(Flags::MaskFormat));
    }

    uint32_t TerminalGraphicsState::Update(GraphicsCommand gfxCmd)
    {
        using gfx = GraphicsCommand;
        switch (gfxCmd) {
        case gfx::Reset:
            mState = 0;
            break;
        case gfx::Bold: mState |= static_cast<uint32_t>(Flags::Bold); break;
        case gfx::Dim: mState |= static_cast<uint32_t>(Flags::Dim); break;
        case gfx::Italic: mState |= static_cast<uint32_t>(Flags::Italic); break;
        case gfx::Underline: mState |= static_cast<uint32_t>(Flags::Underline); break;
        case gfx::Blinking: mState |= static_cast<uint32_t>(Flags::Blinking); break;
        case gfx::Inverse: mState |= static_cast<uint32_t>(Flags::Inverse); break;
        case gfx::Hidden: mState |= static_cast<uint32_t>(Flags::Hidden); break;
        case gfx::Strikethrough: mState |= static_cast<uint32_t>(Flags::Strikethrough); break;
        case gfx::BoldOrDimReset:
            mState &= ~(static_cast<uint32_t>(Flags::Bold)
                | static_cast<uint32_t>(Flags::Dim));
            break;
        case gfx::ItalicReset: mState &= ~static_cast<uint32_t>(Flags::Italic); break;
        case gfx::UnderlineReset: mState &= ~static_cast<uint32_t>(Flags::Underline); break;
        case gfx::BlinkingReset: mState &= ~static_cast<uint32_t>(Flags::Blinking); break;
        case gfx::InverseReset: mState &= ~static_cast<uint32_t>(Flags::Inverse); break;
        case gfx::HiddenReset: mState &= ~static_cast<uint32_t>(Flags::Hidden); break;
        case gfx::StrikethroughReset:
            mState &= ~static_cast<uint32_t>(Flags::Strikethrough);
            break;
        case gfx::BlackFg:
        case gfx::RedFg:
        case gfx::GreenFg:
        case gfx::YellowFg:
        case gfx::BlueFg:
        case gfx::MagentaFg:
        case gfx::CyanFg:
        case gfx::WhiteFg:
            mState &= ~static_cast<uint32_t>(Flags::MaskFgColor);
            mState |= static_cast<uint32_t>(Flags::BlackFg)
                << (static_cast<int>(gfxCmd) - static_cast<int>(gfx::BlackFg));
            break;
        case gfx::DefaultFg:
            mState &= ~static_cast<uint32_t>(Flags::MaskFgColor);
            break;
        case gfx::BlackBg:
        case gfx::RedBg:
        case gfx::GreenBg:
        case gfx::YellowBg:
        case gfx::BlueBg:
        case gfx::MagentaBg:
        case gfx::CyanBg:
        case gfx::WhiteBg:
            mState &= ~static_cast<uint32_t>(Flags::MaskBgColor);
            mState |= static_cast<uint32_t>(Flags::BlackBg)
                << (static_cast<int>(gfxCmd) - static_cast<int>(gfx::BlackBg));
            break;
        case gfx::DefaultBg:
            mState &= ~static_cast<uint32_t>(Flags::MaskBgColor);
            break;
        default:
            break;
        }

        return mState;
    }

    uint32_t TerminalGraphicsState::Update(const std::vector<int>& aCommandData)
    {
        for (int item : aCommandData) {
            {
                const GraphicsCommand gfxCmd = static_cast<GraphicsCommand>(item);
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
        const EscapeSequenceParser::ParseResult& aSequence)
    {
        const auto command = DecodeTerminalCommand(aSequence);
        return command ? Apply(*command) : CommandResult::Ignored;
    }

    TerminalState::CommandResult TerminalState::Apply(
        const TerminalCommand& aCommand)
    {
        SanitizeCursorPosition();
        return std::visit(
            [this](const auto& command) { return ApplyCommand(command); },
            aCommand);
    }

    void TerminalState::SetViewportSize(int aRows, int aColumns)
    {
        const int previousRows = mViewportSize.mRows;
        mViewportSize = ViewportSize{
            std::max(aRows, 1), std::max(aColumns, 1)};

        const int rowDelta = mViewportSize.mRows - previousRows;
        if (rowDelta > 0) {
            const int lastExistingRow = static_cast<int>(
                mTerminalData->GetLineCount() - 1);
            mCursorPosition.mRow = std::min(
                mCursorPosition.mRow + rowDelta, lastExistingRow);
        }
        SanitizeCursorPosition();
    }

    void TerminalState::SanitizeCursorPosition()
    {
        mCursorPosition.mColumn = std::clamp(
            mCursorPosition.mColumn, 0, mViewportSize.mColumns - 1);
        mCursorPosition.mRow = std::clamp(
            mCursorPosition.mRow, 0, mViewportSize.mRows - 1);
    }

    size_t TerminalState::GetViewportTopBufferRow(size_t aTotalLines) const
    {
        const size_t viewportRows = static_cast<size_t>(mViewportSize.mRows);
        return aTotalLines > viewportRows ? aTotalLines - viewportRows : 0;
    }

    BufferPosition TerminalState::ToBufferPosition(
        ScreenPosition aPosition, size_t aTotalLines) const
    {
        aPosition.mRow = std::clamp(
            aPosition.mRow, 0, mViewportSize.mRows - 1);
        aPosition.mColumn = std::clamp(
            aPosition.mColumn, 0, mViewportSize.mColumns - 1);
        return BufferPosition{
            GetViewportTopBufferRow(aTotalLines)
                + static_cast<size_t>(aPosition.mRow),
            RenderedColumn{aPosition.mColumn}};
    }

    TerminalState::CommandResult TerminalState::ApplyCommand(
        const MoveCursor& aCommand)
    {
        const int amount = std::max(aCommand.mAmount, 1);
        switch (aCommand.mDirection) {
        case MoveCursor::Direction::Up:
            mCursorPosition.mRow = std::max(
                mCursorPosition.mRow - amount, 0);
            break;
        case MoveCursor::Direction::Down:
            mCursorPosition.mRow = amount
                >= (mViewportSize.mRows - 1) - mCursorPosition.mRow
                ? mViewportSize.mRows - 1
                : mCursorPosition.mRow + amount;
            break;
        case MoveCursor::Direction::Right:
            mCursorPosition.mColumn = amount
                >= (mViewportSize.mColumns - 1) - mCursorPosition.mColumn
                ? mViewportSize.mColumns - 1
                : mCursorPosition.mColumn + amount;
            break;
        case MoveCursor::Direction::Left:
            mCursorPosition.mColumn = std::max(
                mCursorPosition.mColumn - amount, 0);
            break;
        }
        if (aCommand.mMoveToLineStart) {
            mCursorPosition.mColumn = 0;
        }
        return CommandResult::Applied;
    }

    TerminalState::CommandResult TerminalState::ApplyCommand(
        const SetCursorPosition& aCommand)
    {
        mCursorPosition = aCommand.mPosition;
        SanitizeCursorPosition();
        return CommandResult::Applied;
    }

    TerminalState::CommandResult TerminalState::ApplyCommand(
        const SetCursorColumn& aCommand)
    {
        mCursorPosition.mColumn = aCommand.mColumn;
        SanitizeCursorPosition();
        return CommandResult::Applied;
    }

    TerminalState::CommandResult TerminalState::ApplyCommand(
        const SaveCursor&)
    {
        mSavedCursorPosition = mCursorPosition;
        return CommandResult::Applied;
    }

    TerminalState::CommandResult TerminalState::ApplyCommand(
        const RestoreCursor&)
    {
        mCursorPosition = mSavedCursorPosition;
        SanitizeCursorPosition();
        return CommandResult::Applied;
    }

    void TerminalState::EraseLineAtCursor(EraseLine::Area aArea)
    {
        const size_t lineCount = mTerminalData->GetLineCount();
        const BufferPosition position = ToBufferPosition(
            mCursorPosition, lineCount);
        if (position.mRow >= lineCount) {
            return;
        }

        switch (aArea) {
        case EraseLine::Area::AfterCursor: {
            const ByteOffset start = mTerminalData->GetByteOffset(position);
            mTerminalData->EraseBytes(
                position.mRow, start.mValue,
                mTerminalData->GetLineSize(position.mRow));
            break;
        }
        case EraseLine::Area::BeforeCursor: {
            mTerminalData->ReplaceLinePrefixWithSpaces(
                position.mRow, position.mColumn);
            break;
        }
        case EraseLine::Area::All:
            mTerminalData->ClearLine(position.mRow);
            break;
        }
    }

    void TerminalState::EraseDisplayAtCursor(EraseDisplay::Area aArea)
    {
        const size_t lineCount = mTerminalData->GetLineCount();
        const size_t viewportTop = GetViewportTopBufferRow(lineCount);
        if (aArea == EraseDisplay::Area::Scrollback) {
            if (viewportTop > 0) {
                mTerminalData->RemoveLine(0, static_cast<int>(viewportTop));
            }
            return;
        }

        const BufferPosition cursor = ToBufferPosition(
            mCursorPosition, lineCount);
        const size_t viewportEnd = std::min(
            lineCount, viewportTop + static_cast<size_t>(mViewportSize.mRows));

        if (aArea == EraseDisplay::Area::All) {
            for (size_t row = viewportTop; row < viewportEnd; ++row) {
                mTerminalData->ClearLine(row);
            }
            return;
        }

        if (cursor.mRow >= lineCount) {
            return;
        }

        if (aArea == EraseDisplay::Area::AfterCursor) {
            EraseLineAtCursor(EraseLine::Area::AfterCursor);
            for (size_t row = cursor.mRow + 1; row < viewportEnd; ++row) {
                mTerminalData->ClearLine(row);
            }
            return;
        }

        for (size_t row = viewportTop; row < cursor.mRow; ++row) {
            mTerminalData->ClearLine(row);
        }
        EraseLineAtCursor(EraseLine::Area::BeforeCursor);
    }

    TerminalState::CommandResult TerminalState::ApplyCommand(
        const EraseDisplay& aCommand)
    {
        EraseDisplayAtCursor(aCommand.mArea);
        return CommandResult::Applied;
    }

    TerminalState::CommandResult TerminalState::ApplyCommand(
        const EraseLine& aCommand)
    {
        EraseLineAtCursor(aCommand.mArea);
        return CommandResult::Applied;
    }

    TerminalState::CommandResult TerminalState::ApplyCommand(
        const SetGraphics& aCommand)
    {
        mGraphics.Update(aCommand.mParameters);
        return CommandResult::Applied;
    }

    TerminalState::CommandResult TerminalState::ApplyCommand(
        const RequestStatusReport& aCommand)
    {
        std::string output;
        if (aCommand.mKind == RequestStatusReport::Kind::DeviceStatus) {
            output = "\x1b[0n";
        }
        else {
            output = "\x1b[" + std::to_string(mCursorPosition.mRow + 1)
                + ";" + std::to_string(mCursorPosition.mColumn + 1) + "R";
        }
        mQueuedTerminalOutput.push(
            std::vector<uint8_t>(output.begin(), output.end()));
        return CommandResult::Applied;
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
        mTerminalData->EnsureLineExists(
            static_cast<size_t>(mCursorPosition.mRow));
        const size_t lineCount = mTerminalData->GetLineCount();

        const size_t lineIndex = ToBufferPosition(
            mCursorPosition, lineCount).mRow;
        mTerminalData->EnsureLineExists(lineIndex);

        mTerminalData->InputGlyph(
            lineIndex, mCursorPosition.mColumn, GetPaletteIndex(), value);
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

        const auto currentLineIndex = [this]() {
            SanitizeCursorPosition();
            mTerminalData->EnsureLineExists(
                static_cast<size_t>(mCursorPosition.mRow));
            const size_t lineCount = mTerminalData->GetLineCount();
            return ToBufferPosition(mCursorPosition, lineCount).mRow;
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
                    if (mCursorPosition.mRow == mViewportSize.mRows - 1) {
                        // At the bottom (end) of the lines, so we need to add
                        mTerminalData->InsertLine(
                            static_cast<int>(currentLineIndex() + 1));
                    }
                    else {
                        // Only advance the terminal row if we are not at the bottom
                        ++mCursorPosition.mRow;
                    }
                }
                mCursorPosition.mColumn = 0;
                ++offset;
            }
            else if (value == '\n')
            {
                if (mCursorPosition.mRow == mViewportSize.mRows - 1) {
                    // At the bottom (end) of the lines, so we need to add
                    mTerminalData->InsertLine(
                        static_cast<int>(currentLineIndex() + 1));
                }
                else {
                    ++mCursorPosition.mRow;
                }
                if (mNewLineMode == NewLineMode::AddCrToLf) {
                    mCursorPosition.mColumn = 0;
                }
                
                ++totalLines;
                ++offset;
            }
            else if (value == '\b')
            {
                if (mCursorPosition.mColumn > 0) {
                    --mCursorPosition.mColumn;
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
                    SanitizeCursorPosition();
                    const size_t lineIndex = currentLineIndex();
                    mTerminalData->InputBytes(
                        lineIndex, mCursorPosition.mColumn, GetPaletteIndex(),
                        bytes.subspan(offset, characterLength));
                    SanitizeCursorPosition();
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

        uint32_t icolor = 0;

        if (IsInverse()) {
            icolor = static_cast<uint32_t>(getBackgroundColor());
        }
        else {
            icolor = static_cast<uint32_t>(getForegroundColor());
        }

        PaletteIndex pal = PaletteIndex::Default;

        if (icolor & (static_cast<uint32_t>(BlackFg) | static_cast<uint32_t>(BlackBg))) {
            pal = PaletteIndex::Black;
        }
        else if (icolor & (static_cast<uint32_t>(RedFg) | static_cast<uint32_t>(RedBg))) {
            pal = PaletteIndex::Red;
        }
        else if (icolor & (static_cast<uint32_t>(GreenFg) | static_cast<uint32_t>(GreenBg))) {
            pal = PaletteIndex::Green;
        }
        else if (icolor & (static_cast<uint32_t>(YellowFg) | static_cast<uint32_t>(YellowBg))) {
            pal = PaletteIndex::Yellow;
        }
        else if (icolor & (static_cast<uint32_t>(BlueFg) | static_cast<uint32_t>(BlueBg))) {
            pal = PaletteIndex::Blue;
        }
        else if (icolor & (static_cast<uint32_t>(MagentaFg) | static_cast<uint32_t>(MagentaBg))) {
            pal = PaletteIndex::Magenta;
        }
        else if (icolor & (static_cast<uint32_t>(CyanFg) | static_cast<uint32_t>(CyanBg))) {
            pal = PaletteIndex::Cyan;
        }
        else if (icolor & (static_cast<uint32_t>(WhiteFg) | static_cast<uint32_t>(WhiteBg))) {
            pal = PaletteIndex::White;
        }

        return pal;
    }

}
