#pragma once

#include <cstdint>
#include <memory>
#include <limits>
#include <queue>
#include <span>
#include <vector>

#include "terminal_data.h"
#include "terminal_command.h"
#include "escape_sequence_parser.h"
#include "coordinates.h"
#include "terminal_coordinates.h"

namespace imterm {

	class TerminalGraphicsState {

	public:

		TerminalGraphicsState();
		~TerminalGraphicsState();

		enum class Flags : uint32_t {

			MaskFormat = 0x0000'00FFUL,
			MaskColor = 0xFFFF'0000UL,
			MaskFgColor = 0x00FF'0000UL,
			MaskBgColor = 0xFF00'0000UL,

			Bold = 0x0000'0001UL,
			Dim = 0x0000'0002UL,
			Italic = 0x0000'0004UL,
			Underline = 0x0000'0008UL,
			Blinking = 0x0000'0010UL,
			Inverse = 0x0000'0020UL,
			Hidden = 0x0000'0040UL,
			Strikethrough = 0x0000'0080UL,


			/* bits 8 - 15 unused */

			BlackFg = 0x0001'0000UL,
			RedFg = 0x0002'0000UL,
			GreenFg = 0x0004'0000UL,
			YellowFg = 0x0008'0000UL,
			BlueFg = 0x0010'0000UL,
			MagentaFg = 0x0020'0000UL,
			CyanFg = 0x0040'0000UL,
			WhiteFg = 0x0080'0000UL,

			BlackBg = 0x0100'0000UL,
			RedBg = 0x0200'0000UL,
			GreenBg = 0x0400'0000UL,
			YellowBg = 0x0800'0000UL,
			BlueBg = 0x1000'0000UL,
			MagentaBg = 0x2000'0000UL,
			CyanBg = 0x4000'0000UL,
			WhiteBg = 0x8000'0000UL,
		};

		uint32_t getState() const { return mState; }
		Flags getForegroundColor();
		Flags getBackgroundColor();
		Flags getTextFormatting();

		uint32_t Update(GraphicsCommand aCommand);
		uint32_t Update(const std::vector<int>& aCommandData);

		bool IsBold() const { return mState & static_cast<uint32_t>(Flags::Bold); }
		bool IsDim() const { return mState & static_cast<uint32_t>(Flags::Dim); }
		bool IsItalic() const { return mState & static_cast<uint32_t>(Flags::Italic); }
		bool IsUnderline() const { return mState & static_cast<uint32_t>(Flags::Underline); }
		bool IsBlinking() const { return mState & static_cast<uint32_t>(Flags::Blinking); }
		bool IsInverse() const { return mState & static_cast<uint32_t>(Flags::Inverse); }
		bool IsHidden() const { return mState & static_cast<uint32_t>(Flags::Hidden); }
		bool IsStrikethrough() const { return mState & static_cast<uint32_t>(Flags::Strikethrough); }

	private:

		uint32_t mState = 0;
	};

	class TerminalState {

	public:

		enum class NewLineMode {
			Strict,
			AddCrToLf,
			AddLfToCr
		};

		TerminalState(std::shared_ptr<TerminalData> aTerminalData, NewLineMode aNewLineMode);
		~TerminalState();

		enum class CommandResult {
			Ignored,
			Applied
		};

		CommandResult Update(const EscapeSequenceParser::ParseResult& aParseResult);
		CommandResult Apply(const TerminalCommand& aCommand);
		void SetViewportSize(int aRows, int aColumns);

		int Input(std::span<const uint8_t> aBytes);

		ViewportSize GetViewportSize() const { return mViewportSize; }
		int getColumnIndex() const { return mCursorPosition.mColumn; }
		int getRowIndex() const { return mCursorPosition.mRow; }
		Coordinates getPosition() const {
			return Coordinates(
				mCursorPosition.mRow, mCursorPosition.mColumn);
		}
		ScreenPosition GetScreenPosition() const { return mCursorPosition; }

		Coordinates getPositionRelative(size_t totalLines) const {
			const BufferPosition position = ToBufferPosition(
				mCursorPosition, totalLines);
			const int safeRow = position.mRow
				> static_cast<size_t>(std::numeric_limits<int>::max())
				? std::numeric_limits<int>::max()
				: static_cast<int>(position.mRow);
			return Coordinates(
				safeRow, position.mColumn.mValue);
		}

		BufferPosition ToBufferPosition(
			ScreenPosition aPosition, size_t aTotalLines) const;

		TerminalGraphicsState::Flags getForegroundColor() {
			return mGraphics.getForegroundColor();
		}

		TerminalGraphicsState::Flags getBackgroundColor() {
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

		inline NewLineMode GetNewLineMode() const { return mNewLineMode; }
		inline void SetNewLineMode(NewLineMode aValue) { mNewLineMode = aValue; }

		PaletteIndex GetPaletteIndex();

	private:

		void SanitizeCursorPosition();
		CommandResult ApplyCommand(const MoveCursor& aCommand);
		CommandResult ApplyCommand(const SetCursorPosition& aCommand);
		CommandResult ApplyCommand(const SetCursorColumn& aCommand);
		CommandResult ApplyCommand(const SaveCursor& aCommand);
		CommandResult ApplyCommand(const RestoreCursor& aCommand);
		CommandResult ApplyCommand(const EraseDisplay& aCommand);
		CommandResult ApplyCommand(const EraseLine& aCommand);
		CommandResult ApplyCommand(const SetGraphics& aCommand);
		CommandResult ApplyCommand(const RequestStatusReport& aCommand);
		void EraseLineAtCursor(EraseLine::Area aArea);
		void EraseDisplayAtCursor(EraseDisplay::Area aArea);
		size_t GetViewportTopBufferRow(size_t aTotalLines) const;
		void InputPrintableByte(uint8_t value);

		ViewportSize mViewportSize;
		ScreenPosition mCursorPosition;
		ScreenPosition mSavedCursorPosition;
		TerminalGraphicsState mGraphics;
		std::shared_ptr<TerminalData> mTerminalData = nullptr;

		std::queue<std::vector<uint8_t>> mQueuedTerminalOutput;

		NewLineMode mNewLineMode;

		EscapeSequenceParser mAnsiEscSeqParser;
		std::vector<uint8_t> mPendingUtf8;

	};

}
