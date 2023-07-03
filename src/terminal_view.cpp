#include <algorithm>
#include <chrono>
#include <string>
#include <regex>
#include <cmath>
#include <stdexcept>
#include <future>

#include "terminal_view.h"
#include "escape_sequence_parser.h"
#include "coordinates.h"
#include "beep.h"
#include "terminal_data.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h" // for imGui::GetCurrentWindow()


template<class InputIt1, class InputIt2, class BinaryPredicate>
bool equals(InputIt1 first1, InputIt1 last1,
	InputIt2 first2, InputIt2 last2, BinaryPredicate p)
{
	for (; first1 != last1 && first2 != last2; ++first1, ++first2)
	{
		if (!p(*first1, *first2))
			return false;
	}
	return first1 == last1 && first2 == last2;
}

TerminalView::TerminalView()
	: mLineSpacing(1.0f)
	, mTabSize(4)
	, mOverwrite(false)
	, mReadOnly(false)
	, mWithinRender(false)
	, mScrollToCursor(false)
	, mScrollToTop(false)
	, mTextChanged(false)
	, mColorizerEnabled(true)
	, mTextStart(20.0f)
	, mLeftMargin(10)
	, mCursorPositionChanged(false)
	, mColorRangeMin(0)
	, mColorRangeMax(0)
	, mSelectionMode(SelectionMode::Normal)
	, mLastClick(-1.0f)
	, mHandleKeyboardInputs(true)
	, mHandleMouseInputs(true)
	, mIgnoreImGuiChild(false)
	, mShowWhitespaces(false)
	, mStartTime(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count())
	, mNewLineMode(NewLineMode::Strict)
{
	SetPalette(GetDarkPalette());
	mLines.push_back(TerminalData::Line());
}

TerminalView::~TerminalView()
{
}


void TerminalView::SetPalette(const Palette & aValue)
{
	mPaletteBase = aValue;
}

std::string TerminalView::GetText(const Coordinates & aStart, const Coordinates & aEnd) const
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

Coordinates TerminalView::GetActualCursorCoordinates() const
{
	return SanitizeCoordinates(mState.mCursorPosition);
}

Coordinates TerminalView::SanitizeCoordinates(const Coordinates & aValue) const
{
	auto line = aValue.mLine;
	auto column = aValue.mColumn;
	if (line >= (int)mLines.size())
	{
		if (mLines.empty())
		{
			line = 0;
			column = 0;
		}
		else
		{
			line = (int)mLines.size() - 1;
			column = GetLineMaxColumn(line);
		}
		return Coordinates(line, column);
	}
	else
	{
		column = mLines.empty() ? 0 : std::min(column, GetLineMaxColumn(line));
		return Coordinates(line, column);
	}
}

// https://en.wikipedia.org/wiki/UTF-8
// We assume that the char is a standalone character (<128) or a leading byte of an UTF-8 code sequence (non-10xxxxxx code)
static int UTF8CharLength(TerminalData::Char c)
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

// "Borrowed" from ImGui source
static inline int ImTextCharToUtf8(char* buf, int buf_size, unsigned int c)
{
	if (c < 0x80)
	{
		buf[0] = (char)c;
		return 1;
	}
	if (c < 0x800)
	{
		if (buf_size < 2) return 0;
		buf[0] = (char)(0xc0 + (c >> 6));
		buf[1] = (char)(0x80 + (c & 0x3f));
		return 2;
	}
	if (c >= 0xdc00 && c < 0xe000)
	{
		return 0;
	}
	if (c >= 0xd800 && c < 0xdc00)
	{
		if (buf_size < 4) return 0;
		buf[0] = (char)(0xf0 + (c >> 18));
		buf[1] = (char)(0x80 + ((c >> 12) & 0x3f));
		buf[2] = (char)(0x80 + ((c >> 6) & 0x3f));
		buf[3] = (char)(0x80 + ((c) & 0x3f));
		return 4;
	}
	//else if (c < 0x10000)
	{
		if (buf_size < 3) return 0;
		buf[0] = (char)(0xe0 + (c >> 12));
		buf[1] = (char)(0x80 + ((c >> 6) & 0x3f));
		buf[2] = (char)(0x80 + ((c) & 0x3f));
		return 3;
	}
}

void TerminalView::Advance(Coordinates & aCoordinates) const
{
	if (aCoordinates.mLine < (int)mLines.size())
	{
		auto& line = mLines[aCoordinates.mLine];
		auto cindex = GetCharacterIndex(aCoordinates);

		if (cindex + 1 < (int)line.size())
		{
			auto delta = UTF8CharLength(line[cindex].mChar);
			cindex = std::min(cindex + delta, (int)line.size() - 1);
		}
		else
		{
			++aCoordinates.mLine;
			cindex = 0;
		}
		aCoordinates.mColumn = GetCharacterColumn(aCoordinates.mLine, cindex);
	}
}

void TerminalView::DeleteRange(const Coordinates & aStart, const Coordinates & aEnd)
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

int TerminalView::InsertTextAt(Coordinates& /* inout */ aWhere, const char * aValue)
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
				line.insert(line.begin() + cindex++, TerminalData::Glyph(*aValue++, TerminalData::PaletteIndex::Default));
			++aWhere.mColumn;
		}

		mTextChanged = true;
	}

	return totalLines;
}

void TerminalView::InputGlyph(TerminalData::Line& aLine, int& aColumnIndex, TerminalData::PaletteIndex aPaletteIndex, uint8_t aValue) {

	// Add spaces until the data structure size matches the current column index 
	while (aLine.size() < aColumnIndex) {
		aLine.push_back(TerminalData::Glyph(' ', TerminalData::PaletteIndex::Default));
	}

	if (aLine.size() == aColumnIndex) {
		// Add a character
		aLine.insert(aLine.begin() + aColumnIndex++, TerminalData::Glyph(aValue, aPaletteIndex));
	}
	else {
		// Replace a character
		aLine[aColumnIndex].mChar = aValue;
		aLine[aColumnIndex].mColorIndex = aPaletteIndex;
		aColumnIndex++;
	}
}


int TerminalView::TerminalInput(const std::vector<uint8_t>& aVector)
{
	/*
	 * We want to provide terminal scrollback and the ability to directly modify
	 * the terminal 'screen' / buffer.
	 * 
	 * The terminal cursor will be a coordinate (0,0) representing the top left
	 * of the terminal to (x,y) representing the bottom right. The maximum x and
	 * y will be the total columns and rows viewable without scrolling the GUI 
	 * control. This will be cacluated each frame, so if the GUI is resized then
	 * the next frame will have a different terminal size.
	 * 
	 * The terminal row and column when reported will be (1,1) based.
	 * 
	 * When the screen is empty at the start, each new line will increment the the
	 * terminal cursor row (y) position. When the terminal cursor is at the last
	 * (highest numbered, bottom most) row visable, the terminal cursor row will
	 * maintain that value. New lines will now cause row 0 (at the top) to no
	 * longer be a part of the terminal buffer. It will still be visable to the GUI
	 * and the user can scroll up to see it. However, terminal escape sequences
	 * will not be able to modify that text any longer.
	 * 
	 * If terminal escape sequences want to modify row Y before Y lines have been
	 * inputted by the source, new empty lines will be added to accommodate. If
	 * terminal escape sequences want to modify column X and column X on that row
	 * doesn't exist, spaces will generated in the backing data structure to allow
	 * for it.
	 * 
	*/


	assert(!mReadOnly);

	// Get a reference for easy... uhm, reference, to the data.
	int& termRowI = mTermState.getRowIndex();
	int& termColI = mTermState.getColumnIndex();

	auto height = ImGui::GetWindowHeight();
	auto width = ImGui::GetWindowWidth();

	int termRowMaxI = std::max((int)ceil(mLastRenderGeometry.mContentRegionAvail.y / mCharAdvance.y) - 1, 0);
	int termColMaxI = std::max((int)ceil((mLastRenderGeometry.mContentRegionAvail.x - mLastRenderGeometry.mTextScreenPos.x)/ mCharAdvance.x) - 1, 0);
	mTermState.SetBounds(Coordinates(termRowMaxI, termColMaxI));

	size_t mLinesI = 0;

	const uint8_t* aValue = aVector.data();
	size_t dataLength = aVector.size();

	int totalLines = 0;
	while (dataLength-- > 0)
	{
		assert(!mLines.empty());

		if (termRowI > termRowMaxI) {
			termRowI = termRowMaxI;
		}

		// Terminal row is greater than the total number lines we have. This can
		// occur at the start of running.
		while (termRowI >= mLines.size()) {
			mLines.push_back(TerminalData::Line());
		}

		// Convert termRowI to mLinesI
		if ((mLines.size()-1) < termRowMaxI) {
			mLinesI = termRowI;
		}
		else {
			// When the terminal is simply adding lines, termRowI will be at the bottom
			// so it will equal termRowMaxI, thus mLinesI will equal (mLines.size()-1) 
			mLinesI = (mLines.size()-1) - (termRowMaxI - termRowI);
		}

		if (*aValue == 0) {
			++aValue;
		}
		else if (*aValue == '\a')
		{
			// beep is blocking, so run it in the background, only if it is not already running.
			static std::future<void> beep_result;
			if (!beep_result.valid() || beep_result.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
				beep_result = std::async(std::launch::async, []() {
					beep(1200, 150);
				});
			}
			++aValue;
			
		}
		else if (*aValue == '\r')
		{
			if (mNewLineMode == NewLineMode::AddLfToCr) {
				InsertLine(++mLinesI);
			}
			termColI = 0;
			++aValue;
		}
		else if (*aValue == '\n')
		{
			
			InsertLine(++mLinesI);
			if (mNewLineMode == NewLineMode::AddCrToLf) {
				termColI = 0;
			}
			++termRowI;
			++totalLines;
			++aValue;
		}
		else
		{

			auto& line = mLines[mLinesI];
			auto d = UTF8CharLength(*aValue);
			auto pi = GetPaletteIndex(mTermState);
			if (d > 1) {
				while (d-- > 0 && *aValue != '\0') {
					InputGlyph(line, termColI, pi, *aValue++);
				}
			}
			else {

				const auto escSeq = mAnsiEscSeqParser.Parse(*aValue);

				if (escSeq.mOutputChar) {
					InputGlyph(line, termColI, pi, escSeq.mOutputChar);
				}
				else if ((escSeq.mStage == EscapeSequenceParser::Stage::Inactive) && (escSeq.mError == EscapeSequenceParser::Error::None)) {
					auto cmd = mTermState.Update(escSeq);
					
					// TODO: Use dependency injection when creating TerminalState
					// so that it can use EscapeSequenceParser and EscapseSequenceParser
					// can callback to Edit/Erase line functionality of the TerminalView.

					if (!cmd.mProcessed) {
						using enum EscapeSequenceParser::CommandType;
						if ((cmd.mType >= EraseDisplayAfterCursor) && (cmd.mType <= EraseLine))	{
							Coordinates begin = cmd.mEraseBegin;
							Coordinates end = cmd.mEraseEnd;

							// Begin and end are in localized Terminal coordinates, not global mLines
							// The last line of the terminal is the max mLine
							begin = mTermState.getPositionRelative(mLines.size(), begin);
							end = mTermState.getPositionRelative(mLines.size(), end);

							while (begin < end) {
								auto& eraseLine = mLines[begin.mLine];
								if (begin.mLine < end.mLine) {
									eraseLine.erase(eraseLine.begin() + begin.mColumn, eraseLine.end());
									begin.mLine++;
									begin.mColumn = 0;
								}
								else {
									// begin and end are on the same line
									if (end.mColumn == mTermState.GetBounds().mColumn) {
										// the end of the erase is the end of the line, so we can erase
										eraseLine.erase(eraseLine.begin() + begin.mColumn, eraseLine.end());
									}
									else {
										// end of the erase is in the middle of the last line, replace printables with spaces
										for (auto it = eraseLine.begin() + begin.mColumn; it != eraseLine.begin() + end.mColumn; ++it) {
											(*it).mChar = ' '; // = Glyph(' ', PaletteIndex::Default);
										}
									}
									begin.mColumn = end.mColumn;
								}
							}
						}

						if (!cmd.mOutput.empty()) {
							mQueuedTerminalOutput.push(cmd.mOutput);
						}
					}
					
				}

				aValue++;
			}

		}

		mTextChanged = true;
	}

	return totalLines;
}

