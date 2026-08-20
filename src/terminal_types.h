#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace imterm {

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

	// A line's timestamp is the time of its most recent content mutation. This
	// matches what users see in the terminal and what is written to the log when
	// the line is completed. Glyph storage is intentionally read-only outside
	// TerminalData so all mutations preserve the terminal-buffer invariants.
	class TerminalLine
	{
	public:
		using const_iterator = std::vector<Glyph>::const_iterator;
		using size_type = std::vector<Glyph>::size_type;
		using Timestamp = std::chrono::system_clock::time_point;

		TerminalLine() = default;
		TerminalLine(std::initializer_list<Glyph> aGlyphs)
			: mGlyphs(aGlyphs)
		{
		}

		bool empty() const noexcept { return mGlyphs.empty(); }
		size_type size() const noexcept { return mGlyphs.size(); }
		const Glyph& operator[](size_type aIndex) const { return mGlyphs[aIndex]; }
		const Glyph& at(size_type aIndex) const { return mGlyphs.at(aIndex); }
		const Glyph& front() const { return mGlyphs.front(); }
		const Glyph& back() const { return mGlyphs.back(); }
		const_iterator begin() const noexcept { return mGlyphs.begin(); }
		const_iterator end() const noexcept { return mGlyphs.end(); }
		Timestamp GetTimestamp() const noexcept { return mTimestamp; }

	private:
		friend class TerminalData;

		void Touch() noexcept { mTimestamp = std::chrono::system_clock::now(); }

		std::vector<Glyph> mGlyphs;
		Timestamp mTimestamp = std::chrono::system_clock::now();
	};

	using Line = TerminalLine;
	using Lines = std::vector<Line>;

}
