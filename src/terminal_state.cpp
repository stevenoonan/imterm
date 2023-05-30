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

TerminalState::TerminalState()
{
}

TerminalState::~TerminalState()
{
}

EscapeSequenceParser::Command TerminalState::Update(EscapeSequenceParser::ParseResult aSeq)
{

    using enum EscapeSequenceParser::CommandType;
    EscapeSequenceParser::CommandType type = None;
    bool processed = false;

    Coordinates eraseBegin;
    Coordinates eraseEnd;

    std::vector<uint8_t> output;

    switch (aSeq.mIdentifier) {
    case 'H':
        [[fallthrough]];
    case 'f':
        if ((aSeq.mIdentifier == 'H') && aSeq.mCommandData.size() == 0) {
            // ECS[H
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
    case 'A':
        if (aSeq.mCommandData.size() == 1) {
            type = MoveCursorUp;
            mCursorPos.mLine -= aSeq.mCommandData[0];
        }
        break;
    case 'B':
        if (aSeq.mCommandData.size() == 1) {
            type = MoveCursorDown;
            mCursorPos.mLine += aSeq.mCommandData[0];
        }
        break;
    case 'C':
        if (aSeq.mCommandData.size() == 1) {
            type = MoveCursorRight;
            mCursorPos.mColumn += aSeq.mCommandData[0];
        }
        break;
    case 'D':
        if (aSeq.mCommandData.size() == 1) {
            type = MoveCursorLeft;
            mCursorPos.mColumn -= aSeq.mCommandData[0];
        }
        break;
    case 'E':
        if (aSeq.mCommandData.size() == 1) {
            type = MoveCursorDownBeginning;
            mCursorPos.mLine += aSeq.mCommandData[0];
            mCursorPos.mColumn = 0;
        }
        break;
    case 'F':
        if (aSeq.mCommandData.size() == 1) {
            type = MoveCursorUpBeginning;
            mCursorPos.mLine -= aSeq.mCommandData[0];
            mCursorPos.mColumn = 0;
        }
        break;
    case 'G':
        if (aSeq.mCommandData.size() == 1) {
            type = MoveCursorCol;
            mCursorPos.mColumn = aSeq.mCommandData[0];
        }
        break;
    case 's':
        if (aSeq.mCommandData.size() == 0) {
            type = SaveCursorPosition;
            mSavedCursorPos = mCursorPos;
            processed = true;
        }
        break;
    case 'u':
        if (aSeq.mCommandData.size() == 0) {
            type = RestoreCursorPosition;
            mCursorPos = mSavedCursorPos;
        }
        break;
    case 'J':

        // None of these are processed here

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
    case 'K':

        // None of these are processed here

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

    case 'm':
        mGraphics.Update(aSeq.mCommandData);
        type = SetGraphics;
        processed = true;
        break;

    case 'n':
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
    }
    
    if ((type >= MoveCursorToHome && type <= MoveCursorCol) || (type == RestoreCursorPosition)) {
        SanitzeCursorPosition();
        processed = true;
    }

    return EscapeSequenceParser::Command(
        processed,
        aSeq.mIdentifier, 
        type, 
        aSeq.mCommandData, 
        eraseBegin,
        eraseEnd,
        output
    );
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
