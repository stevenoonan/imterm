#include <algorithm>
#include <chrono>
#include <string>
#include <regex>
#include <cmath>
#include <stdexcept>
#include <future>
#include <queue>
#include <algorithm>
#include <iterator>

#include "terminal_view.h"
#include "escape_sequence_parser.h"
#include "coordinates.h"
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

TerminalView::TerminalView(TerminalData& aTerminalData, TerminalState& aTerminalState)
	: mLineSpacing(1.0f)
	, mWithinRender(false)
	, mScrollToCursor(false)
	, mScrollToTop(false)
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
	, mData(aTerminalData)
	, mLines(aTerminalData.mLines)
	, mTermState(aTerminalState)
{
	SetPalette(GetDarkPalette());
}

TerminalView::~TerminalView()
{
}


void TerminalView::SetPalette(const Palette & aValue)
{
	mPaletteBase = aValue;
}



Coordinates TerminalView::GetActualCursorCoordinates() const
{
	return SanitizeCoordinates(mUiState.mCursorPosition);
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
			column = mData.GetLineMaxColumn(line);
		}
		return Coordinates(line, column);
	}
	else
	{
		column = mLines.empty() ? 0 : std::min(column, mData.GetLineMaxColumn(line));
		return Coordinates(line, column);
	}
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
		auto cindex = mData.GetCharacterIndex(aCoordinates);

		if (cindex + 1 < (int)line.size())
		{
			auto delta = TerminalData::UTF8CharLength(line[cindex].mChar);
			cindex = std::min(cindex + delta, (int)line.size() - 1);
		}
		else
		{
			++aCoordinates.mLine;
			cindex = 0;
		}
		aCoordinates.mColumn = mData.GetCharacterColumn(aCoordinates.mLine, cindex);
	}
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
				float newColumnX = (1.0f + std::floor((1.0f + columnX) / (float(mData.GetTabSize()) * spaceSize))) * (float(mData.GetTabSize()) * spaceSize);
				columnWidth = newColumnX - oldX;
				if (mTextStart + columnX + columnWidth * 0.5f > local.x)
					break;
				columnX = newColumnX;
				columnCoord = (columnCoord / mData.GetTabSize()) * mData.GetTabSize() + mData.GetTabSize();
				columnIndex++;
			}
			else
			{
				char buf[7];
				auto d = TerminalData::UTF8CharLength(line[columnIndex].mChar);
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
	auto cindex = mData.GetCharacterIndex(at);

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
	return Coordinates(at.mLine, mData.GetCharacterColumn(at.mLine, cindex));
}

