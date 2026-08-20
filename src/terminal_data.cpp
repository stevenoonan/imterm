#include "terminal_data.h"

#include <cassert>
#include <iostream>
#include <stdexcept>

namespace {

	unsigned char FoldAscii(unsigned char aValue)
	{
		return aValue >= 'A' && aValue <= 'Z'
			? static_cast<unsigned char>(aValue + ('a' - 'A'))
			: aValue;
	}

	bool BytesEqual(char aLeft, char aRight, bool aMatchCase)
	{
		const auto left = static_cast<unsigned char>(aLeft);
		const auto right = static_cast<unsigned char>(aRight);
		return aMatchCase ? left == right : FoldAscii(left) == FoldAscii(right);
	}

	std::optional<size_t> FindForwardInLine(
		const imterm::Line& aLine, std::string_view aQuery,
		size_t aStart, size_t aEnd, bool aMatchCase)
	{
		const size_t end = std::min(aEnd, aLine.size());
		if (aQuery.empty() || aStart > end || aQuery.size() > end - aStart) {
			return std::nullopt;
		}

		for (size_t start = aStart; start + aQuery.size() <= end; ++start) {
			bool matches = true;
			for (size_t offset = 0; offset < aQuery.size(); ++offset) {
				if (!BytesEqual(
						static_cast<char>(aLine[start + offset].mChar),
						aQuery[offset], aMatchCase)) {
					matches = false;
					break;
				}
			}
			if (matches) {
				return start;
			}
		}
		return std::nullopt;
	}

	std::optional<size_t> FindBackwardInLine(
		const imterm::Line& aLine, std::string_view aQuery,
		size_t aStart, size_t aEnd, bool aMatchCase)
	{
		const size_t end = std::min(aEnd, aLine.size());
		if (aQuery.empty() || aStart > end || aQuery.size() > end - aStart) {
			return std::nullopt;
		}

		const size_t lastStart = end - aQuery.size();
		for (size_t start = lastStart;; --start) {
			bool matches = true;
			for (size_t offset = 0; offset < aQuery.size(); ++offset) {
				if (!BytesEqual(
						static_cast<char>(aLine[start + offset].mChar),
						aQuery[offset], aMatchCase)) {
					matches = false;
					break;
				}
			}
			if (matches) {
				return start;
			}
			if (start == aStart) {
				break;
			}
		}
		return std::nullopt;
	}

}

namespace imterm {

	TerminalData::TerminalData()
		: mReadOnly(false), mTextChanged(false), mTabSize(4)
	{
	}

	TerminalData::TerminalData(std::shared_ptr<TerminalLogger> aLogger)
		: mReadOnly(false), mTextChanged(false), mTabSize(4),
		  mLogger(std::move(aLogger))
	{
		if (mLogger) {
			mLoggerWatcherToken = mLogger->RegisterLogClosingWatcher([this] {
				LogPendingLine(true);
			});
		}
	}

	TerminalData::~TerminalData()
	{
		if (!mLogger) {
			return;
		}

		try {
			LogPendingLine(true);
		}
		catch (const std::exception& error) {
			std::cerr << "TerminalData final log flush failed: "
				<< error.what() << '\n';
		}
		catch (...) {
			std::cerr << "TerminalData final log flush failed\n";
		}

		if (mLoggerWatcherToken) {
			mLogger->DeregisterLogClosingWatcher(*mLoggerWatcherToken);
		}
	}

	void TerminalData::SetReadOnly(bool aValue)
	{
		mReadOnly = aValue;
	}

	void TerminalData::Touch(Line& aLine) noexcept
	{
		aLine.Touch();
		mTextChanged = true;
	}

	void TerminalData::ResetPendingLog(size_t aLineIndex)
	{
		assert(!mLines.empty());
		aLineIndex = std::min(aLineIndex, mLines.size() - 1);
		mPendingLog = PendingLog{
			aLineIndex, static_cast<int>(aLineIndex + 1)};
	}