Coordinates TerminalView::ScreenPosToCoordinates(const ImVec2& aPosition) const
{
	ImVec2 origin = ImGui::GetCursorScreenPos();
	ImVec2 local(aPosition.x - origin.x, aPosition.y - origin.y);

	int lineNo = std::max(0, (int)floor(local.y / mCharAdvance.y));

	int columnCoord = 0;

	if (lineNo >= 0 && lineNo < (int)mLines.size())
	{
		auto& line = mLines.at(lineNo);

		int columnIndex = 0;
		float columnX = 0.0f;

		while ((size_t)columnIndex < line.size())
		{
			float columnWidth = 0.0f;

			if (line[columnIndex].mChar == '\t')
			{
				float spaceSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, " ").x;
				float oldX = columnX;
				float newColumnX = (1.0f + std::floor((1.0f + columnX) / (float(mTabSize) * spaceSize))) * (float(mTabSize) * spaceSize);
				columnWidth = newColumnX - oldX;
				if (mTextStart + columnX + columnWidth * 0.5f > local.x)
					break;
				columnX = newColumnX;
				columnCoord = (columnCoord / mTabSize) * mTabSize + mTabSize;
				columnIndex++;
			}
			else
			{
				char buf[7];
				auto d = UTF8CharLength(line[columnIndex].mChar);
				int i = 0;
				while (i < 6 && d-- > 0)
					buf[i++] = line[columnIndex++].mChar;
				buf[i] = '\0';
				columnWidth = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, buf).x;
				if (mTextStart + columnX + columnWidth * 0.5f > local.x)
					break;
				columnX += columnWidth;
				columnCoord++;
			}
		}
	}

	return SanitizeCoordinates(Coordinates(lineNo, columnCoord));
}

Coordinates TerminalView::FindWordStart(const Coordinates & aFrom) const
{
	Coordinates at = aFrom;
	if (at.mLine >= (int)mLines.size())
		return at;

	auto& line = mLines[at.mLine];
	auto cindex = GetCharacterIndex(at);

	if (cindex >= (int)line.size())
		return at;

	while (cindex > 0 && isspace(line[cindex].mChar))
		--cindex;

	auto cstart = (TerminalData::PaletteIndex)line[cindex].mColorIndex;
	while (cindex > 0)
	{
		auto c = line[cindex].mChar;
		if ((c & 0xC0) != 0x80)	// not UTF code sequence 10xxxxxx
		{
			if (c <= 32 && isspace(c))
			{
				cindex++;
				break;
			}
			if (cstart != (TerminalData::PaletteIndex)line[size_t(cindex - 1)].mColorIndex)
				break;
		}
		--cindex;
	}
	return Coordinates(at.mLine, GetCharacterColumn(at.mLine, cindex));
}

Coordinates TerminalView::FindWordEnd(const Coordinates & aFrom) const
{
	Coordinates at = aFrom;
	if (at.mLine >= (int)mLines.size())
		return at;

	auto& line = mLines[at.mLine];
	auto cindex = GetCharacterIndex(at);

	if (cindex >= (int)line.size())
		return at;

	bool prevspace = (bool)isspace(line[cindex].mChar);
	auto cstart = (TerminalData::PaletteIndex)line[cindex].mColorIndex;
	while (cindex < (int)line.size())
	{
		auto c = line[cindex].mChar;
		auto d = UTF8CharLength(c);
		if (cstart != (TerminalData::PaletteIndex)line[cindex].mColorIndex)
			break;

		if (prevspace != !!isspace(c))
		{
			if (isspace(c))
				while (cindex < (int)line.size() && isspace(line[cindex].mChar))
					++cindex;
			break;
		}
		cindex += d;
	}
	return Coordinates(aFrom.mLine, GetCharacterColumn(aFrom.mLine, cindex));
}

Coordinates TerminalView::FindNextWord(const Coordinates & aFrom) const
{
	Coordinates at = aFrom;
	if (at.mLine >= (int)mLines.size())
		return at;

	// skip to the next non-word character
	auto cindex = GetCharacterIndex(aFrom);
	bool isword = false;
	bool skip = false;
	if (cindex < (int)mLines[at.mLine].size())
	{
		auto& line = mLines[at.mLine];
		isword = isalnum(line[cindex].mChar);
		skip = isword;
	}

	while (!isword || skip)
	{
		if (at.mLine >= mLines.size())
		{
			auto l = std::max(0, (int) mLines.size() - 1);
			return Coordinates(l, GetLineMaxColumn(l));
		}

		auto& line = mLines[at.mLine];
		if (cindex < (int)line.size())
		{
			isword = isalnum(line[cindex].mChar);

			if (isword && !skip)
				return Coordinates(at.mLine, GetCharacterColumn(at.mLine, cindex));

			if (!isword)
				skip = false;

			cindex++;
		}
		else
		{
			cindex = 0;
			++at.mLine;
			skip = false;
			isword = false;
		}
	}

	return at;
}

int TerminalView::GetCharacterIndex(const Coordinates& aCoordinates) const
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
		i += UTF8CharLength(line[i].mChar);
	}
	return i;
}

int TerminalView::GetCharacterColumn(int aLine, int aIndex) const
{
	if (aLine >= mLines.size())
		return 0;
	auto& line = mLines[aLine];
	int col = 0;
	int i = 0;
	while (i < aIndex && i < (int)line.size())
	{
		auto c = line[i].mChar;
		i += UTF8CharLength(c);
		if (c == '\t')
			col = (col / mTabSize) * mTabSize + mTabSize;
		else
			col++;
	}
	return col;
}

int TerminalView::GetLineCharacterCount(int aLine) const
{
	if (aLine >= mLines.size())
		return 0;
	auto& line = mLines[aLine];
	int c = 0;
	for (unsigned i = 0; i < line.size(); c++)
		i += UTF8CharLength(line[i].mChar);
	return c;
}

int TerminalView::GetLineMaxColumn(int aLine) const
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
		i += UTF8CharLength(c);
	}
	return col;
}

