#include "terminal_data.h"

namespace imterm {

	TerminalData::TerminalData() : mReadOnly(false), mTextChanged(false), mTabSize(4) {

	}

	TerminalData::TerminalData(TerminalLogger* aLogger) : mReadOnly(false), mTextChanged(false), mTabSize(4), mLogger(aLogger) {

		mLogger->RegisterLogClosingWatcher([this] {
			LogUnloggedLine();
			});

	}

	TerminalData::~TerminalData() {
		bool removed = mLogger->DeregisterLogClosingWatcher([this] {
			LogUnloggedLine();
			});

		assert(removed);

		if (!removed) std::cerr << "~TerminalData: DeregisterLogClosingWatcher() failed" << std::endl;
	}

	void TerminalData::SetReadOnly(bool aValue)
	{
		mReadOnly = aValue;
	}

	void TerminalData::RemoveLine(int aStart, int aEnd)
	{
		assert(!mReadOnly);
		assert(aEnd >= aStart);
		assert(mLines.size() > (size_t)(aEnd - aStart));

		mLines.erase(mLines.begin() + aStart, mLines.begin() + aEnd);
		assert(!mLines.empty());

		mTextChanged = true;
	}

	void TerminalData::RemoveLine(int aIndex)
	{
		assert(!mReadOnly);
		assert(mLines.size() > 1);

		mLines.erase(mLines.begin() + aIndex);
		assert(!mLines.empty());

		mTextChanged = true;
	}

	Line& TerminalData::InsertLine(int aIndex)
	{
		assert(!mReadOnly);
		
		LogUnloggedLine();

		auto& result = *mLines.insert(mLines.begin() + aIndex, Line());

		mUnloggedLine = &mLines[aIndex];
		mUnloggedLineNumber = aIndex + 1;

		return result;
	}

	void TerminalData::LogUnloggedLine() {
		if (mUnloggedLine) {
			
			// First invalidate mUnloggedLine before calling mLogger->Log() because it can cause this function be
			// called again
			auto unlogged = mUnloggedLine;
			auto lineNumber = mUnloggedLineNumber;
			
			mUnloggedLine = nullptr;
			mUnloggedLineNumber = 0;

			if (mLogger) {
				mLogger->Log(*unlogged, lineNumber);
			}
			
		}
	}

	void TerminalData::DeleteRange(const Coordinates& aStart, const Coordinates& aEnd)
	{
		assert(aEnd >= aStart);
		assert(!mReadOnly);

		//printf("D(%d.%d)-(%d.%d)\n", aStart.mLine, aStart.mColumn, aEnd.mLine, aEnd.mColumn);

		if (aEnd == aStart)
			return;

		auto start = GetCharacterIndex(aStart);
		auto end = GetCharacterIndex(aEnd);

		if (aStart.mLine == aEnd.mLine)
		{
			auto& line = mLines[aStart.mLine];
			auto n = GetLineMaxColumn(aStart.mLine);
			if (aEnd.mColumn >= n)
				line.erase(line.begin() + start, line.end());
			else
				line.erase(line.begin() + start, line.begin() + end);
		}
		else
		{
			auto& firstLine = mLines[aStart.mLine];
			auto& lastLine = mLines[aEnd.mLine];

			firstLine.erase(firstLine.begin() + start, firstLine.end());
			lastLine.erase(lastLine.begin(), lastLine.begin() + end);

			if (aStart.mLine < aEnd.mLine)
				firstLine.insert(firstLine.end(), lastLine.begin(), lastLine.end());

			if (aStart.mLine < aEnd.mLine)
				RemoveLine(aStart.mLine + 1, aEnd.mLine + 1);
		}

		mTextChanged = true;
	}

	int TerminalData::InsertTextAt(Coordinates& /* inout */ aWhere, const char* aValue)
	{
		assert(!mReadOnly);

		int cindex = GetCharacterIndex(aWhere);
		int totalLines = 0;
		while (*aValue != '\0')
		{
			assert(!mLines.empty());

			if (*aValue == '\r')
			{
				// skip
				++aValue;
			}
			else if (*aValue == '\n')
			{
				if (cindex < (int)mLines[aWhere.mLine].size())
				{
					auto& newLine = InsertLine(aWhere.mLine + 1);
					auto& line = mLines[aWhere.mLine];
					newLine.insert(newLine.begin(), line.begin() + cindex, line.end());
					line.erase(line.begin() + cindex, line.end());
				}
				else
				{
					InsertLine(aWhere.mLine + 1);
				}
				++aWhere.mLine;
				aWhere.mColumn = 0;
				cindex = 0;
				++totalLines;
				++aValue;
			}
			else
			{
				auto& line = mLines[aWhere.mLine];
				auto d = UTF8CharLength(*aValue);
				while (d-- > 0 && *aValue != '\0')
					line.insert(line.begin() + cindex++, Glyph(*aValue++, PaletteIndex::Default));
				++aWhere.mColumn;
			}

			mTextChanged = true;
		}

		return totalLines;
	}

