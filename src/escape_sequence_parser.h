#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

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
		BadData,
		SequenceTooLong,
		ArgumentTooLong,
		TooManyArguments,
		NumericOverflow
	};

	/**
	 * @brief The raw 8-bit identifier of an escape sequence. The lexical parser
	 * preserves this and the raw arguments; DecodeTerminalCommand converts that
	 * representation into a typed command before terminal state is changed.
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
		Mode mMode;
		std::vector<int> mCommandData;
	};

	EscapeSequenceParser();

	static constexpr std::size_t MaxSequenceLength = 128;
	static constexpr std::size_t MaxArgumentDigits = 6;
	static constexpr std::size_t MaxArguments = 16;
	static constexpr int MaxNumericValue = 65535;

	const ParseResult& Parse(uint8_t input);


private:

	static constexpr uint8_t ESC = 0x1B;
	static constexpr uint8_t CSI = '[';

	Stage mStage;
	Error mError;
	Mode mMode;

	EscapeIdentifier mIdentifier;
	int mDataElementInProcess = 0;
	std::size_t mDataElementDigits = 0;
	bool mSawDataSeparator = false;
	std::vector<int> mDataStaged;
	std::size_t mSequenceLength = 0;

	ParseResult mParseResult;

	bool StageDataElement(bool aIsFinalElement = false);
	void ResetForNextByte();
	void Fail(Error error);
};