bool TerminalView::IsOnWordBoundary(const Coordinates & aAt) const
{
	if (aAt.mLine >= (int)mLines.size() || aAt.mColumn == 0)
		return true;

	auto& line = mLines[aAt.mLine];
	auto cindex = GetCharacterIndex(aAt);
	if (cindex >= (int)line.size())
		return true;

	if (mColorizerEnabled)
		return line[cindex].mColorIndex != line[size_t(cindex - 1)].mColorIndex;

	return isspace(line[cindex].mChar) != isspace(line[cindex - 1].mChar);
}

void TerminalView::RemoveLine(int aStart, int aEnd)
{
	assert(!mReadOnly);
	assert(aEnd >= aStart);
	assert(mLines.size() > (size_t)(aEnd - aStart));

	mLines.erase(mLines.begin() + aStart, mLines.begin() + aEnd);
	assert(!mLines.empty());

	mTextChanged = true;
}

void TerminalView::RemoveLine(int aIndex)
{
	assert(!mReadOnly);
	assert(mLines.size() > 1);

	mLines.erase(mLines.begin() + aIndex);
	assert(!mLines.empty());

	mTextChanged = true;
}

TerminalData::Line& TerminalView::InsertLine(int aIndex)
{
	assert(!mReadOnly);

	auto& result = *mLines.insert(mLines.begin() + aIndex, TerminalData::Line());

	return result;
}

std::string TerminalView::GetWordUnderCursor() const
{
	auto c = GetCursorPosition();
	return GetWordAt(c);
}

std::string TerminalView::GetWordAt(const Coordinates & aCoords) const
{
	auto start = FindWordStart(aCoords);
	auto end = FindWordEnd(aCoords);

	std::string r;

	auto istart = GetCharacterIndex(start);
	auto iend = GetCharacterIndex(end);

	for (auto it = istart; it < iend; ++it)
		r.push_back(mLines[aCoords.mLine][it].mChar);

	return r;
}

ImU32 TerminalView::GetGlyphColor(const TerminalData::Glyph & aGlyph) const
{
	if (!mColorizerEnabled)
		return mPalette[(int)TerminalData::PaletteIndex::Default];

	auto const color = mPalette[(int)aGlyph.mColorIndex];
	if (aGlyph.mPreprocessor)
	{
		const auto ppcolor = mPalette[(int)TerminalData::PaletteIndex::Preprocessor];
		const int c0 = ((ppcolor & 0xff) + (color & 0xff)) / 2;
		const int c1 = (((ppcolor >> 8) & 0xff) + ((color >> 8) & 0xff)) / 2;
		const int c2 = (((ppcolor >> 16) & 0xff) + ((color >> 16) & 0xff)) / 2;
		const int c3 = (((ppcolor >> 24) & 0xff) + ((color >> 24) & 0xff)) / 2;
		return ImU32(c0 | (c1 << 8) | (c2 << 16) | (c3 << 24));
	}
	return color;
}

void TerminalView::HandleKeyboardInputs()
{
	ImGuiIO& io = ImGui::GetIO();
	auto shift = io.KeyShift;
	auto ctrl = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
	auto alt = io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeyAlt;

	if (ImGui::IsWindowFocused())
	{
		if (ImGui::IsWindowHovered())
			ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
		//ImGui::CaptureKeyboardFromApp(true);

		bool allowKeyboardInput = !IsReadOnly() && IsKeyboardInputAllowed();

		io.WantCaptureKeyboard = true;
		io.WantTextInput = true;

		if (allowKeyboardInput) {

			//if (allowKeyboardInput && ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Z)))
			//	Undo();
			//else if (allowKeyboardInput && !ctrl && !shift && alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Backspace)))
			//	Undo();
			//else if (allowKeyboardInput && ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Y)))
			//	Redo();
			//else 

			if (allowKeyboardInput && !ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete)))
				Delete();
			else if (allowKeyboardInput && !ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Backspace)))
				Backspace();
			else if (allowKeyboardInput && !ctrl && shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Insert)))
				Paste();
			else if (allowKeyboardInput && ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_V)))
				Paste();
			else if (allowKeyboardInput && !ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter)))
				EnterCharacter('\n', false);
			else if (allowKeyboardInput && !ctrl && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Tab)))
				EnterCharacter('\t', shift);

		}
		else {

			bool test = false;
			/*if (exposeKeyboardInput && !ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter))) {
				mInputQueueCharacters.push('\n');
			}*/

			/* handle Delete, Backspace, Enter / Return, Tab */
			if (!ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete)))
				//Delete();
				test = true;
			else if (!ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Backspace)))
				//Backspace();
				//test = true;
				mInputQueueCharacters.push('\x7F');
			else if (!ctrl && shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Insert)))
				Paste();
			else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_V)))
				Paste();
			else if (!ctrl && !shift && !alt && (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter)) || ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_KeypadEnter))))
				//EnterCharacter('\n', false);
				//test = true;
				mInputQueueCharacters.push('\n');
			else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Tab)))
				//EnterCharacter('\t', shift);
				mInputQueueCharacters.push('\t');
				//test = true;

		}

		if (!ctrl && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_UpArrow))) {
			mInputQueueCharacters.push('\x1B'); //MoveUp(1, shift);
			mInputQueueCharacters.push('[');
			mInputQueueCharacters.push('A');
		}
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_DownArrow))) {
			//MoveDown(1, shift);
			mInputQueueCharacters.push('\x1B');
			mInputQueueCharacters.push('[');
			mInputQueueCharacters.push('B');

		} 
		else if (!alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_LeftArrow))) {
			//MoveLeft(1, shift, ctrl);
			mInputQueueCharacters.push('\x1B');
			mInputQueueCharacters.push('[');
			mInputQueueCharacters.push('D');

		} 
		else if (!alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_RightArrow))) {
			//MoveRight(1, shift, ctrl);
			mInputQueueCharacters.push('\x1B');
			mInputQueueCharacters.push('[');
			mInputQueueCharacters.push('C');

		} 
		else if (!alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageUp)))
			MoveUp(GetPageSize() - 4, shift);
		else if (!alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageDown)))
			MoveDown(GetPageSize() - 4, shift);
		else if (!alt && ctrl && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Home)))
			MoveTop(shift);
		else if (ctrl && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_End)))
			MoveBottom(shift);
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Home)))
			MoveHome(shift);
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_End)))
			MoveEnd(shift);
		else if (!ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Insert)))
			mOverwrite ^= true;
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Insert)))
			Copy();
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_C)))
			Copy();
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_X)))
			Cut();
		else if (!ctrl && shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete)))
			Cut();
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_A)))
			SelectAll();

		

		
		
		



		bool exposeKeyboardInput = true;

		if ((allowKeyboardInput || exposeKeyboardInput) && !io.InputQueueCharacters.empty())
		{
			for (int i = 0; i < io.InputQueueCharacters.Size; i++)
			{
				auto c = io.InputQueueCharacters[i];

				if (allowKeyboardInput) {
					if (c != 0 && (c == '\n' || c >= 32))
						EnterCharacter(c, shift);
				}

				if (exposeKeyboardInput) {
					mInputQueueCharacters.push(c);
				}
			}
			io.InputQueueCharacters.resize(0);
		}

	}
}

void TerminalView::HandleMouseInputs()
{
	ImGuiIO& io = ImGui::GetIO();
	auto shift = io.KeyShift;
	auto ctrl = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
	auto alt = io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeyAlt;

	if (ImGui::IsWindowHovered())
	{
		if (!shift && !alt)
		{
			auto click = ImGui::IsMouseClicked(0);
			auto doubleClick = ImGui::IsMouseDoubleClicked(0);
			auto t = ImGui::GetTime();
			auto tripleClick = click && !doubleClick && (mLastClick != -1.0f && (t - mLastClick) < io.MouseDoubleClickTime);

			/*
			Left mouse button triple click
			*/

			if (tripleClick)
			{
				if (!ctrl)
				{
					mState.mCursorPosition = mInteractiveStart = mInteractiveEnd = ScreenPosToCoordinates(ImGui::GetMousePos());
					mSelectionMode = SelectionMode::Line;
					SetSelection(mInteractiveStart, mInteractiveEnd, mSelectionMode);
				}

				mLastClick = -1.0f;
			}

			/*
			Left mouse button double click
			*/

			else if (doubleClick)
			{
				if (!ctrl)
				{
					mState.mCursorPosition = mInteractiveStart = mInteractiveEnd = ScreenPosToCoordinates(ImGui::GetMousePos());
					if (mSelectionMode == SelectionMode::Line)
						mSelectionMode = SelectionMode::Normal;
					else
						mSelectionMode = SelectionMode::Word;
					SetSelection(mInteractiveStart, mInteractiveEnd, mSelectionMode);
				}

				mLastClick = (float)ImGui::GetTime();
			}

			/*
			Left mouse button click
			*/
			else if (click)
			{
				mState.mCursorPosition = mInteractiveStart = mInteractiveEnd = ScreenPosToCoordinates(ImGui::GetMousePos());
				if (ctrl)
					mSelectionMode = SelectionMode::Word;
				else
					mSelectionMode = SelectionMode::Normal;
				SetSelection(mInteractiveStart, mInteractiveEnd, mSelectionMode);

				mLastClick = (float)ImGui::GetTime();
			}
			// Mouse left button dragging (=> update selection)
			else if (ImGui::IsMouseDragging(0) && ImGui::IsMouseDown(0))
			{
				io.WantCaptureMouse = true;
				mState.mCursorPosition = mInteractiveEnd = ScreenPosToCoordinates(ImGui::GetMousePos());
				SetSelection(mInteractiveStart, mInteractiveEnd, mSelectionMode);
			}
		}
	}
}

