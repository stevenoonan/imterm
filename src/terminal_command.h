#pragma once

#include <optional>
#include <variant>
#include <vector>

#include "escape_sequence_parser.h"
#include "terminal_coordinates.h"

namespace imterm {

	enum class GraphicsCommand {
		Reset = 0,
		Bold = 1,
		Dim = 2,
		Italic = 3,
		Underline = 4,
		Blinking = 5,
		Inverse = 7,
		Hidden = 8,
		Strikethrough = 9,
		BoldOrDimReset = 22,
		ItalicReset = 23,
		UnderlineReset = 24,
		BlinkingReset = 25,
		InverseReset = 27,
		HiddenReset = 28,
		StrikethroughReset = 29,
		BlackFg = 30,
		RedFg = 31,
		GreenFg = 32,
		YellowFg = 33,
		BlueFg = 34,
		MagentaFg = 35,
		CyanFg = 36,
		WhiteFg = 37,
		DefaultFg = 39,
		BlackBg = 40,
		RedBg = 41,
		GreenBg = 42,
		YellowBg = 43,
		BlueBg = 44,
		MagentaBg = 45,
		CyanBg = 46,
		WhiteBg = 47,
		DefaultBg = 49
	};

	struct MoveCursor {
		enum class Direction { Up, Down, Right, Left };
		Direction mDirection;
		int mAmount = 1;
		bool mMoveToLineStart = false;
	};

	struct SetCursorPosition {
		ScreenPosition mPosition;
	};

	struct SetCursorColumn {
		int mColumn = 0;
	};

	struct SaveCursor { };
	struct RestoreCursor { };

	struct EraseDisplay {
		enum class Area { AfterCursor, BeforeCursor, All, Scrollback };
		Area mArea;
	};

	struct EraseLine {
		enum class Area { AfterCursor, BeforeCursor, All };
		Area mArea;
	};

	struct SetGraphics {
		std::vector<int> mParameters;
	};

	struct RequestStatusReport {
		enum class Kind { DeviceStatus, CursorPosition };
		Kind mKind;
	};

	using TerminalCommand = std::variant<
		MoveCursor,
		SetCursorPosition,
		SetCursorColumn,
		SaveCursor,
		RestoreCursor,
		EraseDisplay,
		EraseLine,
		SetGraphics,
		RequestStatusReport>;

	std::optional<TerminalCommand> DecodeTerminalCommand(
		const EscapeSequenceParser::ParseResult& aSequence);

}
