#pragma once

#include <string>
#include <vector>
#include <array>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <regex>
#include <queue>
#include <chrono>

#include "imgui.h"
#include "coordinates.h"
#include "escape_sequence_parser.h"
#include "terminal_state.h"
#include "terminal_data.h"
#include "vector_timed.h"

namespace imterm {

	class TerminalView
	{
	public:

		struct Options {
			bool LineNumbers = true;
			bool TimeStamps = true;
		};

		enum class SelectionMode
		{
			Normal,
			Word,
			Line
		};

		struct Breakpoint
		{
			int mLine;
			bool mEnabled;
			std::string mCondition;

			Breakpoint()
				: mLine(-1)
				, mEnabled(false)
			{}
		};

		struct Identifier
		{
			Coordinates mLocation;
			std::string mDeclaration;
		};

		typedef std::string String;
		typedef std::unordered_map<std::string, Identifier> Identifiers;
		typedef std::unordered_set<std::string> Keywords;
		typedef std::map<int, std::string> ErrorMarkers;
		typedef std::unordered_set<int> Breakpoints;
		typedef std::array<ImU32, (unsigned)PaletteIndex::Max> Palette;


		struct RenderGeometry
		{

			RenderGeometry()
				: mValid(false),
				mCursorScreenPos(ImVec2(0, 0)),
				mScrollX(0),
				mScrollY(0),
				mFirstVisibleLineNo(0),
				mLastVisibleLineNo(0),
				mTotalLines(0),
				mWindowSize(ImVec2(0, 0)),
				mContentRegionAvail(ImVec2(0, 0)),
				mTextScreenPos(ImVec2(0, 0))
			{}

			bool mValid;
			ImVec2 mCursorScreenPos;
			float mScrollX;
			float mScrollY;
			int mFirstVisibleLineNo;
			int mLastVisibleLineNo;
			int mTotalLines;
			ImVec2 mWindowSize;
			ImVec2 mContentRegionAvail;
			ImVec2 mTextScreenPos;
		};

		TerminalView(TerminalData& aTerminalData, TerminalState& aTerminalState, Options aOptions);
		~TerminalView();

		const Palette& GetPalette() const { return mPaletteBase; }
		void SetPalette(const Palette& aValue);

		void SetErrorMarkers(const ErrorMarkers& aMarkers) { mErrorMarkers = aMarkers; }
		void SetBreakpoints(const Breakpoints& aMarkers) { mBreakpoints = aMarkers; }

		void Render(const char* aTitle, const ImVec2& aSize = ImVec2(), bool aBorder = false);

		std::vector<std::string> GetTextLines() const;

		std::string GetSelectedText() const;
		std::string GetCurrentLineText()const;

		int GetTotalLines() const { return (int)mLines.size(); }

		bool IsTextChanged() const { return mData.IsTextChanged(); }
		bool IsCursorPositionChanged() const { return mCursorPositionChanged; }

		bool IsColorizerEnabled() const { return mColorizerEnabled; }
		void SetColorizerEnable(bool aValue);

		Coordinates GetCursorPosition() const { return GetActualCursorCoordinates(); }
		void SetCursorPosition(const Coordinates& aPosition);

		inline void SetHandleMouseInputs(bool aValue) { mHandleMouseInputs = aValue; }
		inline bool IsHandleMouseInputsEnabled() const { return mHandleKeyboardInputs; }

		inline void SetHandleKeyboardInputs(bool aValue) { mHandleKeyboardInputs = aValue; }
		inline bool IsHandleKeyboardInputsEnabled() const { return mHandleKeyboardInputs; }

		inline void SetImGuiChildIgnored(bool aValue) { mIgnoreImGuiChild = aValue; }
		inline bool IsImGuiChildIgnored() const { return mIgnoreImGuiChild; }

		inline void SetShowWhitespaces(bool aValue) { mShowWhitespaces = aValue; }
		inline bool IsShowingWhitespaces() const { return mShowWhitespaces; }




		void SetCursorToEnd();