void TerminalView::Render()
{
	/* Compute mCharAdvance regarding to scaled font size (Ctrl + mouse wheel)*/
	const float fontSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, "#", nullptr, nullptr).x;
	mCharAdvance = ImVec2(fontSize, ImGui::GetTextLineHeightWithSpacing() * mLineSpacing);

	/* Update palette with the current alpha from style */
	for (int i = 0; i < (int)TerminalData::PaletteIndex::Max; ++i)
	{
		auto color = ImGui::ColorConvertU32ToFloat4(mPaletteBase[i]);
		color.w *= ImGui::GetStyle().Alpha;
		mPalette[i] = ImGui::ColorConvertFloat4ToU32(color);
	}

	assert(mLineBuffer.empty());

	auto contentSize = ImGui::GetWindowContentRegionMax();
	auto drawList = ImGui::GetWindowDrawList();
	float longest(mTextStart);

	if (mScrollToTop)
	{
		mScrollToTop = false;
		ImGui::SetScrollY(0.f);
	}

	ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();
	auto scrollX = ImGui::GetScrollX();
	auto scrollY = ImGui::GetScrollY();

	auto lineNo = (int)floor(scrollY / mCharAdvance.y);
	auto globalLineMax = (int)mLines.size();
	auto lineMax = std::max(0, std::min((int)mLines.size() - 1, lineNo + (int)floor((scrollY + contentSize.y) / mCharAdvance.y)));

	RenderGeometry thisRenderGeometry;
	thisRenderGeometry.mCursorScreenPos = cursorScreenPos;
	thisRenderGeometry.mFirstVisibleLineNo = lineNo;
	thisRenderGeometry.mLastVisibleLineNo = lineMax;
	thisRenderGeometry.mScrollX = scrollX;
	thisRenderGeometry.mScrollY = scrollY;
	thisRenderGeometry.mTotalLines = mLines.size();
	thisRenderGeometry.mValid = true;
	thisRenderGeometry.mWindowSize = ImGui::GetWindowSize();
	thisRenderGeometry.mContentRegionAvail = ImGui::GetContentRegionAvail();
	

	// Deduce mTextStart by evaluating mLines size (global lineMax) plus two spaces as text width
	static const int buf_length = 48;
	char buf[buf_length];
	snprintf(buf, 16, " %d [12:12:59]  ", globalLineMax);
	mTextStart = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, buf, nullptr, nullptr).x + mLeftMargin;

	if (!mLines.empty())
	{
		float spaceSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, " ", nullptr, nullptr).x;

		while (lineNo <= lineMax)
		{
			ImVec2 lineStartScreenPos = ImVec2(cursorScreenPos.x, cursorScreenPos.y + lineNo * mCharAdvance.y);
			ImVec2 textScreenPos = ImVec2(lineStartScreenPos.x + mTextStart, lineStartScreenPos.y);
			thisRenderGeometry.mTextScreenPos = textScreenPos;

			auto& line = mLines[lineNo];
			longest = std::max(mTextStart + TextDistanceToLineStart(Coordinates(lineNo, GetLineMaxColumn(lineNo))), longest);
			auto columnNo = 0;
			Coordinates lineStartCoord(lineNo, 0);
			Coordinates lineEndCoord(lineNo, GetLineMaxColumn(lineNo));

			// Draw selection for the current line
			float sstart = -1.0f;
			float ssend = -1.0f;

			assert(mState.mSelectionStart <= mState.mSelectionEnd);
			if (mState.mSelectionStart <= lineEndCoord)
				sstart = mState.mSelectionStart > lineStartCoord ? TextDistanceToLineStart(mState.mSelectionStart) : 0.0f;
			if (mState.mSelectionEnd > lineStartCoord)
				ssend = TextDistanceToLineStart(mState.mSelectionEnd < lineEndCoord ? mState.mSelectionEnd : lineEndCoord);

			if (mState.mSelectionEnd.mLine > lineNo)
				ssend += mCharAdvance.x;

			if (sstart != -1 && ssend != -1 && sstart < ssend)
			{
				ImVec2 vstart(lineStartScreenPos.x + mTextStart + sstart, lineStartScreenPos.y);
				ImVec2 vend(lineStartScreenPos.x + mTextStart + ssend, lineStartScreenPos.y + mCharAdvance.y);
				drawList->AddRectFilled(vstart, vend, mPalette[(int)TerminalData::PaletteIndex::Selection]);
			}

			// Draw breakpoints
			auto start = ImVec2(lineStartScreenPos.x + scrollX, lineStartScreenPos.y);

			if (mBreakpoints.count(lineNo + 1) != 0)
			{
				auto end = ImVec2(lineStartScreenPos.x + contentSize.x + 2.0f * scrollX, lineStartScreenPos.y + mCharAdvance.y);
				drawList->AddRectFilled(start, end, mPalette[(int)TerminalData::PaletteIndex::Breakpoint]);
			}

			// Draw error markers
			auto errorIt = mErrorMarkers.find(lineNo + 1);
			if (errorIt != mErrorMarkers.end())
			{
				auto end = ImVec2(lineStartScreenPos.x + contentSize.x + 2.0f * scrollX, lineStartScreenPos.y + mCharAdvance.y);
				drawList->AddRectFilled(start, end, mPalette[(int)TerminalData::PaletteIndex::ErrorMarker]);

				if (ImGui::IsMouseHoveringRect(lineStartScreenPos, end))
				{
					ImGui::BeginTooltip();
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
					ImGui::Text("Error at line %d:", errorIt->first);
					ImGui::PopStyleColor();
					ImGui::Separator();
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.2f, 1.0f));
					ImGui::Text("%s", errorIt->second.c_str());
					ImGui::PopStyleColor();
					ImGui::EndTooltip();
				}
			}

			// Draw line number (right aligned)
			std::time_t time_t_timestamp = std::chrono::system_clock::to_time_t(line.getTimestamp());
			std::tm* time_tm_timestamp = std::localtime(&time_t_timestamp);
			snprintf(buf, buf_length, "%d  [%02d:%02d:%02d]  ", lineNo + 1, time_tm_timestamp->tm_hour, time_tm_timestamp->tm_min, time_tm_timestamp->tm_sec);

			auto lineNoWidth = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, buf, nullptr, nullptr).x;
			drawList->AddText(ImVec2(lineStartScreenPos.x + mTextStart - lineNoWidth, lineStartScreenPos.y), mPalette[(int)TerminalData::PaletteIndex::LineNumber], buf);

			mState.mCursorPosition = mTermState.getPositionRelative(mLines.size());

			if (mState.mCursorPosition.mLine == lineNo)
			{
				auto focused = ImGui::IsWindowFocused();

				// Highlight the current line (where the cursor is)
				if (!HasSelection())
				{
					auto end = ImVec2(start.x + contentSize.x + scrollX, start.y + mCharAdvance.y);
					drawList->AddRectFilled(start, end, mPalette[(int)(focused ? TerminalData::PaletteIndex::CurrentLineFill : TerminalData::PaletteIndex::CurrentLineFillInactive)]);
					drawList->AddRect(start, end, mPalette[(int)TerminalData::PaletteIndex::CurrentLineEdge], 1.0f);
				}

				// Render the cursor
				if (focused)
				{
					auto timeEnd = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
					auto elapsed = timeEnd - mStartTime;
					if (elapsed > 400)
					{
						float width = 1.0f;
						auto cindex = GetCharacterIndex(mState.mCursorPosition);
						float cx = TextDistanceToLineStart(mState.mCursorPosition);

						if (mOverwrite && cindex < (int)line.size())
						{
							auto c = line[cindex].mChar;
							if (c == '\t')
							{
								auto x = (1.0f + std::floor((1.0f + cx) / (float(mTabSize) * spaceSize))) * (float(mTabSize) * spaceSize);
								width = x - cx;
							}
							else
							{
								char buf2[2];
								buf2[0] = line[cindex].mChar;
								buf2[1] = '\0';
								width = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, buf2).x;
							}
						}
						ImVec2 cstart(textScreenPos.x + cx, lineStartScreenPos.y);
						ImVec2 cend(textScreenPos.x + cx + width, lineStartScreenPos.y + mCharAdvance.y);
						drawList->AddRectFilled(cstart, cend, mPalette[(int)TerminalData::TerminalData::PaletteIndex::Cursor]);
						if (elapsed > 800)
							mStartTime = timeEnd;
					}
				}
			}

			// Render colorized text
			auto prevColor = line.empty() ? mPalette[(int)TerminalData::TerminalData::PaletteIndex::Default] : GetGlyphColor(line[0]);
			ImVec2 bufferOffset;

			for (int i = 0; i < line.size();)
			{
				auto& glyph = line[i];
				auto color = GetGlyphColor(glyph);

				if ((color != prevColor || glyph.mChar == '\t' || glyph.mChar == ' ') && !mLineBuffer.empty())
				{
					const ImVec2 newOffset(textScreenPos.x + bufferOffset.x, textScreenPos.y + bufferOffset.y);
					drawList->AddText(newOffset, prevColor, mLineBuffer.c_str());
					auto textSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, mLineBuffer.c_str(), nullptr, nullptr);
					bufferOffset.x += textSize.x;
					mLineBuffer.clear();
				}
				prevColor = color;

				if (glyph.mChar == '\t')
				{
					auto oldX = bufferOffset.x;
					bufferOffset.x = (1.0f + std::floor((1.0f + bufferOffset.x) / (float(mTabSize) * spaceSize))) * (float(mTabSize) * spaceSize);
					++i;

					if (mShowWhitespaces)
					{
						const auto s = ImGui::GetFontSize();
						const auto x1 = textScreenPos.x + oldX + 1.0f;
						const auto x2 = textScreenPos.x + bufferOffset.x - 1.0f;
						const auto y = textScreenPos.y + bufferOffset.y + s * 0.5f;
						const ImVec2 p1(x1, y);
						const ImVec2 p2(x2, y);
						const ImVec2 p3(x2 - s * 0.2f, y - s * 0.2f);
						const ImVec2 p4(x2 - s * 0.2f, y + s * 0.2f);
						drawList->AddLine(p1, p2, 0x90909090);
						drawList->AddLine(p2, p3, 0x90909090);
						drawList->AddLine(p2, p4, 0x90909090);
					}
				}
				else if (glyph.mChar == ' ')
				{
					if (mShowWhitespaces)
					{
						const auto s = ImGui::GetFontSize();
						const auto x = textScreenPos.x + bufferOffset.x + spaceSize * 0.5f;
						const auto y = textScreenPos.y + bufferOffset.y + s * 0.5f;
						drawList->AddCircleFilled(ImVec2(x, y), 1.5f, 0x80808080, 4);
					}
					bufferOffset.x += spaceSize;
					i++;
				}
				else
				{
					auto l = UTF8CharLength(glyph.mChar);
					while (l-- > 0)
						mLineBuffer.push_back(line[i++].mChar);
				}
				++columnNo;
			}

			if (!mLineBuffer.empty())
			{
				const ImVec2 newOffset(textScreenPos.x + bufferOffset.x, textScreenPos.y + bufferOffset.y);
				drawList->AddText(newOffset, prevColor, mLineBuffer.c_str());
				mLineBuffer.clear();
			}

			++lineNo;
		}

	}


	ImGui::Dummy(ImVec2((longest + 2), mLines.size() * mCharAdvance.y));

	if (mScrollToCursor)
	{
		EnsureCursorVisible();
		ImGui::SetWindowFocus();
		mScrollToCursor = false;
	}

	mLastRenderGeometry = thisRenderGeometry;
}