Coordinates TerminalView::FindWordEnd(const Coordinates & aFrom) const
{
	Coordinates at = aFrom;
	if (at.mLine >= (int)mLines.size())
		return at;

	auto& line = mLines[at.mLine];
	auto cindex = mData.GetCharacterIndex(at);

	if (cindex >= (int)line.size())
		return at;

	bool prevspace = (bool)isspace(line[cindex].mChar);
	auto cstart = (TerminalData::PaletteIndex)line[cindex].mColorIndex;
	while (cindex < (int)line.size())
	{
		auto c = line[cindex].mChar;
		auto d = TerminalData::UTF8CharLength(c);
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
	return Coordinates(aFrom.mLine, mData.GetCharacterColumn(aFrom.mLine, cindex));
}

Coordinates TerminalView::FindNextWord(const Coordinates & aFrom) const
{
	Coordinates at = aFrom;
	if (at.mLine >= (int)mLines.size())
		return at;

	// skip to the next non-word character
	auto cindex = mData.GetCharacterIndex(aFrom);
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
			return Coordinates(l, mData.GetLineMaxColumn(l));
		}

		auto& line = mLines[at.mLine];
		if (cindex < (int)line.size())
		{
			isword = isalnum(line[cindex].mChar);

			if (isword && !skip)
				return Coordinates(at.mLine, mData.GetCharacterColumn(at.mLine, cindex));

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



bool TerminalView::IsOnWordBoundary(const Coordinates & aAt) const
{
	if (aAt.mLine >= (int)mLines.size() || aAt.mColumn == 0)
		return true;

	auto& line = mLines[aAt.mLine];
	auto cindex = mData.GetCharacterIndex(aAt);
	if (cindex >= (int)line.size())
		return true;

	if (mColorizerEnabled)
		return line[cindex].mColorIndex != line[size_t(cindex - 1)].mColorIndex;

	return isspace(line[cindex].mChar) != isspace(line[cindex - 1].mChar);
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

	auto istart = mData.GetCharacterIndex(start);
	auto iend = mData.GetCharacterIndex(end);

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


		io.WantCaptureKeyboard = true;
		io.WantTextInput = true;

		static const auto* input_delete = "\x1B[3~";
		static const auto* input_backspace = "\x7F";
		static const auto* input_enter = "\n";
		static const auto* input_tab = "\t";
		static const auto* input_up_arrow = "\x1B[A";
		static const auto* input_down_arrow = "\x1B[B";
		static const auto* input_right_arrow = "\x1B[C";
		static const auto* input_left_arrow = "\x1B[D";


		/* handle Delete, Backspace, Enter / Return, Tab */
		if (!ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete))) {
			AddKeyboardInput(input_delete);
		}
		else if (!ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Backspace))) {
			AddKeyboardInput(input_backspace);
		}
		else if (!ctrl && shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Insert)))
			Paste();
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_V)))
			Paste();
		else if (!ctrl && !shift && !alt && (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter)) || ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_KeypadEnter))))
			AddKeyboardInput(input_enter);
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Tab)))
			AddKeyboardInput(input_tab);


		if (!ctrl && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_UpArrow))) {
			AddKeyboardInput(input_up_arrow);
		}
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_DownArrow))) {
			AddKeyboardInput(input_down_arrow);
		} 
		else if (!alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_LeftArrow))) {
			AddKeyboardInput(input_left_arrow);
		} 
		else if (!alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_RightArrow))) {
			AddKeyboardInput(input_right_arrow);

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


		if (!io.InputQueueCharacters.empty())
		{
			for (auto& elem : io.InputQueueCharacters) {
				mKeyboardInputQueue.push(std::move(elem));
				
			}
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
					mUiState.mCursorPosition = mInteractiveStart = mInteractiveEnd = ScreenPosToCoordinates(ImGui::GetMousePos());
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
					mUiState.mCursorPosition = mInteractiveStart = mInteractiveEnd = ScreenPosToCoordinates(ImGui::GetMousePos());
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
				mUiState.mCursorPosition = mInteractiveStart = mInteractiveEnd = ScreenPosToCoordinates(ImGui::GetMousePos());
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
				mUiState.mCursorPosition = mInteractiveEnd = ScreenPosToCoordinates(ImGui::GetMousePos());
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

	int termRowMaxI = std::max((int)ceil(thisRenderGeometry.mContentRegionAvail.y / mCharAdvance.y) - 1, 0);
	int termColMaxI = std::max((int)ceil((thisRenderGeometry.mContentRegionAvail.x - thisRenderGeometry.mTextScreenPos.x) / mCharAdvance.x) - 1, 0);
	mTermState.SetBounds(Coordinates(termRowMaxI, termColMaxI));
	

	// Deduce mTextStart by evaluating mLines size (global lineMax) plus two spaces as text width
	static const int buf_length = 48;
	char buf[buf_length];
	int globalLineMaxDigits = 0;
	int digitCountTemp = globalLineMax;
	while (digitCountTemp) {
		digitCountTemp /= 10;
		globalLineMaxDigits++;
	}
	const char* marginStringFormat = "%0*d %02d:%02d:%02d ";
	int snpf_len = snprintf(buf, buf_length, marginStringFormat, globalLineMaxDigits, globalLineMax, 12, 12, 59);
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
			longest = std::max(mTextStart + TextDistanceToLineStart(Coordinates(lineNo, mData.GetLineMaxColumn(lineNo))), longest);
			auto columnNo = 0;
			Coordinates lineStartCoord(lineNo, 0);
			Coordinates lineEndCoord(lineNo, mData.GetLineMaxColumn(lineNo));

			// Draw selection for the current line
			float sstart = -1.0f;
			float ssend = -1.0f;

			assert(mUiState.mSelectionStart <= mUiState.mSelectionEnd);
			if (mUiState.mSelectionStart <= lineEndCoord)
				sstart = mUiState.mSelectionStart > lineStartCoord ? TextDistanceToLineStart(mUiState.mSelectionStart) : 0.0f;
			if (mUiState.mSelectionEnd > lineStartCoord)
				ssend = TextDistanceToLineStart(mUiState.mSelectionEnd < lineEndCoord ? mUiState.mSelectionEnd : lineEndCoord);

			if (mUiState.mSelectionEnd.mLine > lineNo)
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
			snprintf(buf, buf_length, marginStringFormat, globalLineMaxDigits, lineNo + 1, time_tm_timestamp->tm_hour, time_tm_timestamp->tm_min, time_tm_timestamp->tm_sec);

			auto lineNoWidth = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, buf, nullptr, nullptr).x;
			drawList->AddText(ImVec2(lineStartScreenPos.x + mTextStart - lineNoWidth, lineStartScreenPos.y), mPalette[(int)TerminalData::PaletteIndex::LineNumber], buf);

			mUiState.mCursorPosition = mTermState.getPositionRelative(mLines.size());

			if (mUiState.mCursorPosition.mLine == lineNo)
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
						auto cindex = mData.GetCharacterIndex(mUiState.mCursorPosition);
						float cx = TextDistanceToLineStart(mUiState.mCursorPosition);

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
					bufferOffset.x = (1.0f + std::floor((1.0f + bufferOffset.x) / (float(mData.GetTabSize()) * spaceSize))) * (float(mData.GetTabSize()) * spaceSize);
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
					auto l = TerminalData::UTF8CharLength(glyph.mChar);
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
	mData.SetTextChanged(false);
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

void TerminalView::SetColorizerEnable(bool aValue)
{
	mColorizerEnabled = aValue;
}

void TerminalView::SetCursorPosition(const Coordinates & aPosition)
{
	if (mUiState.mCursorPosition != aPosition)
	{
		mUiState.mCursorPosition = aPosition;
		mCursorPositionChanged = true;
		EnsureCursorVisible();
	}
}

void TerminalView::SetSelectionStart(const Coordinates & aPosition)
{
	mUiState.mSelectionStart = SanitizeCoordinates(aPosition);
	if (mUiState.mSelectionStart > mUiState.mSelectionEnd)
		std::swap(mUiState.mSelectionStart, mUiState.mSelectionEnd);
}

void TerminalView::SetSelectionEnd(const Coordinates & aPosition)
{
	mUiState.mSelectionEnd = SanitizeCoordinates(aPosition);
	if (mUiState.mSelectionStart > mUiState.mSelectionEnd)
		std::swap(mUiState.mSelectionStart, mUiState.mSelectionEnd);
}

void TerminalView::SetSelection(const Coordinates & aStart, const Coordinates & aEnd, SelectionMode aMode)
{
	auto oldSelStart = mUiState.mSelectionStart;
	auto oldSelEnd = mUiState.mSelectionEnd;

	mUiState.mSelectionStart = SanitizeCoordinates(aStart);
	mUiState.mSelectionEnd = SanitizeCoordinates(aEnd);
	if (mUiState.mSelectionStart > mUiState.mSelectionEnd)
		std::swap(mUiState.mSelectionStart, mUiState.mSelectionEnd);

	switch (aMode)
	{
	case TerminalView::SelectionMode::Normal:
		break;
	case TerminalView::SelectionMode::Word:
	{
		mUiState.mSelectionStart = FindWordStart(mUiState.mSelectionStart);
		if (!IsOnWordBoundary(mUiState.mSelectionEnd))
			mUiState.mSelectionEnd = FindWordEnd(FindWordStart(mUiState.mSelectionEnd));
		break;
	}
	case TerminalView::SelectionMode::Line:
	{
		const auto lineNo = mUiState.mSelectionEnd.mLine;
		const auto lineSize = (size_t)lineNo < mLines.size() ? mLines[lineNo].size() : 0;
		mUiState.mSelectionStart = Coordinates(mUiState.mSelectionStart.mLine, 0);
		mUiState.mSelectionEnd = Coordinates(lineNo, mData.GetLineMaxColumn(lineNo));
		break;
	}
	default:
		break;
	}

	if (mUiState.mSelectionStart != oldSelStart ||
		mUiState.mSelectionEnd != oldSelEnd)
		mCursorPositionChanged = true;
}




void TerminalView::SetCursorToEnd()
{
	auto line = (int)mLines.size() - 1;
	auto column = mData.GetLineMaxColumn(line);
	Coordinates pos(line, column);
	if (mUiState.mCursorPosition != pos)
	{
		mUiState.mCursorPosition = pos;
		mCursorPositionChanged = true;
		EnsureCursorVisible();
	}
}


void TerminalView::DeleteSelection()
{
	assert(mUiState.mSelectionEnd >= mUiState.mSelectionStart);

	if (mUiState.mSelectionEnd == mUiState.mSelectionStart)
		return;

	mData.DeleteRange(mUiState.mSelectionStart, mUiState.mSelectionEnd);

	SetSelection(mUiState.mSelectionStart, mUiState.mSelectionStart);
	SetCursorPosition(mUiState.mSelectionStart);
}

void TerminalView::MoveUp(int aAmount, bool aSelect)
{
	auto oldPos = mUiState.mCursorPosition;
	mUiState.mCursorPosition.mLine = std::max(0, mUiState.mCursorPosition.mLine - aAmount);
	if (oldPos != mUiState.mCursorPosition)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveStart)
				mInteractiveStart = mUiState.mCursorPosition;
			else if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mUiState.mCursorPosition;
			else
			{
				mInteractiveStart = mUiState.mCursorPosition;
				mInteractiveEnd = oldPos;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mUiState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);

		EnsureCursorVisible();
	}
}

