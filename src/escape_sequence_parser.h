#pragma once

#include <algorithm>
#include <string>
#include <stdexcept>
#include <vector>
#include <string>
#include <map>
#include <cstdint>

#include "coordinates.h"

/*
 * All of the sequences we care about follow this pattern:
 *   1. ESC      : 0x1B (27)
 *   2. [        : Control Sequence Introducer (CSI)
 *   3. {0-9,;}  : Data for the control sequence. Digits sometimes delimited
 *                  by semicolons.
 *   4. {A-Z,a-z}: A character of the alphabet. The "identifier" character.
 *
 * The actual data allowed in (3) depends on (4). We are defining (4) in a 
 * liberal way here. Only a subset of all A-Z, a-z characters actually do
 * anything.
 *
 * The length of (3) continues until matching criteria for (4) is found.
 * After (4) is found, the data will be parsed to determine if it truly is
 * a valid sequence. If not, the data
 *
 * Note that there are other, non ESC CSI based sequences. We are not
 * dealing with them (at least not yet!)
 * 
 * A good resource: https://gist.github.com/fnky/458719343aabd01cfb17a3a4f7296797
 *
 */

class EscapeSequenceParser
{
public:

	enum class Stage {
		Inactive,
		GetEsc,
		GetCsi,
		GetMode,
		GetData
	};

	enum class Error {
		None,
		NotReady,
		BadEsc,
		BadCsi,
		BadData
	};

	/**
	 * @brief The raw 8-bit identifier of an escape sequence. Using this along
	 * with the 'arguments' of the incoming data stream will result in a 
	 * CommandType. In many cases, but not all, there is a 1 to 1 correlation
	 * between EscapeIdentifier and CommandType. EscapeIdentifier is only used
	 * for the parsing of data, thereafter CommandType is used.
	*/
	enum class EscapeIdentifier : uint8_t {
		Undefined = 0,
		A_MoveCursorUp = 'A',
		B_MoveCursorDown = 'B',
		C_MoveCursorRight = 'C',
		D_MoveCursorLeft = 'D',
		E_MoveCursorDownBeginning = 'E',
		F_MoveCursorUpBeginning = 'F',
		f_MoveCursor = 'f',
		G_MoveCursorCol = 'G',
		H_MoveCursor = 'H',
		h_Mode = 'h', // ESC[=...h (Screen) or ESC[?...h (Private)
		J_EraseDisplay = 'J',
		K_EraseLine = 'K',
		l_Mode = 'l', // ESC[=...l (Screen) or ESC[?...l (Private)
		m_SetGraphics = 'm',
		n_RequestReport = 'n',
		s_SaveCursorPosition = 's',
		u_RestoreCursorPosition = 'u',
	};


	/**
	 * @brief An incoming command to change the state and/or request data from
	 * the terminal.
	*/
	enum class CommandType {
		None,
		MoveCursorToHome,		  // ESC[H                    moves cursor to home position(0, 0)
		MoveCursorAbs,            // ESC[{line}; {column}H    moves cursor to line #, column #
		                          // ESC[{line}; {column}f    moves cursor to line #, column #
		MoveCursorUp,			  // ESC[#A                   moves cursor up # lines
		MoveCursorDown,           // ESC[#B                   moves cursor down # lines
		MoveCursorRight,          // ESC[#C                   moves cursor right # columns
		MoveCursorLeft,           // ESC[#D                   moves cursor left # columns
		MoveCursorDownBeginning,  // ESC[#E                   moves cursor to beginning of next line, # lines down
		MoveCursorUpBeginning,    // ESC[#F                   moves cursor to beginning of previous line, # lines up
		MoveCursorCol,            // ESC[#G                   moves cursor to column #
		DeviceStatusReport, 	  // ESC[5n                   request cursor position(reports as ESC[#; #R)
		CursorPositionReport, 	  // ESC[6n                   request cursor position(reports as ESC[#; #R)
		SaveCursorPosition,       // ESC[s                    save cursor position(SCO)
		RestoreCursorPosition,    // ESC[u                    restores the cursor to the last saved position(SCO)
		EraseDisplayAfterCursor,  // ESC[J, ESC[0J            erase from cursor until end of screen
		EraseDisplayBeforeCursor, // ESC[1J                   erase from cursor to beginning of screen
		EraseDisplay,             // ESC[2J                   erase entire screen
		EraseSavedLines,          // ESC[3J                   erase saved lines
		EraseLineAfterCursor,     // ESC[K, ESC[0K            erase from cursor to end of line
		EraseLineBeforeCursor,    // ESC[1K                   erase start of line to the cursor
		EraseLine,                // ESC[2K                   erase the entire line
		SetGraphics,              // ESC[{..};{..}m           Background color, foreground, color, bold, underline, etc.
	};

	
	enum class GraphicsCommands {
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

	enum Mode {
		None,
		Screen,
		Private
	};
	

	struct ParseResult {
		uint8_t mOutputChar;
		Stage mStage;
		Error mError;
		EscapeIdentifier mIdentifier;
		CommandType mCommand;
		Mode mMode;
		std::vector<int> mCommandData;
	};

	EscapeSequenceParser();
	~EscapeSequenceParser();

	const ParseResult& Parse(uint8_t input);


private:

	static constexpr uint8_t ESC = 0x1B;
	static constexpr uint8_t CSI = '[';

	Stage mStage;
	Error mError;
	Mode mMode;

	EscapeIdentifier mIdentifier;
	std::vector<uint8_t> mDataElementInProcess;
	std::vector<int> mDataStaged;

	ParseResult mParseResult;

	void ConvertDataElementInProcessToStagedInt();
};