void TerminalView::Render(const char* aTitle, const ImVec2& aSize, bool aBorder)
{
	mWithinRender = true;
	mTextChanged = false;
	mCursorPositionChanged = false;

	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(mPalette[(int)TerminalData::PaletteIndex::Background]));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	if (!mIgnoreImGuiChild)
		ImGui::BeginChild(aTitle, aSize, aBorder, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNavInputs);

	if (mHandleKeyboardInputs)
	{
		HandleKeyboardInputs();
		ImGui::PushAllowKeyboardFocus(true);
	}

	if (mHandleMouseInputs)
		HandleMouseInputs();

	Render();

	if (mHandleKeyboardInputs)
		ImGui::PopAllowKeyboardFocus();

	if (!mIgnoreImGuiChild)
		ImGui::EndChild();

	ImGui::PopStyleVar();
	ImGui::PopStyleColor();

	mWithinRender = false;
}

void TerminalView::SetText(const std::string & aText)
{
	mLines.clear();
	mLines.emplace_back(TerminalData::Line());
	for (auto chr : aText)
	{
		if (chr == '\r')
		{
			// ignore the carriage return character
		}
		else if (chr == '\n')
			mLines.emplace_back(TerminalData::Line());
		else
		{
			mLines.back().emplace_back(TerminalData::Glyph(chr, TerminalData::PaletteIndex::Default));
		}
	}

	mTextChanged = true;
	mScrollToTop = true;

}

void TerminalView::SetTextLines(const std::vector<std::string> & aLines)
{
	mLines.clear();

	if (aLines.empty())
	{
		mLines.emplace_back(TerminalData::Line());
	}
	else
	{
		mLines.resize(aLines.size());

		for (size_t i = 0; i < aLines.size(); ++i)
		{
			const std::string & aLine = aLines[i];

			mLines[i].reserve(aLine.size());
			for (size_t j = 0; j < aLine.size(); ++j)
				mLines[i].emplace_back(TerminalData::Glyph(aLine[j], TerminalData::PaletteIndex::Default));
		}
	}

	mTextChanged = true;
	mScrollToTop = true;

}

void TerminalView::EnterCharacter(ImWchar aChar, bool aShift)
{
	assert(!mReadOnly);

	if (HasSelection())
	{
		if (aChar == '\t' && mState.mSelectionStart.mLine != mState.mSelectionEnd.mLine)
		{

			auto start = mState.mSelectionStart;
			auto end = mState.mSelectionEnd;
			auto originalEnd = end;

			if (start > end)
				std::swap(start, end);
			start.mColumn = 0;
			//			end.mColumn = end.mLine < mLines.size() ? mLines[end.mLine].size() : 0;
			if (end.mColumn == 0 && end.mLine > 0)
				--end.mLine;
			if (end.mLine >= (int)mLines.size())
				end.mLine = mLines.empty() ? 0 : (int)mLines.size() - 1;
			end.mColumn = GetLineMaxColumn(end.mLine);

			//if (end.mColumn >= GetLineMaxColumn(end.mLine))
			//	end.mColumn = GetLineMaxColumn(end.mLine) - 1;

			bool modified = false;

			for (int i = start.mLine; i <= end.mLine; i++)
			{
				auto& line = mLines[i];
				if (aShift)
				{
					if (!line.empty())
					{
						if (line.front().mChar == '\t')
						{
							line.erase(line.begin());
							modified = true;
						}
						else
						{
							for (int j = 0; j < mTabSize && !line.empty() && line.front().mChar == ' '; j++)
							{
								line.erase(line.begin());
								modified = true;
							}
						}
					}
				}
				else
				{
					line.insert(line.begin(), TerminalData::Glyph('\t', TerminalData::PaletteIndex::Background));
					modified = true;
				}
			}

			if (modified)
			{
				start = Coordinates(start.mLine, GetCharacterColumn(start.mLine, 0));
				Coordinates rangeEnd;
				if (originalEnd.mColumn != 0)
				{
					end = Coordinates(end.mLine, GetLineMaxColumn(end.mLine));
					rangeEnd = end;
				}
				else
				{
					end = Coordinates(originalEnd.mLine, 0);
					rangeEnd = Coordinates(end.mLine - 1, GetLineMaxColumn(end.mLine - 1));
				}

				mState.mSelectionStart = start;
				mState.mSelectionEnd = end;

				mTextChanged = true;

				EnsureCursorVisible();
			}

			return;
		} // c == '\t'
		else
		{
			DeleteSelection();
		}
	} // HasSelection

	auto coord = GetActualCursorCoordinates();

	assert(!mLines.empty());

	if (aChar == '\n')
	{
		InsertLine(coord.mLine + 1);
		auto& line = mLines[coord.mLine];
		auto& newLine = mLines[coord.mLine + 1];

		const size_t whitespaceSize = newLine.size();
		auto cindex = GetCharacterIndex(coord);
		newLine.insert(newLine.end(), line.begin() + cindex, line.end());
		line.erase(line.begin() + cindex, line.begin() + line.size());
		SetCursorPosition(Coordinates(coord.mLine + 1, GetCharacterColumn(coord.mLine + 1, (int)whitespaceSize)));
	}
	else
	{
		char buf[7];
		int e = ImTextCharToUtf8(buf, 7, aChar);
		if (e > 0)
		{
			buf[e] = '\0';
			auto& line = mLines[coord.mLine];
			auto cindex = GetCharacterIndex(coord);

			if (mOverwrite && cindex < (int)line.size())
			{
				auto d = UTF8CharLength(line[cindex].mChar);

				while (d-- > 0 && cindex < (int)line.size())
				{
					line.erase(line.begin() + cindex);
				}
			}

			for (auto p = buf; *p != '\0'; p++, ++cindex)
				line.insert(line.begin() + cindex, TerminalData::Glyph(*p, TerminalData::PaletteIndex::Default));

			SetCursorPosition(Coordinates(coord.mLine, GetCharacterColumn(coord.mLine, cindex)));
		}
		else
			return;
	}

	mTextChanged = true;

	EnsureCursorVisible();
}

void TerminalView::SetKeyboardInputAllowed(bool aValue)
{
	mKeyboardInputAllowed = aValue;
}

void TerminalView::SetReadOnly(bool aValue)
{
	mReadOnly = aValue;
}

void TerminalView::SetColorizerEnable(bool aValue)
{
	mColorizerEnabled = aValue;
}

void TerminalView::SetCursorPosition(const Coordinates & aPosition)
{
	if (mState.mCursorPosition != aPosition)
	{
		mState.mCursorPosition = aPosition;
		mCursorPositionChanged = true;
		EnsureCursorVisible();
	}
}

