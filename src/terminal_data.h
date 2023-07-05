#pragma once

#include <string>
#include <vector>


#include "vector_timed.h"
#include "coordinates.h"


class TerminalData
{
public:

	typedef uint8_t Char;

	enum class PaletteIndex
	{
		Default,
		Keyword,
		Number,
		String,
		CharLiteral,
		Punctuation,
		Preprocessor,
		Identifier,
		KnownIdentifier,
		PreprocIdentifier,
		Comment,
		MultiLineComment,
		Background,
		Cursor,
		Selection,
		ErrorMarker,
		Breakpoint,
		LineNumber,
		CurrentLineFill,
		CurrentLineFillInactive,
		CurrentLineEdge,
		Black,
		Red,
		Green,
		Yellow,
		Blue,
		Magenta,
		Cyan,
		White,
		BrightBlack,
		BrightRed,
		BrightGreen,
		BrightYellow,
		BrightBlue,
		BrightMagenta,
		BrightCyan,
		BrightWhite,
		Max
	};

	struct Glyph
	{
		Char mChar;
		PaletteIndex mColorIndex = PaletteIndex::Default;

		bool mPreprocessor : 1;

		Glyph(Char aChar, PaletteIndex aColorIndex) : mChar(aChar), mColorIndex(aColorIndex), mPreprocessor(false) {}
	};


	typedef vector_timed<Glyph> Line;
	typedef std::vector<Line> Lines;

	TerminalData::Lines mLines;

	TerminalData();
	~TerminalData();

	TerminalData::Line& InsertLine(int aIndex);

	void SetReadOnly(bool aValue);
	bool IsReadOnly() const { return mReadOnly; }
	bool IsTextChanged() const { return mTextChanged; }
	void SetTextChanged(bool aValue) { mTextChanged = aValue; }

	void RemoveLine(int aStart, int aEnd);
	void RemoveLine(int aIndex);
	void DeleteRange(const Coordinates& aStart, const Coordinates& aEnd);
	int InsertTextAt(Coordinates& aWhere, const char* aValue);
	void SetText(const std::string& aText);
	void SetTextLines(const std::vector<std::string>& aLines);
	std::vector<std::string> GetTextLines() const;
	std::string GetText(const Coordinates& aStart, const Coordinates& aEnd) const;
	std::string GetText() const;

	void InputGlyph(TerminalData::Line& aLine, int& aColumnIndex, TerminalData::PaletteIndex aPaletteIndex, uint8_t aValue);

	int GetCharacterIndex(const Coordinates& aCoordinates) const;
	int GetCharacterColumn(int aLine, int aIndex) const;
	int GetLineCharacterCount(int aLine) const;
	int GetLineMaxColumn(int aLine) const;

	void SetTabSize(int aValue);
	inline int GetTabSize() const { return mTabSize; }

	//static int UTF8CharLength(Char c);
	// https://en.wikipedia.org/wiki/UTF-8
	// We assume that the char is a standalone character (<128) or a leading byte of an UTF-8 code sequence (non-10xxxxxx code)
	static int UTF8CharLength(Char c)
	{
		if ((c & 0xFE) == 0xFC)
			return 6;
		if ((c & 0xFC) == 0xF8)
			return 5;
		if ((c & 0xF8) == 0xF0)
			return 4;
		else if ((c & 0xF0) == 0xE0)
			return 3;
		else if ((c & 0xE0) == 0xC0)
			return 2;
		return 1;
	}


private:

	bool mReadOnly;
	bool mTextChanged;
	int mTabSize;

};

