

#include "escape_sequence_parser.h"

EscapeSequenceParser::EscapeSequenceParser() : mStage(Stage::Inactive), mError(Error::NotReady), mIdentifier(EscapeIdentifier::Undefined)
{
}

EscapeSequenceParser::~EscapeSequenceParser()
{
}

const EscapeSequenceParser::ParseResult& EscapeSequenceParser::Parse(uint8_t input)
{

	switch (mStage) {
	case Stage::Inactive:
		// Init / re-init status
		mStage = Stage::GetEsc;
		mIdentifier = EscapeIdentifier::Undefined;
		mMode = Mode::None;
		mDataStaged.clear();
		mDataElementInProcess.clear();
		mError = Error::NotReady;
		mParseResult.mOutputChar = 0;
		[[fallthrough]];

	case Stage::GetEsc:
		if (input == ESC) {
			mStage = Stage::GetCsi;
		}
		else {
			mStage = Stage::Inactive;
			mError = Error::BadEsc;
			mParseResult.mOutputChar = input;
		}
		break;

	case Stage::GetCsi:
		if (input == CSI) {
			mStage = Stage::GetMode;
		}
		else {
			mStage = Stage::Inactive;
			mError = Error::BadCsi;
		}
		break;

	case Stage::GetMode:

		mStage = Stage::GetData;

		if (input == '=') {
			mMode = Mode::Screen;
			break;
		}
		else if (input == '?') {
			mMode = Mode::Private;
			break;
		}
		else {
			[[fallthrough]];
		}		

	case Stage::GetData:
		if (input >= '0' && input <= '9') {
			mDataElementInProcess.push_back(input);
		}
		else if (input == ';') {
			ConvertDataElementInProcessToStagedInt();
		}
		else {
			if ((input >= 'A' && input <= 'Z') || (input >= 'a' && input <= 'z')) {
				ConvertDataElementInProcessToStagedInt();
				mIdentifier = static_cast<EscapeIdentifier>(input);
				mError = Error::None;
				mParseResult.mIdentifier = mIdentifier;
				mParseResult.mCommandData = mDataStaged;
				mParseResult.mMode = mMode;
			}
			else {
				mError = Error::BadData;
			}
			mStage = Stage::Inactive;
		}
		break;
	}

	mParseResult.mStage = mStage;
	mParseResult.mError = mError;

	return mParseResult;
}

void EscapeSequenceParser::ConvertDataElementInProcessToStagedInt()
{
	if (mDataElementInProcess.size() > 0) {
		std::string cstring(mDataElementInProcess.begin(), mDataElementInProcess.end());
		int iVal = std::stoi(cstring);
		mDataStaged.push_back(iVal);
		mDataElementInProcess.clear();
	}
}