void TerminalView::SetSelectionStart(const Coordinates & aPosition)
{
	mState.mSelectionStart = SanitizeCoordinates(aPosition);
	if (mState.mSelectionStart > mState.mSelectionEnd)
		std::swap(mState.mSelectionStart, mState.mSelectionEnd);
}

void TerminalView::SetSelectionEnd(const Coordinates & aPosition)
{
	mState.mSelectionEnd = SanitizeCoordinates(aPosition);
	if (mState.mSelectionStart > mState.mSelectionEnd)
		std::swap(mState.mSelectionStart, mState.mSelectionEnd);
}

void TerminalView::SetSelection(const Coordinates & aStart, const Coordinates & aEnd, SelectionMode aMode)
{
	auto oldSelStart = mState.mSelectionStart;
	auto oldSelEnd = mState.mSelectionEnd;

	mState.mSelectionStart = SanitizeCoordinates(aStart);
	mState.mSelectionEnd = SanitizeCoordinates(aEnd);
	if (mState.mSelectionStart > mState.mSelectionEnd)
		std::swap(mState.mSelectionStart, mState.mSelectionEnd);

	switch (aMode)
	{
	case TerminalView::SelectionMode::Normal:
		break;
	case TerminalView::SelectionMode::Word:
	{
		mState.mSelectionStart = FindWordStart(mState.mSelectionStart);
		if (!IsOnWordBoundary(mState.mSelectionEnd))
			mState.mSelectionEnd = FindWordEnd(FindWordStart(mState.mSelectionEnd));
		break;
	}
	case TerminalView::SelectionMode::Line:
	{
		const auto lineNo = mState.mSelectionEnd.mLine;
		const auto lineSize = (size_t)lineNo < mLines.size() ? mLines[lineNo].size() : 0;
		mState.mSelectionStart = Coordinates(mState.mSelectionStart.mLine, 0);
		mState.mSelectionEnd = Coordinates(lineNo, GetLineMaxColumn(lineNo));
		break;
	}
	default:
		break;
	}

	if (mState.mSelectionStart != oldSelStart ||
		mState.mSelectionEnd != oldSelEnd)
		mCursorPositionChanged = true;
}

void TerminalView::SetTabSize(int aValue)
{
	mTabSize = std::max(0, std::min(32, aValue));
}

void TerminalView::InsertText(const std::string & aValue)
{
	InsertText(aValue.c_str());
}

void TerminalView::InsertText(const char * aValue)
{
	if (aValue == nullptr)
		return;

	auto pos = GetActualCursorCoordinates();
	auto start = std::min(pos, mState.mSelectionStart);
	int totalLines = pos.mLine - start.mLine;

	totalLines += InsertTextAt(pos, aValue);

	SetSelection(pos, pos);
	SetCursorPosition(pos);
}

void TerminalView::AppendText(const std::string& aValue)
{
	AppendText(aValue.c_str());
}

void TerminalView::AppendText(const char* aValue)
{
	if (aValue == nullptr)
		return;

	auto line = (int)mLines.size() - 1;
	auto column = GetLineMaxColumn(line);
	Coordinates pos(line, column);

	auto start = std::min(pos, mState.mSelectionStart);
	int totalLines = pos.mLine - start.mLine;

	totalLines += InsertTextAt(pos, aValue);

	//SetSelection(pos, pos);
	//SetCursorPosition(pos);
}

void TerminalView::AppendLine()
{
	mLines.push_back(TerminalData::Line());
}

void TerminalView::SetCursorToEnd()
{
	auto line = (int)mLines.size() - 1;
	auto column = GetLineMaxColumn(line);
	Coordinates pos(line, column);
	if (mState.mCursorPosition != pos)
	{
		mState.mCursorPosition = pos;
		mCursorPositionChanged = true;
		EnsureCursorVisible();
	}
}


void TerminalView::DeleteSelection()
{
	assert(mState.mSelectionEnd >= mState.mSelectionStart);

	if (mState.mSelectionEnd == mState.mSelectionStart)
		return;

	DeleteRange(mState.mSelectionStart, mState.mSelectionEnd);

	SetSelection(mState.mSelectionStart, mState.mSelectionStart);
	SetCursorPosition(mState.mSelectionStart);
}

void TerminalView::MoveUp(int aAmount, bool aSelect)
{
	auto oldPos = mState.mCursorPosition;
	mState.mCursorPosition.mLine = std::max(0, mState.mCursorPosition.mLine - aAmount);
	if (oldPos != mState.mCursorPosition)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveStart)
				mInteractiveStart = mState.mCursorPosition;
			else if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mState.mCursorPosition;
			else
			{
				mInteractiveStart = mState.mCursorPosition;
				mInteractiveEnd = oldPos;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);

		EnsureCursorVisible();
	}
}

void TerminalView::MoveDown(int aAmount, bool aSelect)
{
	assert(mState.mCursorPosition.mColumn >= 0);
	auto oldPos = mState.mCursorPosition;
	mState.mCursorPosition.mLine = std::max(0, std::min((int)mLines.size() - 1, mState.mCursorPosition.mLine + aAmount));

	if (mState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mState.mCursorPosition;
			else if (oldPos == mInteractiveStart)
				mInteractiveStart = mState.mCursorPosition;
			else
			{
				mInteractiveStart = oldPos;
				mInteractiveEnd = mState.mCursorPosition;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);

		EnsureCursorVisible();
	}
}

static bool IsUTFSequence(char c)
{
	return (c & 0xC0) == 0x80;
}

void TerminalView::MoveLeft(int aAmount, bool aSelect, bool aWordMode)
{
	if (mLines.empty())
		return;

	auto oldPos = mState.mCursorPosition;
	mState.mCursorPosition = GetActualCursorCoordinates();
	auto line = mState.mCursorPosition.mLine;
	auto cindex = GetCharacterIndex(mState.mCursorPosition);

	while (aAmount-- > 0)
	{
		if (cindex == 0)
		{
			if (line > 0)
			{
				--line;
				if ((int)mLines.size() > line)
					cindex = (int)mLines[line].size();
				else
					cindex = 0;
			}
		}
		else
		{
			--cindex;
			if (cindex > 0)
			{
				if ((int)mLines.size() > line)
				{
					while (cindex > 0 && IsUTFSequence(mLines[line][cindex].mChar))
						--cindex;
				}
			}
		}

		mState.mCursorPosition = Coordinates(line, GetCharacterColumn(line, cindex));
		if (aWordMode)
		{
			mState.mCursorPosition = FindWordStart(mState.mCursorPosition);
			cindex = GetCharacterIndex(mState.mCursorPosition);
		}
	}

	mState.mCursorPosition = Coordinates(line, GetCharacterColumn(line, cindex));

	assert(mState.mCursorPosition.mColumn >= 0);
	if (aSelect)
	{
		if (oldPos == mInteractiveStart)
			mInteractiveStart = mState.mCursorPosition;
		else if (oldPos == mInteractiveEnd)
			mInteractiveEnd = mState.mCursorPosition;
		else
		{
			mInteractiveStart = mState.mCursorPosition;
			mInteractiveEnd = oldPos;
		}
	}
	else
		mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
	SetSelection(mInteractiveStart, mInteractiveEnd, aSelect && aWordMode ? SelectionMode::Word : SelectionMode::Normal);

	EnsureCursorVisible();
}

void TerminalView::MoveRight(int aAmount, bool aSelect, bool aWordMode)
{
	auto oldPos = mState.mCursorPosition;

	if (mLines.empty() || oldPos.mLine >= mLines.size())
		return;

	auto cindex = GetCharacterIndex(mState.mCursorPosition);
	while (aAmount-- > 0)
	{
		auto lindex = mState.mCursorPosition.mLine;
		auto& line = mLines[lindex];

		if (cindex >= line.size())
		{
			if (mState.mCursorPosition.mLine < mLines.size() - 1)
			{
				mState.mCursorPosition.mLine = std::max(0, std::min((int)mLines.size() - 1, mState.mCursorPosition.mLine + 1));
				mState.mCursorPosition.mColumn = 0;
			}
			else
				return;
		}
		else
		{
			cindex += UTF8CharLength(line[cindex].mChar);
			mState.mCursorPosition = Coordinates(lindex, GetCharacterColumn(lindex, cindex));
			if (aWordMode)
				mState.mCursorPosition = FindNextWord(mState.mCursorPosition);
		}
	}

	if (aSelect)
	{
		if (oldPos == mInteractiveEnd)
			mInteractiveEnd = SanitizeCoordinates(mState.mCursorPosition);
		else if (oldPos == mInteractiveStart)
			mInteractiveStart = mState.mCursorPosition;
		else
		{
			mInteractiveStart = oldPos;
			mInteractiveEnd = mState.mCursorPosition;
		}
	}
	else
		mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
	SetSelection(mInteractiveStart, mInteractiveEnd, aSelect && aWordMode ? SelectionMode::Word : SelectionMode::Normal);

	EnsureCursorVisible();
}

void TerminalView::MoveTop(bool aSelect)
{
	auto oldPos = mState.mCursorPosition;
	SetCursorPosition(Coordinates(0, 0));

	if (mState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			mInteractiveEnd = oldPos;
			mInteractiveStart = mState.mCursorPosition;
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);
	}
}