	void TerminalData::AdjustPendingLogForRemoval(size_t aStart, size_t aEnd)
	{
		if (!mPendingLog) {
			return;
		}

		if (mPendingLog->mLineIndex >= aEnd) {
			mPendingLog->mLineIndex -= aEnd - aStart;
		}
		else if (mPendingLog->mLineIndex >= aStart) {
			mPendingLog.reset();
		}
	}

	void TerminalData::RemoveLine(int aStart, int aEnd)
	{
		assert(!mReadOnly);
		if (mReadOnly) {
			return;
		}
		if (aStart < 0 || aEnd < aStart
			|| static_cast<size_t>(aEnd) > mLines.size()) {
			throw std::out_of_range("TerminalData::RemoveLine range");
		}

		const size_t start = static_cast<size_t>(aStart);
		const size_t end = static_cast<size_t>(aEnd);
		if (start == end) {
			return;
		}
		if (end - start >= mLines.size()) {
			throw std::invalid_argument(
				"TerminalData cannot remove every line");
		}

		AdjustPendingLogForRemoval(start, end);
		mLines.erase(
			mLines.begin() + static_cast<std::ptrdiff_t>(start),
			mLines.begin() + static_cast<std::ptrdiff_t>(end));
		mTextChanged = true;
	}

	void TerminalData::RemoveLine(int aIndex)
	{
		RemoveLine(aIndex, aIndex + 1);
	}

	void TerminalData::InsertLine(int aIndex)
	{
		assert(!mReadOnly);
		if (mReadOnly) {
			return;
		}
		if (aIndex < 0 || static_cast<size_t>(aIndex) > mLines.size()) {
			throw std::out_of_range("TerminalData::InsertLine index");
		}

		LogPendingLine();
		const size_t index = static_cast<size_t>(aIndex);
		mLines.insert(
			mLines.begin() + static_cast<std::ptrdiff_t>(index), Line());
		ResetPendingLog(index);
		mTextChanged = true;
	}

	void TerminalData::EnsureLineExists(size_t aIndex)
	{
		while (mLines.size() <= aIndex) {
			InsertLine(static_cast<int>(mLines.size()));
		}
	}

	void TerminalData::LogPendingLine(bool aLoggerIsClosing)
	{
		if (!mPendingLog) {
			return;
		}

		const PendingLog pending = *mPendingLog;
		if (pending.mLineIndex >= mLines.size()) {
			mPendingLog.reset();
			return;
		}

		const Line& line = mLines[pending.mLineIndex];
		if (aLoggerIsClosing && line.empty() && pending.mLineNumber == 1) {
			return;
		}

		mPendingLog.reset();
		if (mLogger) {
			try {
				mLogger->Log(line, pending.mLineNumber);
			}
			catch (...) {
				mPendingLog = pending;
				throw;
			}
		}
	}

	void TerminalData::EraseBytes(
		size_t aLineIndex, size_t aStart, size_t aEnd)
	{
		assert(!mReadOnly);
		if (mReadOnly) {
			return;
		}
		Line& line = mLines.at(aLineIndex);
		const size_t start = std::min(aStart, line.mGlyphs.size());
		const size_t end = std::min(std::max(aEnd, start), line.mGlyphs.size());
		if (start == end) {
			return;
		}

		line.mGlyphs.erase(
			line.mGlyphs.begin() + static_cast<std::ptrdiff_t>(start),
			line.mGlyphs.begin() + static_cast<std::ptrdiff_t>(end));
		if (!mPendingLog) {
			ResetPendingLog(aLineIndex);
		}
		Touch(line);
	}

	void TerminalData::ReplaceBytesWithSpaces(
		size_t aLineIndex, size_t aStart, size_t aEnd)
	{
		assert(!mReadOnly);
		if (mReadOnly) {
			return;
		}
		Line& line = mLines.at(aLineIndex);
		const size_t start = std::min(aStart, line.mGlyphs.size());
		const size_t end = std::min(std::max(aEnd, start), line.mGlyphs.size());
		if (start == end) {
			return;
		}

		for (size_t index = start; index < end; ++index) {
			line.mGlyphs[index].mChar = ' ';
		}
		if (!mPendingLog) {
			ResetPendingLog(aLineIndex);
		}
		Touch(line);
	}