void TerminalView::MoveDown(int aAmount, bool aSelect)
{
	assert(mUiState.mCursorPosition.mColumn >= 0);
	auto oldPos = mUiState.mCursorPosition;
	mUiState.mCursorPosition.mLine = std::max(0, std::min((int)mLines.size() - 1, mUiState.mCursorPosition.mLine + aAmount));

	if (mUiState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mUiState.mCursorPosition;
			else if (oldPos == mInteractiveStart)
				mInteractiveStart = mUiState.mCursorPosition;
			else
			{
				mInteractiveStart = oldPos;
				mInteractiveEnd = mUiState.mCursorPosition;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mUiState.mCursorPosition;
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

	auto oldPos = mUiState.mCursorPosition;
	mUiState.mCursorPosition = GetActualCursorCoordinates();
	auto line = mUiState.mCursorPosition.mLine;
	auto cindex = mData.GetCharacterIndex(mUiState.mCursorPosition);

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

		mUiState.mCursorPosition = Coordinates(line, mData.GetCharacterColumn(line, cindex));
		if (aWordMode)
		{
			mUiState.mCursorPosition = FindWordStart(mUiState.mCursorPosition);
			cindex = mData.GetCharacterIndex(mUiState.mCursorPosition);
		}
	}

	mUiState.mCursorPosition = Coordinates(line, mData.GetCharacterColumn(line, cindex));

	assert(mUiState.mCursorPosition.mColumn >= 0);
	if (aSelect)
	{
		if (oldPos == mInteractiveStart)
			mInteractiveStart = mUiState.mCursorPosition;
		else if (oldPos == mInteractiveEnd)
			mInteractiveEnd = mUiState.mCursorPosition;
		else
		{
			mInteractiveStart = mUiState.mCursorPosition;
			mInteractiveEnd = oldPos;
		}
	}
	else
		mInteractiveStart = mInteractiveEnd = mUiState.mCursorPosition;
	SetSelection(mInteractiveStart, mInteractiveEnd, aSelect && aWordMode ? SelectionMode::Word : SelectionMode::Normal);

	EnsureCursorVisible();
}

void TerminalView::MoveRight(int aAmount, bool aSelect, bool aWordMode)
{
	auto oldPos = mUiState.mCursorPosition;

	if (mLines.empty() || oldPos.mLine >= mLines.size())
		return;

	auto cindex = mData.GetCharacterIndex(mUiState.mCursorPosition);
	while (aAmount-- > 0)
	{
		auto lindex = mUiState.mCursorPosition.mLine;
		auto& line = mLines[lindex];

		if (cindex >= line.size())
		{
			if (mUiState.mCursorPosition.mLine < mLines.size() - 1)
			{
				mUiState.mCursorPosition.mLine = std::max(0, std::min((int)mLines.size() - 1, mUiState.mCursorPosition.mLine + 1));
				mUiState.mCursorPosition.mColumn = 0;
			}
			else
				return;
		}
		else
		{
			cindex += TerminalData::UTF8CharLength(line[cindex].mChar);
			mUiState.mCursorPosition = Coordinates(lindex, mData.GetCharacterColumn(lindex, cindex));
			if (aWordMode)
				mUiState.mCursorPosition = FindNextWord(mUiState.mCursorPosition);
		}
	}

	if (aSelect)
	{
		if (oldPos == mInteractiveEnd)
			mInteractiveEnd = SanitizeCoordinates(mUiState.mCursorPosition);
		else if (oldPos == mInteractiveStart)
			mInteractiveStart = mUiState.mCursorPosition;
		else
		{
			mInteractiveStart = oldPos;
			mInteractiveEnd = mUiState.mCursorPosition;
		}
	}
	else
		mInteractiveStart = mInteractiveEnd = mUiState.mCursorPosition;
	SetSelection(mInteractiveStart, mInteractiveEnd, aSelect && aWordMode ? SelectionMode::Word : SelectionMode::Normal);

	EnsureCursorVisible();
}

void TerminalView::MoveTop(bool aSelect)
{
	auto oldPos = mUiState.mCursorPosition;
	SetCursorPosition(Coordinates(0, 0));

	if (mUiState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			mInteractiveEnd = oldPos;
			mInteractiveStart = mUiState.mCursorPosition;
		}
		else
			mInteractiveStart = mInteractiveEnd = mUiState.mCursorPosition;
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
	auto oldPos = mUiState.mCursorPosition;
	SetCursorPosition(Coordinates(mUiState.mCursorPosition.mLine, 0));

	if (mUiState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveStart)
				mInteractiveStart = mUiState.mCursorPosition;
			else if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mUiState.mCursorPosition;
			else
			{
				mInteractiveStart = mUiState.mCursorPosition;
				mInteractiveEnd = oldPos;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mUiState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);
	}
}