void TerminalView::TerminalView::MoveBottom(bool aSelect)
{
	auto oldPos = GetCursorPosition();
	auto newPos = Coordinates((int)mLines.size() - 1, 0);
	SetCursorPosition(newPos);
	if (aSelect)
	{
		mInteractiveStart = oldPos;
		mInteractiveEnd = newPos;
	}
	else
		mInteractiveStart = mInteractiveEnd = newPos;
	SetSelection(mInteractiveStart, mInteractiveEnd);
}

void TerminalView::MoveHome(bool aSelect)
{
	auto oldPos = mState.mCursorPosition;
	SetCursorPosition(Coordinates(mState.mCursorPosition.mLine, 0));

	if (mState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveStart)
				mInteractiveStart = mState.mCursorPosition;
			else if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mState.mCursorPosition;
			else
			{
				mInteractiveStart = mState.mCursorPosition;
				mInteractiveEnd = oldPos;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);
	}
}

void TerminalView::MoveEnd(bool aSelect)
{
	auto oldPos = mState.mCursorPosition;
	SetCursorPosition(Coordinates(mState.mCursorPosition.mLine, GetLineMaxColumn(oldPos.mLine)));

	if (mState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mState.mCursorPosition;
			else if (oldPos == mInteractiveStart)
				mInteractiveStart = mState.mCursorPosition;
			else
			{
				mInteractiveStart = oldPos;
				mInteractiveEnd = mState.mCursorPosition;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);
	}
}

void TerminalView::Delete()
{
	assert(!mReadOnly);

	if (mLines.empty())
		return;


	if (HasSelection())
	{
		DeleteSelection();
	}
	else
	{
		auto pos = GetActualCursorCoordinates();
		SetCursorPosition(pos);
		auto& line = mLines[pos.mLine];

		if (pos.mColumn == GetLineMaxColumn(pos.mLine))
		{
			if (pos.mLine == (int)mLines.size() - 1)
				return;

			auto& nextLine = mLines[pos.mLine + 1];
			line.insert(line.end(), nextLine.begin(), nextLine.end());
			RemoveLine(pos.mLine + 1);
		}
		else
		{
			auto cindex = GetCharacterIndex(pos);

			auto d = UTF8CharLength(line[cindex].mChar);
			while (d-- > 0 && cindex < (int)line.size())
				line.erase(line.begin() + cindex);
		}

		mTextChanged = true;

	}

}

void TerminalView::Backspace()
{
	assert(!mReadOnly);

	if (mLines.empty())
		return;

	if (HasSelection())
	{
		DeleteSelection();
	}
	else
	{
		auto pos = GetActualCursorCoordinates();
		SetCursorPosition(pos);

		if (mState.mCursorPosition.mColumn == 0)
		{
			if (mState.mCursorPosition.mLine == 0)
				return;

			auto& line = mLines[mState.mCursorPosition.mLine];
			auto& prevLine = mLines[mState.mCursorPosition.mLine - 1];
			auto prevSize = GetLineMaxColumn(mState.mCursorPosition.mLine - 1);
			prevLine.insert(prevLine.end(), line.begin(), line.end());

			ErrorMarkers etmp;
			for (auto& i : mErrorMarkers)
				etmp.insert(ErrorMarkers::value_type(i.first - 1 == mState.mCursorPosition.mLine ? i.first - 1 : i.first, i.second));
			mErrorMarkers = std::move(etmp);

			RemoveLine(mState.mCursorPosition.mLine);
			--mState.mCursorPosition.mLine;
			mState.mCursorPosition.mColumn = prevSize;
		}
		else
		{
			auto& line = mLines[mState.mCursorPosition.mLine];
			auto cindex = GetCharacterIndex(pos) - 1;
			auto cend = cindex + 1;
			while (cindex > 0 && IsUTFSequence(line[cindex].mChar))
				--cindex;

			//if (cindex > 0 && UTF8CharLength(line[cindex].mChar) > 1)
			//	--cindex;

			--mState.mCursorPosition.mColumn;

			while (cindex < line.size() && cend-- > cindex)
			{
				line.erase(line.begin() + cindex);
			}
		}

		mTextChanged = true;

		EnsureCursorVisible();
	}

}

void TerminalView::SelectWordUnderCursor()
{
	auto c = GetCursorPosition();
	SetSelection(FindWordStart(c), FindWordEnd(c));
}

void TerminalView::SelectAll()
{
	SetSelection(Coordinates(0, 0), Coordinates((int)mLines.size(), 0));
}

bool TerminalView::HasSelection() const
{
	return mState.mSelectionEnd > mState.mSelectionStart;
}

// TODO: Clean up IsLastLineVisible()
bool TerminalView::IsLastLineVisible()
{
	//float scrollX = ImGui::GetScrollX();
	float scrollY = ImGui::GetScrollY();

	auto height = ImGui::GetWindowHeight();
	//auto width = ImGui::GetWindowWidth();

	auto top = 1 + (int)ceil(scrollY / mCharAdvance.y);
	auto bottom = (int)ceil((scrollY + height) / mCharAdvance.y);

	//auto left = (int)ceil(scrollX / mCharAdvance.x);
	//auto right = (int)ceil((scrollX + width) / mCharAdvance.x);

	return (bottom >= mLines.size());
}

const TerminalView::RenderGeometry& TerminalView::GetLastRenderGeometry()
{
	return mLastRenderGeometry;
}


void TerminalView::Copy()
{
	if (HasSelection())
	{
		ImGui::SetClipboardText(GetSelectedText().c_str());
	}
	else
	{
		if (!mLines.empty())
		{
			std::string str;
			auto& line = mLines[GetActualCursorCoordinates().mLine];
			for (auto& g : line)
				str.push_back(g.mChar);
			ImGui::SetClipboardText(str.c_str());
		}
	}
}

void TerminalView::Cut()
{
	if (IsReadOnly())
	{
		Copy();
	}
	else
	{
		if (HasSelection())
		{
			Copy();
			DeleteSelection();
		}
	}
}

void TerminalView::Paste()
{
	if (IsReadOnly())
		return;

	auto clipText = ImGui::GetClipboardText();
	if (clipText != nullptr && strlen(clipText) > 0)
	{

		if (HasSelection())
		{
			DeleteSelection();
		}

		InsertText(clipText);
	}
}

//#define DEFAULT_COLOR 0xff7f7f7f // original
#define DEFAULT_COLOR 0xffeeeeee


const TerminalView::Palette & TerminalView::GetDarkPalette()
{
	const static Palette p = { {
			DEFAULT_COLOR,	// Default
			0xffd69c56,	// Keyword	
			0xff00ff00,	// Number
			0xff7070e0,	// String
			0xff70a0e0, // Char literal
			0xffffffff, // Punctuation
			0xff408080,	// Preprocessor
			0xffaaaaaa, // Identifier
			0xff9bc64d, // Known identifier
			0xffc040a0, // Preproc identifier
			0xff206020, // Comment (single line)
			0xff406020, // Comment (multi line)
			0xff101010, // Background
			0xffe0e0e0, // Cursor
			0x80a06020, // Selection
			0x800020ff, // ErrorMarker
			0x40f08000, // Breakpoint
			0xff707000, // Line number
			0x40000000, // Current line fill
			0x40808080, // Current line fill (inactive)
			0x40a0a0a0, // Current line edge
			0xff000000, //        Black   ANSI FG=30 BG= 40
			0xff0000bb, //        Red     ANSI FG=31 BG= 41
			0xff00bb00, //        Green   ANSI FG=32 BG= 42
			0xff00bbbb, //        Yellow  ANSI FG=33 BG= 43
			0xffbb0000, //        Blue    ANSI FG=34 BG= 44
			0xffbb00bb, //        Magenta ANSI FG=35 BG= 45
			0xffbbbb00, //        Cyan    ANSI FG=36 BG= 46
			0xffbbbbbb, //        White   ANSI FG=37 BG= 47
			0xff555555, // Bright Black   ANSI FG=90 BG=100
			0xff5555ff, // Bright Red     ANSI FG=91 BG=101
			0xff55ff55, // Bright Green   ANSI FG=92 BG=102
			0xff55ffff, // Bright Yellow  ANSI FG=93 BG=103
			0xffff5555, // Bright Blue    ANSI FG=94 BG=104
			0xffff55ff, // Bright Magenta ANSI FG=95 BG=105
			0xffffff55, // Bright Cyan    ANSI FG=96 BG=106
			0xffffffff  // Bright White   ANSI FG=97 BG=107
		} };
	return p;
}