	void TerminalData::ClearLine(size_t aLineIndex)
	{
		EraseBytes(aLineIndex, 0, GetLineSize(aLineIndex));
	}

	void TerminalData::ReplaceLinePrefixWithSpaces(
		size_t aLineIndex, RenderedColumn aThroughColumn)
	{
		assert(!mReadOnly);
		if (mReadOnly) {
			return;
		}
		Line& line = mLines.at(aLineIndex);
		const BufferPosition position{aLineIndex, aThroughColumn};
		const size_t byteEnd = GetByteOffsetAfter(position).mValue;
		const int originalEndColumn = GetCharacterColumn(
			static_cast<int>(aLineIndex), static_cast<int>(byteEnd));
		const size_t blankColumns = static_cast<size_t>(std::max(
			aThroughColumn.mValue + 1, originalEndColumn));

		line.mGlyphs.erase(
			line.mGlyphs.begin(),
			line.mGlyphs.begin() + static_cast<std::ptrdiff_t>(byteEnd));
		line.mGlyphs.insert(
			line.mGlyphs.begin(), blankColumns,
			Glyph(' ', PaletteIndex::Default));
		if (!mPendingLog) {
			ResetPendingLog(aLineIndex);
		}
		Touch(line);
	}

	ByteOffset TerminalData::GetByteOffset(
		const BufferPosition& aPosition) const
	{
		const Line& line = mLines.at(aPosition.mRow);
		const int targetColumn = std::max(aPosition.mColumn.mValue, 0);
		int renderedColumn = 0;
		for (size_t index = 0; index < line.size();) {
			const Char character = line[index].mChar;
			const size_t characterLength = static_cast<size_t>(UTF8CharLength(
				character, line.size() - index));
			const int nextColumn = character == '\t'
				? (renderedColumn / mTabSize) * mTabSize + mTabSize
				: renderedColumn + 1;
			if (targetColumn < nextColumn) {
				return ByteOffset{index};
			}
			index += characterLength;
			renderedColumn = nextColumn;
		}
		return ByteOffset{line.size()};
	}

	ByteOffset TerminalData::GetByteOffsetAfter(
		const BufferPosition& aPosition) const
	{
		const Line& line = mLines.at(aPosition.mRow);
		const int targetColumn = std::max(aPosition.mColumn.mValue, 0);
		int renderedColumn = 0;
		for (size_t index = 0; index < line.size();) {
			const Char character = line[index].mChar;
			const size_t characterLength = static_cast<size_t>(UTF8CharLength(
				character, line.size() - index));
			const int nextColumn = character == '\t'
				? (renderedColumn / mTabSize) * mTabSize + mTabSize
				: renderedColumn + 1;
			if (targetColumn < nextColumn) {
				return ByteOffset{index + characterLength};
			}
			index += characterLength;
			renderedColumn = nextColumn;
		}
		return ByteOffset{line.size()};
	}

	void TerminalData::DeleteRange(
		const Coordinates& aStart, const Coordinates& aEnd)
	{
		assert(aEnd >= aStart);
		assert(!mReadOnly);
		if (mReadOnly || aEnd < aStart || aStart.mLine < 0
			|| aEnd.mLine < 0
			|| static_cast<size_t>(aStart.mLine) >= mLines.size()
			|| static_cast<size_t>(aEnd.mLine) >= mLines.size()) {
			return;
		}
		if (aEnd == aStart) {
			return;
		}

		const int startIndex = GetCharacterIndex(aStart);
		const int endIndex = GetCharacterIndex(aEnd);
		if (startIndex < 0 || endIndex < 0) {
			return;
		}

		const size_t startLineIndex = static_cast<size_t>(aStart.mLine);
		const size_t endLineIndex = static_cast<size_t>(aEnd.mLine);
		if (startLineIndex == endLineIndex) {
			const Line& line = mLines[startLineIndex];
			const size_t start = std::min(
				static_cast<size_t>(startIndex), line.size());
			const size_t end = aEnd.mColumn >= GetLineMaxColumn(aStart.mLine)
				? line.size()
				: std::min(static_cast<size_t>(endIndex), line.size());
			EraseBytes(startLineIndex, start, end);
			return;
		}

		Line& firstLine = mLines[startLineIndex];
		Line& lastLine = mLines[endLineIndex];
		const size_t firstErase = std::min(
			static_cast<size_t>(startIndex), firstLine.mGlyphs.size());
		const size_t lastErase = std::min(
			static_cast<size_t>(endIndex), lastLine.mGlyphs.size());
		firstLine.mGlyphs.erase(
			firstLine.mGlyphs.begin() + static_cast<std::ptrdiff_t>(firstErase),
			firstLine.mGlyphs.end());
		firstLine.mGlyphs.insert(
			firstLine.mGlyphs.end(),
			lastLine.mGlyphs.begin() + static_cast<std::ptrdiff_t>(lastErase),
			lastLine.mGlyphs.end());
		Touch(firstLine);
		RemoveLine(aStart.mLine + 1, aEnd.mLine + 1);
		if (!mPendingLog) {
			ResetPendingLog(startLineIndex);
		}
	}

