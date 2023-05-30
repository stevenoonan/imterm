

#include "escape_sequence_parser.h"

EscapeSequenceParser::EscapeSequenceParser() : mStage(Stage::Inactive), mError(Error::NotReady)
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
		mIdentifier = 0;
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
			mStage = Stage::GetData;
		}
		else {
			mStage = Stage::Inactive;
			mError = Error::BadCsi;
		}
		break;

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
				mIdentifier = input;
				mError = Error::None;
				mParseResult.mIdentifier = mIdentifier;
				mParseResult.mCommandData = mDataStaged;
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