const TerminalView::Palette & TerminalView::GetLightPalette()
{
	const static Palette p = { {
			DEFAULT_COLOR,	// None
			0xffff0c06,	// Keyword	
			0xff008000,	// Number
			0xff2020a0,	// String
			0xff304070, // Char literal
			0xff000000, // Punctuation
			0xff406060,	// Preprocessor
			0xff404040, // Identifier
			0xff606010, // Known identifier
			0xffc040a0, // Preproc identifier
			0xff205020, // Comment (single line)
			0xff405020, // Comment (multi line)
			0xffffffff, // Background
			0xff000000, // Cursor
			0x80600000, // Selection
			0xa00010ff, // ErrorMarker
			0x80f08000, // Breakpoint
			0xff505000, // Line number
			0x40000000, // Current line fill
			0x40808080, // Current line fill (inactive)
			0x40000000, // Current line edge
			0xff000000, //        Black   ANSI FG=30 BG= 40
			0xff0000bb, //        Red     ANSI FG=31 BG= 41
			0xff00bb00, //        Green   ANSI FG=32 BG= 42
			0xff00bbbb, //        Yellow  ANSI FG=33 BG= 43
			0xffbb0000, //        Blue    ANSI FG=34 BG= 44
			0xffbb00bb, //        Magenta ANSI FG=35 BG= 45
			0xffbbbb00, //        Cyan    ANSI FG=36 BG= 46
			0xffbbbbbb, //        White   ANSI FG=37 BG= 47
			0xff555555, // Bright Black   ANSI FG=90 BG=100
			0xff5555ff, // Bright Red     ANSI FG=91 BG=101
			0xff55ff55, // Bright Green   ANSI FG=92 BG=102
			0xff55ffff, // Bright Yellow  ANSI FG=93 BG=103
			0xffff5555, // Bright Blue    ANSI FG=94 BG=104
			0xffff55ff, // Bright Magenta ANSI FG=95 BG=105
			0xffffff55, // Bright Cyan    ANSI FG=96 BG=106
			0xffffffff  // Bright White   ANSI FG=97 BG=107
		} };
	return p;
}

const TerminalView::Palette & TerminalView::GetRetroBluePalette()
{
	const static Palette p = { {
			0xff00ffff,	// None
			0xffffff00,	// Keyword	
			0xff00ff00,	// Number
			0xff808000,	// String
			0xff808000, // Char literal
			0xffffffff, // Punctuation
			0xff008000,	// Preprocessor
			0xff00ffff, // Identifier
			0xffffffff, // Known identifier
			0xffff00ff, // Preproc identifier
			0xff808080, // Comment (single line)
			0xff404040, // Comment (multi line)
			0xff800000, // Background
			0xff0080ff, // Cursor
			0x80ffff00, // Selection
			0xa00000ff, // ErrorMarker
			0x80ff8000, // Breakpoint
			0xff808000, // Line number
			0x40000000, // Current line fill
			0x40808080, // Current line fill (inactive)
			0x40000000, // Current line edge
			0xff000000, //        Black   ANSI FG=30 BG= 40
			0xff0000bb, //        Red     ANSI FG=31 BG= 41
			0xff00bb00, //        Green   ANSI FG=32 BG= 42
			0xff00bbbb, //        Yellow  ANSI FG=33 BG= 43
			0xffbb0000, //        Blue    ANSI FG=34 BG= 44
			0xffbb00bb, //        Magenta ANSI FG=35 BG= 45
			0xffbbbb00, //        Cyan    ANSI FG=36 BG= 46
			0xffbbbbbb, //        White   ANSI FG=37 BG= 47
			0xff555555, // Bright Black   ANSI FG=90 BG=100
			0xff5555ff, // Bright Red     ANSI FG=91 BG=101
			0xff55ff55, // Bright Green   ANSI FG=92 BG=102
			0xff55ffff, // Bright Yellow  ANSI FG=93 BG=103
			0xffff5555, // Bright Blue    ANSI FG=94 BG=104
			0xffff55ff, // Bright Magenta ANSI FG=95 BG=105
			0xffffff55, // Bright Cyan    ANSI FG=96 BG=106
			0xffffffff  // Bright White   ANSI FG=97 BG=107
		} };
	return p;
}

std::vector<uint8_t> TerminalView::GetTerminalOutput()
{
	if (mQueuedTerminalOutput.empty()) {
		throw std::underflow_error("No terminal output to get. Check TerminalOutputAvailable() before calling.");
	}
	else {
		auto item = mQueuedTerminalOutput.front();
		mQueuedTerminalOutput.pop();
		return item;
	}
}

bool TerminalView::TerminalOutputAvailable()
{
	return !mQueuedTerminalOutput.empty();
}

TerminalData::PaletteIndex TerminalView::GetPaletteIndex(TerminalState aTermState)
{
	using enum TerminalGraphicsState::Flags;

	int icolor = 0;

	if (aTermState.IsInverse()) {
		icolor = (int)aTermState.getBackgroundColor();
	}
	else {
		icolor = (int)aTermState.getForegroundColor();
	}

	TerminalData::PaletteIndex pal = TerminalData::PaletteIndex::Default;

	if (icolor & (int)BlackFg) {
		pal = TerminalData::PaletteIndex::Black;
	}
	else if (icolor & (int)RedFg) {
		pal = TerminalData::PaletteIndex::Red;
	}
	else if (icolor & (int)GreenFg) {
		pal = TerminalData::PaletteIndex::Green;
	}
	else if (icolor & (int)YellowFg) {
		pal = TerminalData::PaletteIndex::Yellow;
	}
	else if (icolor & (int)BlueFg) {
		pal = TerminalData::PaletteIndex::Blue;
	}
	else if (icolor & (int)MagentaFg) {
		pal = TerminalData::PaletteIndex::Magenta;
	}
	else if (icolor & (int)CyanFg) {
		pal = TerminalData::PaletteIndex::Cyan;
	}
	else if (icolor & (int)WhiteFg) {
		pal = TerminalData::PaletteIndex::White;
	}

	return pal;
}


ImWchar TerminalView::GetKeyboardInput()
{
	if (mInputQueueCharacters.empty()) {
		throw std::underflow_error("No keyboard input to get. Check KeyboardInputAvailable() before calling.");
	}
	else {
		auto item = mInputQueueCharacters.front();
		mInputQueueCharacters.pop();
		return item;
	}
}

bool TerminalView::KeyboardInputAvailable()
{
	return !mInputQueueCharacters.empty();
}




std::string TerminalView::GetText() const
{
	return GetText(Coordinates(), Coordinates((int)mLines.size(), 0));
}

std::vector<std::string> TerminalView::GetTextLines() const
{
	std::vector<std::string> result;

	result.reserve(mLines.size());

	for (auto & line : mLines)
	{
		std::string text;

		text.resize(line.size());

		for (size_t i = 0; i < line.size(); ++i)
			text[i] = line[i].mChar;

		result.emplace_back(std::move(text));
	}

	return result;
}

std::string TerminalView::GetSelectedText() const
{
	return GetText(mState.mSelectionStart, mState.mSelectionEnd);
}

std::string TerminalView::GetCurrentLineText()const
{
	auto lineLength = GetLineMaxColumn(mState.mCursorPosition.mLine);
	return GetText(
		Coordinates(mState.mCursorPosition.mLine, 0),
		Coordinates(mState.mCursorPosition.mLine, lineLength));
}

void TerminalView::ProcessInputs()
{
}

float TerminalView::TextDistanceToLineStart(const Coordinates& aFrom) const
{
	auto& line = mLines[aFrom.mLine];
	float distance = 0.0f;
	float spaceSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, " ", nullptr, nullptr).x;
	int colIndex = GetCharacterIndex(aFrom);
	for (size_t it = 0u; it < line.size() && it < colIndex; )
	{
		if (line[it].mChar == '\t')
		{
			distance = (1.0f + std::floor((1.0f + distance) / (float(mTabSize) * spaceSize))) * (float(mTabSize) * spaceSize);
			++it;
		}
		else
		{
			auto d = UTF8CharLength(line[it].mChar);
			char tempCString[7];
			int i = 0;
			for (; i < 6 && d-- > 0 && it < (int)line.size(); i++, it++)
				tempCString[i] = line[it].mChar;

			tempCString[i] = '\0';
			distance += ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, tempCString, nullptr, nullptr).x;
		}
	}

	return distance;
}

void TerminalView::EnsureCursorVisible()
{
	if (!mWithinRender)
	{
		mScrollToCursor = true;
		return;
	}

	float scrollX = ImGui::GetScrollX();
	float scrollY = ImGui::GetScrollY();

	auto height = ImGui::GetWindowHeight();
	auto width = ImGui::GetWindowWidth();

	auto top = 1 + (int)ceil(scrollY / mCharAdvance.y);
	auto bottom = (int)ceil((scrollY + height) / mCharAdvance.y);

	auto left = (int)ceil(scrollX / mCharAdvance.x);
	auto right = (int)ceil((scrollX + width) / mCharAdvance.x);

	auto pos = GetActualCursorCoordinates();
	auto len = TextDistanceToLineStart(pos);

	if (pos.mLine < top)
		ImGui::SetScrollY(std::max(0.0f, (pos.mLine - 1) * mCharAdvance.y));
	if (pos.mLine > bottom - 4)
		ImGui::SetScrollY(std::max(0.0f, (pos.mLine + 4) * mCharAdvance.y - height));
	if (len + mTextStart < left + 4)
		ImGui::SetScrollX(std::max(0.0f, len + mTextStart - 4));
	if (len + mTextStart > right - 4)
		ImGui::SetScrollX(std::max(0.0f, len + mTextStart + 4 - width));
}

int TerminalView::GetPageSize() const
{
	auto height = ImGui::GetWindowHeight() - 20.0f;
	return (int)floor(height / mCharAdvance.y);
}