	int TerminalData::InsertTextAt(
		Coordinates& /* inout */ aWhere, const char* aValue)
	{
		assert(!mReadOnly);
		if (mReadOnly || aValue == nullptr || aWhere.mLine < 0
			|| static_cast<size_t>(aWhere.mLine) >= mLines.size()) {
			return 0;
		}

		int characterIndex = GetCharacterIndex(aWhere);
		int totalLines = 0;
		while (*aValue != '\0') {
			if (*aValue == '\r') {
				++aValue;
			}
			else if (*aValue == '\n') {
			{
				const size_t lineIndex = static_cast<size_t>(aWhere.mLine);
				const size_t splitIndex = std::min(
					static_cast<size_t>(std::max(characterIndex, 0)),
					mLines[lineIndex].mGlyphs.size());
				InsertLine(aWhere.mLine + 1);

				Line& line = mLines[lineIndex];
				Line& newLine = mLines[lineIndex + 1];
				newLine.mGlyphs.insert(
					newLine.mGlyphs.end(),
					line.mGlyphs.begin() + static_cast<std::ptrdiff_t>(splitIndex),
					line.mGlyphs.end());
				line.mGlyphs.erase(
					line.mGlyphs.begin() + static_cast<std::ptrdiff_t>(splitIndex),
					line.mGlyphs.end());
				Touch(line);
				if (!newLine.mGlyphs.empty()) {
					Touch(newLine);
				}
			}
				++aWhere.mLine;
				aWhere.mColumn = 0;
				characterIndex = 0;
				++totalLines;
				++aValue;
			}
			else {
				Line& line = mLines[static_cast<size_t>(aWhere.mLine)];
				int bytesRemaining = UTF8CharLength(*aValue);
				while (bytesRemaining-- > 0 && *aValue != '\0') {
					line.mGlyphs.insert(
						line.mGlyphs.begin() + characterIndex++,
						Glyph(*aValue++, PaletteIndex::Default));
				}
				Touch(line);
				++aWhere.mColumn;
			}
		}

		return totalLines;
	}

	void TerminalData::SetText(const std::string& aText)
	{
		assert(!mReadOnly);
		if (mReadOnly) {
			return;
		}
		LogPendingLine(true);
		mLines.clear();
		mLines.emplace_back();
		for (const char character : aText) {
			if (character == '\r') {
				continue;
			}
			if (character == '\n') {
				mLines.emplace_back();
			}
			else {
				mLines.back().mGlyphs.emplace_back(
					character, PaletteIndex::Default);
			}
		}
		for (Line& line : mLines) {
			if (!line.mGlyphs.empty()) {
				line.Touch();
			}
		}
		ResetPendingLog(mLines.size() - 1);
		mTextChanged = true;
	}