	void TerminalData::SetText(const std::string& aText)
	{
		mLines.clear();
		mLines.emplace_back(Line());
		for (auto chr : aText)
		{
			if (chr == '\r')
			{
				// ignore the carriage return character
			}
			else if (chr == '\n')
				mLines.emplace_back(Line());
			else
			{
				mLines.back().emplace_back(Glyph(chr, PaletteIndex::Default));
			}
		}

		mTextChanged = true;

		// TODO: mScrollToTop
		//mScrollToTop = true;

	}

	void TerminalData::SetTextLines(const std::vector<std::string>& aLines)
	{
		mLines.clear();

		if (aLines.empty())
		{
			mLines.emplace_back(Line());
		}
		else
		{
			mLines.resize(aLines.size());

			for (size_t i = 0; i < aLines.size(); ++i)
			{
				const std::string& aLine = aLines[i];

				mLines[i].reserve(aLine.size());
				for (size_t j = 0; j < aLine.size(); ++j)
					mLines[i].emplace_back(Glyph(aLine[j], PaletteIndex::Default));
			}
		}

		mTextChanged = true;
		// TODO: mScrollToTop
		//mScrollToTop = true;

	}

	std::string TerminalData::GetText(const Coordinates& aStart, const Coordinates& aEnd) const
	{
		std::string result;

		auto lstart = aStart.mLine;
		auto lend = aEnd.mLine;
		auto istart = GetCharacterIndex(aStart);
		auto iend = GetCharacterIndex(aEnd);
		size_t s = 0;

		for (size_t i = lstart; i < lend; i++)
			s += mLines[i].size();

		result.reserve(s + s / 8);

		while (istart < iend || lstart < lend)
		{
			if (lstart >= (int)mLines.size())
				break;

			auto& line = mLines[lstart];
			if (istart < (int)line.size())
			{
				result += line[istart].mChar;
				istart++;
			}
			else
			{
				istart = 0;
				++lstart;
				result += '\n';
			}
		}

		return result;
	}


	std::string TerminalData::GetText() const
	{
		return GetText(Coordinates(), Coordinates((int)mLines.size(), 0));
	}

	std::vector<std::string> TerminalData::GetTextLines() const
	{
		std::vector<std::string> result;

		result.reserve(mLines.size());

		for (auto& line : mLines)
		{
			std::string text;

			text.resize(line.size());

			for (size_t i = 0; i < line.size(); ++i)
				text[i] = line[i].mChar;

			result.emplace_back(std::move(text));
		}

		return result;
	}


	void TerminalData::InputGlyph(Line& aLine, int& aColumnIndex, PaletteIndex aPaletteIndex, uint8_t aValue) {

		// Add spaces until the data structure size matches the current column index 
		while (aLine.size() < aColumnIndex) {
			aLine.push_back(Glyph(' ', PaletteIndex::Default));
		}

		if (aLine.size() == aColumnIndex) {
			// Add a character
			aLine.insert(aLine.begin() + aColumnIndex++, Glyph(aValue, aPaletteIndex));
		}
		else {
			// Replace a character
			aLine[aColumnIndex].mChar = aValue;
			aLine[aColumnIndex].mColorIndex = aPaletteIndex;
			aColumnIndex++;
		}
	}

	int TerminalData::GetCharacterIndex(const Coordinates& aCoordinates) const
	{
		if (aCoordinates.mLine >= mLines.size())
			return -1;
		auto& line = mLines[aCoordinates.mLine];
		int c = 0;
		int i = 0;
		for (; i < line.size() && c < aCoordinates.mColumn;)
		{
			if (line[i].mChar == '\t')
				c = (c / mTabSize) * mTabSize + mTabSize;
			else
				++c;
			i += TerminalData::UTF8CharLength(line[i].mChar);
		}
		return i;
	}

	int TerminalData::GetCharacterColumn(int aLine, int aIndex) const
	{
		if (aLine >= mLines.size())
			return 0;
		auto& line = mLines[aLine];
		int col = 0;
		int i = 0;
		while (i < aIndex && i < (int)line.size())
		{
			auto c = line[i].mChar;
			i += TerminalData::UTF8CharLength(c);
			if (c == '\t')
				col = (col / mTabSize) * mTabSize + mTabSize;
			else
				col++;
		}
		return col;
	}

	int TerminalData::GetLineCharacterCount(int aLine) const
	{
		if (aLine >= mLines.size())
			return 0;
		auto& line = mLines[aLine];
		int c = 0;
		for (unsigned i = 0; i < line.size(); c++)
			i += TerminalData::UTF8CharLength(line[i].mChar);
		return c;
	}

	int TerminalData::GetLineMaxColumn(int aLine) const
	{
		if (aLine >= mLines.size())
			return 0;
		auto& line = mLines[aLine];
		int col = 0;
		for (unsigned i = 0; i < line.size(); )
		{
			auto c = line[i].mChar;
			if (c == '\t')
				col = (col / mTabSize) * mTabSize + mTabSize;
			else
				col++;
			i += TerminalData::UTF8CharLength(c);
		}
		return col;
	}

	void TerminalData::SetTabSize(int aValue)
	{
		mTabSize = std::max(0, std::min(32, aValue));
	}
}