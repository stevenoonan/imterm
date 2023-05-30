#pragma once

#include <algorithm>
#include <string>
#include <stdexcept>
#include <vector>
#include <string>
#include <map>

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
 * The length of (3) continues until matching critera for (4) is found.
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
		GetData
	};

	enum class Error {
		None,
		NotReady,
		BadEsc,
		BadCsi,
		BadData
	};

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

	class Command {
	public:

		Command(
			bool aProcessed,
			uint8_t aIdentifier, 
			CommandType aType, 
			std::vector<int> aData, 
			Coordinates aEraseBegin, 
			Coordinates aEraseEnd,
			std::vector<uint8_t> aOutput
		) :

			mProcessed(aProcessed),
			mIdentifier(aIdentifier), 
			mType(aType), 
			mData(aData), 
			mEraseBegin(aEraseBegin),
			mEraseEnd(aEraseEnd),
			mOutput(aOutput)
		{}

		//uint8_t getIdentifer() const { return mIdentifier; }
		//CommandType getCommandType() const { return mType; }
		//int getParameterCount() const { return mParameterCount; }

		const uint8_t mIdentifier;
		const CommandType mType;
		const std::vector<int> mData;
		const Coordinates mEraseBegin;
		const Coordinates mEraseEnd;
		const std::vector<uint8_t> mOutput;
		bool mProcessed;
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
	

	struct ParseResult {
		uint8_t mOutputChar;
		Stage mStage;
		Error mError;
		uint8_t mIdentifier;
		CommandType mCommand;
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

	uint8_t mIdentifier;
	std::vector<uint8_t> mDataElementInProcess;
	std::vector<int> mDataStaged;

	ParseResult mParseResult;

	//const std::map<uint8_t, Command> mCommands = []() {
	//	using enum CommandType;
	//	std::map<uint8_t, Command> commands = {
	//		{'H', Command('H', MoveCursorToHome, 0)}
	//	};
	//	return commands;
	//}();

	//const std::map<uint8_t, CommandType> mCommands = []() {
	//	using enum CommandType; 
	//	std::map<uint8_t, CommandType> commands = {
	//		{'H', MoveCursorToHome},
	//		{}
	//	};
	//	
	//	return commands;
	//}();

	void ConvertDataElementInProcessToStagedInt();
};