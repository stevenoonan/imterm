#include "terminal_command.h"

#include <algorithm>

namespace imterm {

	namespace {

		int ParameterOrDefault(
			const std::vector<int>& aParameters, size_t aIndex, int aDefault)
		{
			if (aIndex >= aParameters.size() || aParameters[aIndex] == 0) {
				return aDefault;
			}
			return aParameters[aIndex];
		}

		std::optional<TerminalCommand> DecodeMove(
			const std::vector<int>& aParameters,
			MoveCursor::Direction aDirection,
			bool aMoveToLineStart = false)
		{
			if (aParameters.size() > 1) {
				return std::nullopt;
			}
			return MoveCursor{
				aDirection,
				ParameterOrDefault(aParameters, 0, 1),
				aMoveToLineStart};
		}

	}

	std::optional<TerminalCommand> DecodeTerminalCommand(
		const EscapeSequenceParser::ParseResult& aSequence)
	{
		using Error = EscapeSequenceParser::Error;
		using Identifier = EscapeSequenceParser::EscapeIdentifier;
		using Mode = EscapeSequenceParser::Mode;
		using Stage = EscapeSequenceParser::Stage;

		if (aSequence.mOutputChar != 0
			|| aSequence.mStage != Stage::Inactive
			|| aSequence.mError != Error::None
			|| aSequence.mMode != Mode::None) {
			return std::nullopt;
		}

		const std::vector<int>& parameters = aSequence.mCommandData;
		switch (aSequence.mIdentifier) {
		case Identifier::A_MoveCursorUp:
			return DecodeMove(parameters, MoveCursor::Direction::Up);
		case Identifier::B_MoveCursorDown:
			return DecodeMove(parameters, MoveCursor::Direction::Down);
		case Identifier::C_MoveCursorRight:
			return DecodeMove(parameters, MoveCursor::Direction::Right);
		case Identifier::D_MoveCursorLeft:
			return DecodeMove(parameters, MoveCursor::Direction::Left);
		case Identifier::E_MoveCursorDownBeginning:
			return DecodeMove(
				parameters, MoveCursor::Direction::Down, true);
		case Identifier::F_MoveCursorUpBeginning:
			return DecodeMove(
				parameters, MoveCursor::Direction::Up, true);
		case Identifier::H_MoveCursor:
		case Identifier::f_MoveCursor:
			if (parameters.size() > 2) {
				return std::nullopt;
			}
			return SetCursorPosition{ScreenPosition{
				ParameterOrDefault(parameters, 0, 1) - 1,
				ParameterOrDefault(parameters, 1, 1) - 1}};
		case Identifier::G_MoveCursorCol:
			if (parameters.size() > 1) {
				return std::nullopt;
			}
			return SetCursorColumn{
				ParameterOrDefault(parameters, 0, 1) - 1};
		case Identifier::s_SaveCursorPosition:
			return parameters.empty()
				? std::optional<TerminalCommand>(SaveCursor{})
				: std::nullopt;
		case Identifier::u_RestoreCursorPosition:
			return parameters.empty()
				? std::optional<TerminalCommand>(RestoreCursor{})
				: std::nullopt;
		case Identifier::J_EraseDisplay:
			if (parameters.size() > 1) {
				return std::nullopt;
			}
			switch (ParameterOrDefault(parameters, 0, 0)) {
			case 0: return EraseDisplay{EraseDisplay::Area::AfterCursor};
			case 1: return EraseDisplay{EraseDisplay::Area::BeforeCursor};
			case 2: return EraseDisplay{EraseDisplay::Area::All};
			case 3: return EraseDisplay{EraseDisplay::Area::Scrollback};
			default: return std::nullopt;
			}
		case Identifier::K_EraseLine:
			if (parameters.size() > 1) {
				return std::nullopt;
			}
			switch (ParameterOrDefault(parameters, 0, 0)) {
			case 0: return EraseLine{EraseLine::Area::AfterCursor};
			case 1: return EraseLine{EraseLine::Area::BeforeCursor};
			case 2: return EraseLine{EraseLine::Area::All};
			default: return std::nullopt;
			}
		case Identifier::m_SetGraphics:
			return SetGraphics{
				parameters.empty() ? std::vector<int>{0} : parameters};
		case Identifier::n_RequestReport:
			if (parameters.size() != 1) {
				return std::nullopt;
			}
			if (parameters[0] == 5) {
				return RequestStatusReport{
					RequestStatusReport::Kind::DeviceStatus};
			}
			if (parameters[0] == 6) {
				return RequestStatusReport{
					RequestStatusReport::Kind::CursorPosition};
			}
			return std::nullopt;
		default:
			return std::nullopt;
		}
	}

}
