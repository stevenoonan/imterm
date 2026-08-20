#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include <optional>

#include "terminal_types.h"
#include "coordinates.h"
#include "terminal_logger.h"

namespace imterm {

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

		void InputGlyph(size_t aLineIndex, int& aColumnIndex, PaletteIndex aPaletteIndex, uint8_t aValue);
		void EraseBytes(size_t aLineIndex, size_t aStart, size_t aEnd);
		void ReplaceBytesWithSpaces(size_t aLineIndex, size_t aStart, size_t aEnd);

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

		static int UTF8CharLength(Char c, size_t available)
		{
			return static_cast<int>(std::min(
				available, static_cast<size_t>(UTF8CharLength(c))));
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