	void TerminalData::SetTextLines(const std::vector<std::string>& aLines)
	{
		assert(!mReadOnly);
		if (mReadOnly) {
			return;
		}
		LogPendingLine(true);
		mLines.clear();
		mLines.resize(std::max<size_t>(1, aLines.size()));

		for (size_t lineIndex = 0; lineIndex < aLines.size(); ++lineIndex) {
			Line& line = mLines[lineIndex];
			line.mGlyphs.reserve(aLines[lineIndex].size());
			for (const char character : aLines[lineIndex]) {
				line.mGlyphs.emplace_back(character, PaletteIndex::Default);
			}
			if (!line.mGlyphs.empty()) {
				line.Touch();
			}
		}

		ResetPendingLog(mLines.size() - 1);
		mTextChanged = true;
	}

	std::string TerminalData::GetText(
		const Coordinates& aStart, const Coordinates& aEnd) const
	{
		std::string result;
		int lineIndex = aStart.mLine;
		const int endLine = aEnd.mLine;
		int byteIndex = GetCharacterIndex(aStart);
		const int endByteIndex = GetCharacterIndex(aEnd);
		if (lineIndex < 0 || byteIndex < 0) {
			return result;
		}

		size_t reserveSize = 0;
		for (int index = lineIndex;
			index < endLine && static_cast<size_t>(index) < mLines.size(); ++index) {
			reserveSize += mLines[static_cast<size_t>(index)].size();
		}
		result.reserve(reserveSize + reserveSize / 8);

		while (byteIndex < endByteIndex || lineIndex < endLine) {
			if (static_cast<size_t>(lineIndex) >= mLines.size()) {
				break;
			}

			const Line& line = mLines[static_cast<size_t>(lineIndex)];
			if (static_cast<size_t>(byteIndex) < line.size()) {
				result += line[static_cast<size_t>(byteIndex)].mChar;
				++byteIndex;
			}
			else {
				byteIndex = 0;
				++lineIndex;
				result += '\n';
			}
		}

		return result;
	}

	std::string TerminalData::GetText() const
	{
		return GetText(
			Coordinates(), Coordinates(static_cast<int>(mLines.size()), 0));
	}

	std::vector<std::string> TerminalData::GetTextLines() const
	{
		std::vector<std::string> result;
		result.reserve(mLines.size());
		for (const Line& line : mLines) {
			std::string text;
			text.reserve(line.size());
			for (const Glyph& glyph : line) {
				text += static_cast<char>(glyph.mChar);
			}
			result.emplace_back(std::move(text));
		}
		return result;
	}

	std::optional<TextRange> TerminalData::FindNextText(
		std::string_view aQuery, const Coordinates& aFrom,
		bool aMatchCase) const
	{
		if (aQuery.empty() || aQuery.find_first_of("\r\n") != std::string_view::npos) {
			return std::nullopt;
		}

		const size_t anchorLine = std::min(
			static_cast<size_t>(std::max(aFrom.mLine, 0)), mLines.size() - 1);
		const size_t anchorByte = GetByteOffset(BufferPosition{
			anchorLine, RenderedColumn{std::max(aFrom.mColumn, 0)}}).mValue;

		const auto makeRange = [this, aQuery](size_t aLine, size_t aByte) {
			return TextRange{
				Coordinates(static_cast<int>(aLine), GetCharacterColumn(
					static_cast<int>(aLine), static_cast<int>(aByte))),
				Coordinates(static_cast<int>(aLine), GetCharacterColumn(
					static_cast<int>(aLine),
					static_cast<int>(aByte + aQuery.size())))};
		};

		for (size_t line = anchorLine; line < mLines.size(); ++line) {
			const size_t start = line == anchorLine ? anchorByte : 0;
			if (const auto match = FindForwardInLine(
					mLines[line], aQuery, start, mLines[line].size(), aMatchCase)) {
				return makeRange(line, *match);
			}
		}
		for (size_t line = 0; line <= anchorLine; ++line) {
			const size_t end = line == anchorLine ? anchorByte : mLines[line].size();
			if (const auto match = FindForwardInLine(
					mLines[line], aQuery, 0, end, aMatchCase)) {
				return makeRange(line, *match);
			}
		}
		return std::nullopt;
	}

