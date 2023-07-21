#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <iostream>
#include <fstream>

#include "terminal_types.h"
#include "vector_timed.h"
#include "coordinates.h"
#include "terminal_logger.h"

namespace imterm {

	class TerminalData
	{
	public:

		// TODO: make this private
		Lines mLines = std::vector<Line>{
			Line()
		};


		TerminalData();
		TerminalData(TerminalLogger* mLogger);
		~TerminalData();

		Line& InsertLine(int aIndex);

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

		void InputGlyph(Line& aLine, int& aColumnIndex, PaletteIndex aPaletteIndex, uint8_t aValue);

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

		TerminalLogger* mLogger = nullptr;
		//std::unique_ptr<TerminalLogger> mLogger = nullptr;

		Line* mUnloggedLine = &mLines[0];
		int mUnloggedLineNumber = 1;

		void LogUnloggedLine();

	};

}