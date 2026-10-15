#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "terminal_types.h"
#include "utf8.h"
#include "coordinates.h"
#include "terminal_coordinates.h"
#include "terminal_logger.h"

namespace imterm {

	struct TextRange
	{
		Coordinates mStart;
		Coordinates mEnd;
	};

	struct TextMatchStatus
	{
		size_t mCurrent = 0;
		size_t mTotal = 0;
	};

	class TerminalData
	{
	public:

		TerminalData();
		explicit TerminalData(std::shared_ptr<TerminalLogger> aLogger);
		~TerminalData();

		// Temporary read-only adapter used by TerminalView during the refactor.
		const Lines& GetLines() const noexcept { return mLines; }
		const Line& GetLine(size_t aIndex) const { return mLines.at(aIndex); }
		size_t GetLineCount() const noexcept { return mLines.size(); }
		size_t GetLineSize(size_t aIndex) const { return mLines.at(aIndex).size(); }

		void InsertLine(int aIndex);
		void EnsureLineExists(size_t aIndex);

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
		std::optional<TextRange> FindNextText(
			std::string_view aQuery, const Coordinates& aFrom,
			bool aMatchCase) const;
		std::optional<TextRange> FindPreviousText(
			std::string_view aQuery, const Coordinates& aBefore,
			bool aMatchCase) const;
		bool TextMatches(
			const TextRange& aRange, std::string_view aQuery,
			bool aMatchCase) const;
		TextMatchStatus GetTextMatchStatus(
			std::string_view aQuery,
			const std::optional<TextRange>& aCurrentMatch,
			bool aMatchCase) const;

		void InputGlyph(size_t aLineIndex, int& aColumnIndex, PaletteIndex aPaletteIndex, uint8_t aValue);
		// Complete UTF-8 text, replacing malformed bytes. Stream fragments must go
		// through TerminalState so incomplete scalars can span serial reads.
		void InputBytes(size_t aLineIndex, int& aColumnIndex, PaletteIndex aPaletteIndex, std::span<const uint8_t> aBytes);
		void EraseBytes(size_t aLineIndex, size_t aStart, size_t aEnd);
		void ReplaceBytesWithSpaces(size_t aLineIndex, size_t aStart, size_t aEnd);
		void ReplaceLinePrefixWithSpaces(size_t aLineIndex, RenderedColumn aThroughColumn);
		void ClearLine(size_t aLineIndex);

		ByteOffset GetByteOffset(const BufferPosition& aPosition) const;
		ByteOffset GetByteOffsetAfter(const BufferPosition& aPosition) const;

		int GetCharacterIndex(const Coordinates& aCoordinates) const;
		int GetCharacterColumn(int aLine, int aIndex) const;
		int GetLineCharacterCount(int aLine) const;
		int GetLineMaxColumn(int aLine) const;

		void SetTabSize(int aValue);
		inline int GetTabSize() const { return mTabSize; }

		// Decode through the byte glyph adapter; never infer length from a lead byte.
		static size_t UTF8CharLength(const Line& aLine, size_t aIndex)
		{
			if (aIndex >= aLine.size()) return 0;
			const auto decoded = utf8::Decode(aLine.size() - aIndex,
				[&](size_t i) { return aLine[aIndex + i].mChar; });
			return decoded.mStatus == utf8::Status::Valid ? decoded.mLength : 1;
		}


	private:
		struct PendingLog {
			size_t mLineIndex;
			int mLineNumber;
		};

		Lines mLines = Lines{Line()};

		bool mReadOnly;
		bool mTextChanged;
		int mTabSize;

		std::shared_ptr<TerminalLogger> mLogger = nullptr;

		std::optional<PendingLog> mPendingLog = PendingLog{0, 1};
		std::optional<TerminalLogger::WatcherToken> mLoggerWatcherToken;

		void LogPendingLine(bool aLoggerIsClosing=false);
		void ResetPendingLog(size_t aLineIndex = 0);
		void AdjustPendingLogForRemoval(size_t aStart, size_t aEnd);
		void Touch(Line& aLine) noexcept;

	};

}