	std::optional<TextRange> TerminalData::FindPreviousText(
		std::string_view aQuery, const Coordinates& aBefore,
		bool aMatchCase) const
	{
		if (aQuery.empty() || aQuery.find_first_of("\r\n") != std::string_view::npos) {
			return std::nullopt;
		}

		const size_t anchorLine = std::min(
			static_cast<size_t>(std::max(aBefore.mLine, 0)), mLines.size() - 1);
		const size_t anchorByte = GetByteOffset(BufferPosition{
			anchorLine, RenderedColumn{std::max(aBefore.mColumn, 0)}}).mValue;

		const auto makeRange = [this, aQuery](size_t aLine, size_t aByte) {
			return TextRange{
				Coordinates(static_cast<int>(aLine), GetCharacterColumn(
					static_cast<int>(aLine), static_cast<int>(aByte))),
				Coordinates(static_cast<int>(aLine), GetCharacterColumn(
					static_cast<int>(aLine),
					static_cast<int>(aByte + aQuery.size())))};
		};

		for (size_t line = anchorLine + 1; line-- > 0;) {
			const size_t end = line == anchorLine ? anchorByte : mLines[line].size();
			if (const auto match = FindBackwardInLine(
					mLines[line], aQuery, 0, end, aMatchCase)) {
				return makeRange(line, *match);
			}
		}
		for (size_t line = mLines.size(); line-- > anchorLine;) {
			const size_t start = line == anchorLine ? anchorByte : 0;
			if (const auto match = FindBackwardInLine(
					mLines[line], aQuery, start, mLines[line].size(), aMatchCase)) {
				return makeRange(line, *match);
			}
		}
		return std::nullopt;
	}

	bool TerminalData::TextMatches(
		const TextRange& aRange, std::string_view aQuery,
		bool aMatchCase) const
	{
		if (aQuery.empty() || aRange.mStart.mLine != aRange.mEnd.mLine
			|| aRange.mStart.mLine < 0
			|| static_cast<size_t>(aRange.mStart.mLine) >= mLines.size()) {
			return false;
		}
		const size_t line = static_cast<size_t>(aRange.mStart.mLine);
		const size_t start = GetByteOffset(BufferPosition{
			line, RenderedColumn{aRange.mStart.mColumn}}).mValue;
		const size_t end = GetByteOffset(BufferPosition{
			line, RenderedColumn{aRange.mEnd.mColumn}}).mValue;
		return end >= start && end - start == aQuery.size()
			&& FindForwardInLine(
				mLines[line], aQuery, start, end, aMatchCase) == start;
	}

	TextMatchStatus TerminalData::GetTextMatchStatus(
		std::string_view aQuery,
		const std::optional<TextRange>& aCurrentMatch,
		bool aMatchCase) const
	{
		TextMatchStatus status;
		if (aQuery.empty() || aQuery.find_first_of("\r\n") != std::string_view::npos) {
			return status;
		}

		for (size_t line = 0; line < mLines.size(); ++line) {
			size_t start = 0;
			while (start < mLines[line].size()) {
				const auto match = FindForwardInLine(
					mLines[line], aQuery, start, mLines[line].size(), aMatchCase);
				if (!match) {
					break;
				}

				++status.mTotal;
				const TextRange range{
					Coordinates(static_cast<int>(line), GetCharacterColumn(
						static_cast<int>(line), static_cast<int>(*match))),
					Coordinates(static_cast<int>(line), GetCharacterColumn(
						static_cast<int>(line),
						static_cast<int>(*match + aQuery.size())))};
				if (aCurrentMatch
					&& aCurrentMatch->mStart == range.mStart
					&& aCurrentMatch->mEnd == range.mEnd) {
					status.mCurrent = status.mTotal;
				}
				start = *match + aQuery.size();
			}
		}
		return status;
	}

	void TerminalData::InputGlyph(
		size_t aLineIndex, int& aColumnIndex,
		PaletteIndex aPaletteIndex, uint8_t aValue)
	{
		InputBytes(aLineIndex, aColumnIndex, aPaletteIndex,
			std::span<const uint8_t>(&aValue, 1));
	}

