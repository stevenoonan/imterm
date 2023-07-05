#include <stdexcept>
#include "terminal_state.h"


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

uint32_t TerminalGraphicsState::Update(const std::vector<int> &aCommandData)
{
    for (int item : aCommandData) {
        {
            EscapeSequenceParser::GraphicsCommands gfxCmd = static_cast<EscapeSequenceParser::GraphicsCommands>(item);
            Update(gfxCmd);
        }
    }
    return mState;
}

TerminalState::TerminalState(TerminalData& aTerminalData): mTerminalData(aTerminalData)
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

            } else if (aSeq.mCommandData[0] == 6) {

                std::string out("\x1b[" + std::to_string(mCursorPos.mLine+1) + ";" + std::to_string(mCursorPos.mColumn+1) + "R");
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