void TerminalView::MoveEnd(bool aSelect)
{
	auto oldPos = mUiState.mCursorPosition;
	SetCursorPosition(Coordinates(mUiState.mCursorPosition.mLine, mData.GetLineMaxColumn(oldPos.mLine)));

	if (mUiState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mUiState.mCursorPosition;
			else if (oldPos == mInteractiveStart)
				mInteractiveStart = mUiState.mCursorPosition;
			else
			{
				mInteractiveStart = oldPos;
				mInteractiveEnd = mUiState.mCursorPosition;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mUiState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);
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
	return mUiState.mSelectionEnd > mUiState.mSelectionStart;
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
	Copy();
}

void TerminalView::Paste()
{
	auto clipText = ImGui::GetClipboardText();
	if (clipText != nullptr && strlen(clipText) > 0)
	{
		AddKeyboardInput(clipText);
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

void TerminalView::AddKeyboardInput(std::string input) {
	for (char c : input) {
		mKeyboardInputQueue.push(c);
	}
}


void TerminalView::AddKeyboardInput(std::u16string input) {
	for (char c : input) {
		mKeyboardInputQueue.push(c);
	}
}

void TerminalView::AddKeyboardInput(char input) {
	mKeyboardInputQueue.push(input);
}

void TerminalView::AddKeyboardInput(const char * input) {
	for (auto * p = input; *p != 0; ++p) {
		mKeyboardInputQueue.push(*p);
	}
}


ImWchar TerminalView::GetKeyboardInput()
{
	if (mKeyboardInputQueue.empty()) {
		throw std::underflow_error("No keyboard input to get. Check KeyboardInputAvailable() before calling.");
	}
	else {
		auto item = mKeyboardInputQueue.front();
		mKeyboardInputQueue.pop();
		return item;
	}
}

bool TerminalView::KeyboardInputAvailable()
{
	return !mKeyboardInputQueue.empty();
}


std::string TerminalView::GetSelectedText() const
{
	return mData.GetText(mUiState.mSelectionStart, mUiState.mSelectionEnd);
}

std::string TerminalView::GetCurrentLineText()const
{
	auto lineLength = mData.GetLineMaxColumn(mUiState.mCursorPosition.mLine);
	return mData.GetText(
		Coordinates(mUiState.mCursorPosition.mLine, 0),
		Coordinates(mUiState.mCursorPosition.mLine, lineLength));
}

float TerminalView::TextDistanceToLineStart(const Coordinates& aFrom) const
{
	auto& line = mLines[aFrom.mLine];
	float distance = 0.0f;
	float spaceSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, " ", nullptr, nullptr).x;
	int colIndex = mData.GetCharacterIndex(aFrom);
	for (size_t it = 0u; it < line.size() && it < colIndex; )
	{
		if (line[it].mChar == '\t')
		{
			distance = (1.0f + std::floor((1.0f + distance) / (float(mData.GetTabSize()) * spaceSize))) * (float(mData.GetTabSize()) * spaceSize);
			++it;
		}
		else
		{
			auto d = TerminalData::UTF8CharLength(line[it].mChar);
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