	void TerminalData::InputBytes(
		size_t aLineIndex, int& aColumnIndex,
		PaletteIndex aPaletteIndex, std::span<const uint8_t> aBytes)
	{
		assert(!mReadOnly);
		if (mReadOnly || aBytes.empty()) {
			return;
		}
		Line& line = mLines.at(aLineIndex);
		aColumnIndex = std::max(aColumnIndex, 0);
		while (GetLineMaxColumn(static_cast<int>(aLineIndex)) < aColumnIndex) {
			line.mGlyphs.emplace_back(' ', PaletteIndex::Default);
		}

		const BufferPosition position{
			aLineIndex, RenderedColumn{aColumnIndex}};
		const size_t start = GetByteOffset(position).mValue;
		const size_t finish = GetByteOffsetAfter(position).mValue;
		if (start < line.mGlyphs.size()) {
			line.mGlyphs.erase(
				line.mGlyphs.begin() + static_cast<std::ptrdiff_t>(start),
				line.mGlyphs.begin() + static_cast<std::ptrdiff_t>(finish));
		}
		line.mGlyphs.insert(
			line.mGlyphs.begin() + static_cast<std::ptrdiff_t>(start),
			aBytes.size(), Glyph(0, aPaletteIndex));
		for (size_t index = 0; index < aBytes.size(); ++index) {
			line.mGlyphs[start + index].mChar = aBytes[index];
		}
		if (!mPendingLog) {
			ResetPendingLog(aLineIndex);
		}
		Touch(line);
		++aColumnIndex;
	}

	int TerminalData::GetCharacterIndex(const Coordinates& aCoordinates) const
	{
		if (aCoordinates.mLine < 0
			|| static_cast<size_t>(aCoordinates.mLine) >= mLines.size()) {
			return -1;
		}
		const Line& line = mLines[static_cast<size_t>(aCoordinates.mLine)];
		int column = 0;
		size_t index = 0;
		while (index < line.size() && column < aCoordinates.mColumn) {
			if (line[index].mChar == '\t') {
				column = (column / mTabSize) * mTabSize + mTabSize;
			}
			else {
				++column;
			}
			index += static_cast<size_t>(UTF8CharLength(
				line[index].mChar, line.size() - index));
		}
		return static_cast<int>(index);
	}

	int TerminalData::GetCharacterColumn(int aLine, int aIndex) const
	{
		if (aLine < 0 || static_cast<size_t>(aLine) >= mLines.size()) {
			return 0;
		}
		const Line& line = mLines[static_cast<size_t>(aLine)];
		int column = 0;
		size_t index = 0;
		while (index < static_cast<size_t>(std::max(aIndex, 0))
			&& index < line.size()) {
			const Char character = line[index].mChar;
			index += static_cast<size_t>(UTF8CharLength(
				character, line.size() - index));
			if (character == '\t') {
				column = (column / mTabSize) * mTabSize + mTabSize;
			}
			else {
				++column;
			}
		}
		return column;
	}

	int TerminalData::GetLineCharacterCount(int aLine) const
	{
		if (aLine < 0 || static_cast<size_t>(aLine) >= mLines.size()) {
			return 0;
		}
		const Line& line = mLines[static_cast<size_t>(aLine)];
		int count = 0;
		for (size_t index = 0; index < line.size(); ++count) {
			index += static_cast<size_t>(UTF8CharLength(
				line[index].mChar, line.size() - index));
		}
		return count;
	}

	int TerminalData::GetLineMaxColumn(int aLine) const
	{
		if (aLine < 0 || static_cast<size_t>(aLine) >= mLines.size()) {
			return 0;
		}
		const Line& line = mLines[static_cast<size_t>(aLine)];
		int column = 0;
		for (size_t index = 0; index < line.size();) {
			const Char character = line[index].mChar;
			if (character == '\t') {
				column = (column / mTabSize) * mTabSize + mTabSize;
			}
			else {
				++column;
			}
			index += static_cast<size_t>(UTF8CharLength(
				character, line.size() - index));
		}
		return column;
	}

	void TerminalData::SetTabSize(int aValue)
	{
		mTabSize = std::max(1, std::min(32, aValue));
	}
}
