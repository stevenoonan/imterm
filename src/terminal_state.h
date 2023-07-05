#pragma once

#include <assert.h>
#include <queue>
#include "terminal_data.h"
#include "escape_sequence_parser.h"
#include "coordinates.h"


class TerminalGraphicsState {

public:

	TerminalGraphicsState();
	~TerminalGraphicsState();

	enum class Flags: uint32_t {

		MaskFormat    = 0x0000'00FFUL,
		MaskColor     = 0xFFFF'0000UL,
		MaskFgColor   = 0x00FF'0000UL,
		MaskBgColor   = 0xFF00'0000UL,

		Bold          = 0x0000'0001UL,
		Dim           = 0x0000'0002UL,
		Italic        = 0x0000'0004UL,
		Underline     = 0x0000'0008UL,
		Blinking      = 0x0000'0010UL,
		Inverse       = 0x0000'0020UL,
		Hidden        = 0x0000'0040UL,
		Strikethrough = 0x0000'0080UL,


		/* bits 8 - 15 unused */

		BlackFg       = 0x0001'0000UL,
		RedFg         = 0x0002'0000UL,
		GreenFg       = 0x0004'0000UL,
		YellowFg      = 0x0008'0000UL,
		BlueFg        = 0x0010'0000UL,
		MagentaFg     = 0x0020'0000UL,
		CyanFg        = 0x0040'0000UL,
		WhiteFg       = 0x0080'0000UL,
				      
		BlackBg       = 0x0100'0000UL,
		RedBg         = 0x0200'0000UL,
		GreenBg       = 0x0400'0000UL,
		YellowBg      = 0x0800'0000UL,
		BlueBg        = 0x1000'0000UL,
		MagentaBg     = 0x2000'0000UL,
		CyanBg        = 0x4000'0000UL,
		WhiteBg       = 0x8000'0000UL,
	};

	uint32_t getState() { return mState; }
	Flags getForegroundColor();
	Flags getBackgroundColor();
	Flags getTextFormatting();

	uint32_t Update(EscapeSequenceParser::GraphicsCommands gfxCmd);
	uint32_t Update(const std::vector<int>& aCommandData);

	bool IsBold() const { return mState & (int)Flags::Bold; }
	bool IsDim() const { return mState & (int)Flags::Dim; }
	bool IsItalic() const { return mState & (int)Flags::Italic; }
	bool IsUnderline() const { return mState & (int)Flags::Underline; }
	bool IsBlinking() const { return mState & (int)Flags::Blinking; }
	bool IsInverse() const { return mState & (int)Flags::Inverse; }
	bool IsHidden() const { return mState & (int)Flags::Hidden; }
	bool IsStrikethrough() const { return mState & (int)Flags::Strikethrough; }

private:

	uint32_t mState = 0;
};

class TerminalState {

public:

	TerminalState(TerminalData& aTerminalData);
	~TerminalState();

	void Update(EscapeSequenceParser::ParseResult aParseResult);
	void SetBounds(Coordinates aBounds);

	const Coordinates& GetBounds()
	{
		return mBounds;
	}

	int& getColumnIndex() {
		return mCursorPos.mColumn;
	}

	int& getRowIndex() {
		return mCursorPos.mLine;
	}

	Coordinates& getPosition() {
		return mCursorPos;
	}

	Coordinates getPositionRelative(size_t totalLines) {
		return getPositionRelative(totalLines, mCursorPos);
	}

	Coordinates getPositionRelative(size_t totalLines, Coordinates position) {
		if ((totalLines - 1) >= mBounds.mLine) {
			return Coordinates((totalLines - 1) - (mBounds.mLine - position.mLine), position.mColumn);
		}
		return position;
	}

	TerminalGraphicsState::Flags const getForegroundColor() {
		return mGraphics.getForegroundColor();
	}

	TerminalGraphicsState::Flags const getBackgroundColor() {
		return mGraphics.getBackgroundColor();
	}

	bool IsBold() const { return mGraphics.IsBold(); }
	bool IsDim() const { return mGraphics.IsDim(); }
	bool IsItalic() const { return mGraphics.IsItalic(); }
	bool IsUnderline() const { return mGraphics.IsUnderline(); }
	bool IsBlinking() const { return mGraphics.IsBlinking(); }
	bool IsInverse() const { return mGraphics.IsInverse(); }
	bool IsHidden() const { return mGraphics.IsHidden(); }
	bool IsStrikethrough() const { return mGraphics.IsStrikethrough(); }

	std::vector<uint8_t> GetTerminalOutput();
	bool TerminalOutputAvailable();

private:

	void SanitzeCursorPosition();


	Coordinates mBounds;
	Coordinates mCursorPos;

	Coordinates mSavedCursorPos;
	TerminalGraphicsState mGraphics;
	TerminalData& mTerminalData;

	std::queue<std::vector<uint8_t>> mQueuedTerminalOutput;

};