		void MoveUp(int aAmount = 1, bool aSelect = false);
		void MoveDown(int aAmount = 1, bool aSelect = false);
		void MoveLeft(int aAmount = 1, bool aSelect = false, bool aWordMode = false);
		void MoveRight(int aAmount = 1, bool aSelect = false, bool aWordMode = false);
		void MoveTop(bool aSelect = false);
		void MoveBottom(bool aSelect = false);
		void MoveHome(bool aSelect = false);
		void MoveEnd(bool aSelect = false);

		void SetSelectionStart(const Coordinates& aPosition);
		void SetSelectionEnd(const Coordinates& aPosition);
		void SetSelection(const Coordinates& aStart, const Coordinates& aEnd, SelectionMode aMode = SelectionMode::Normal);
		void SelectWordUnderCursor();
		void SelectAll();
		bool HasSelection() const;

		bool IsLastLineVisible();
		const RenderGeometry& GetLastRenderGeometry();

		void Copy();
		void Cut();
		void Paste();
		void Delete();

		static const Palette& GetDarkPalette();
		static const Palette& GetLightPalette();
		static const Palette& GetRetroBluePalette();

		void AddKeyboardInput(std::string input);
		void AddKeyboardInput(std::u16string input);
		void AddKeyboardInput(char input);
		void AddKeyboardInput(const char* input);


		ImWchar GetKeyboardInput();
		bool KeyboardInputAvailable();

		inline Options GetOptions() { return mOptions; }
		void SetOptions(const Options& aOptions) { mOptions = aOptions; }

	private:
		typedef std::vector<std::pair<std::regex, PaletteIndex>> RegexList;

		struct UiState
		{
			Coordinates mSelectionStart;
			Coordinates mSelectionEnd;
			Coordinates mCursorPosition;	// The UI Cursor, not to be confused with the real terminal cursor location
		};

		float TextDistanceToLineStart(const Coordinates& aFrom) const;
		void EnsureCursorVisible();
		int GetPageSize() const;

		Coordinates GetActualCursorCoordinates() const;
		Coordinates SanitizeCoordinates(const Coordinates& aValue) const;
		void Advance(Coordinates& aCoordinates) const;

		Coordinates ScreenPosToCoordinates(const ImVec2& aPosition) const;
		Coordinates FindWordStart(const Coordinates& aFrom) const;
		Coordinates FindWordEnd(const Coordinates& aFrom) const;
		Coordinates FindNextWord(const Coordinates& aFrom) const;
		bool IsOnWordBoundary(const Coordinates& aAt) const;

		void EnterCharacter(ImWchar aChar, bool aShift);
		void Backspace();
		void DeleteSelection();
		std::string GetWordUnderCursor() const;
		std::string GetWordAt(const Coordinates& aCoords) const;
		ImU32 GetGlyphColor(const Glyph& aGlyph) const;

		void HandleKeyboardInputs();
		void HandleMouseInputs();
		void Render();

		void InputGlyph(Line& line, int& termColI, PaletteIndex pi, uint8_t aValue);

		float mLineSpacing;
		const Lines& mLines;
		TerminalData& mData;

		UiState mUiState;

		bool mWithinRender;
		bool mScrollToCursor;
		bool mScrollToTop;

		bool mColorizerEnabled;
		float mTextStart;                   // position (in pixels) where a code line starts relative to the left of the TextEditor.
		int  mLeftMargin;
		bool mCursorPositionChanged;
		int mColorRangeMin, mColorRangeMax;
		SelectionMode mSelectionMode;
		bool mHandleKeyboardInputs;
		bool mHandleMouseInputs;
		bool mIgnoreImGuiChild;
		bool mShowWhitespaces;

		Palette mPaletteBase;
		Palette mPalette;

		Breakpoints mBreakpoints;
		ErrorMarkers mErrorMarkers;
		ImVec2 mCharAdvance;
		Coordinates mInteractiveStart, mInteractiveEnd;
		std::string mLineBuffer;
		uint64_t mStartTime;

		float mLastClick;

		RenderGeometry mLastRenderGeometry;
		std::queue<ImVector<ImWchar>> mQueuedInputQueueCharacters;
		std::queue<ImWchar> mKeyboardInputQueue;

		TerminalState& mTermState;

		Options mOptions;


	};
}