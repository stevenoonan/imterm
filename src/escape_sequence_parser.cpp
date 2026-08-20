

#include "escape_sequence_parser.h"

EscapeSequenceParser::EscapeSequenceParser()
    : mStage(Stage::Inactive),
      mError(Error::NotReady),
      mMode(Mode::None),
      mIdentifier(EscapeIdentifier::Undefined),
      mParseResult{}
{
}

void EscapeSequenceParser::ResetForNextByte()
{
	mIdentifier = EscapeIdentifier::Undefined;
	mMode = Mode::None;
	mDataStaged.clear();
	mDataElementInProcess = 0;
	mDataElementDigits = 0;
	mSawDataSeparator = false;
	mSequenceLength = 0;
	mError = Error::NotReady;
	mParseResult = {};
	mParseResult.mError = Error::NotReady;
	mParseResult.mIdentifier = EscapeIdentifier::Undefined;
	mParseResult.mMode = Mode::None;
}

const EscapeSequenceParser::ParseResult& EscapeSequenceParser::Parse(uint8_t input)
{
	if (mStage == Stage::Inactive) {
		ResetForNextByte();
		mStage = Stage::GetEsc;
	}

	if (mStage != Stage::GetEsc && input == ESC) {
		ResetForNextByte();
		mStage = Stage::GetCsi;
		mSequenceLength = 1;
		mParseResult.mStage = mStage;
		return mParseResult;
	}

	if (mStage != Stage::GetEsc && ++mSequenceLength > MaxSequenceLength) {
		Fail(Error::SequenceTooLong);
		return mParseResult;
	}

	switch (mStage) {
	case Stage::GetEsc:
		if (input == ESC) {
			mStage = Stage::GetCsi;
			mSequenceLength = 1;
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
			Fail(Error::BadCsi);
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
			if (mDataElementDigits >= MaxArgumentDigits) {
				Fail(Error::ArgumentTooLong);
				break;
			}

			const int digit = input - '0';
			if (mDataElementInProcess > (MaxNumericValue - digit) / 10) {
				Fail(Error::NumericOverflow);
				break;
			}

			mDataElementInProcess = (mDataElementInProcess * 10) + digit;
			++mDataElementDigits;
		}
		else if (input == ';') {
			mSawDataSeparator = true;
			StageDataElement(true);
		}
		else {
			if ((input >= 'A' && input <= 'Z') || (input >= 'a' && input <= 'z')) {
				if (!StageDataElement(true)) {
					break;
				}
				mIdentifier = static_cast<EscapeIdentifier>(input);
				mError = Error::None;
				mParseResult.mIdentifier = mIdentifier;
				mParseResult.mCommandData = mDataStaged;
				mParseResult.mMode = mMode;
			}
			else {
				Fail(Error::BadData);
				break;
			}
			mStage = Stage::Inactive;
		}
		break;

	case Stage::Inactive:
		break;
	}

	mParseResult.mStage = mStage;
	mParseResult.mError = mError;

	return mParseResult;
}

bool EscapeSequenceParser::StageDataElement(bool aIsFinalElement)
{
	if (mDataElementDigits == 0 && (!aIsFinalElement || !mSawDataSeparator)) {
		return true;
	}

	if (mDataStaged.size() >= MaxArguments) {
		Fail(Error::TooManyArguments);
		return false;
	}

	mDataStaged.push_back(
		mDataElementDigits == 0 ? 0 : mDataElementInProcess);
	mDataElementInProcess = 0;
	mDataElementDigits = 0;
	return true;
}

void EscapeSequenceParser::Fail(Error error)
{
	mStage = Stage::Inactive;
	mError = error;
	mParseResult.mOutputChar = 0;
	mParseResult.mIdentifier = EscapeIdentifier::Undefined;
	mParseResult.mMode = Mode::None;
	mParseResult.mCommandData.clear();
	mParseResult.mStage = mStage;
	mParseResult.mError = mError;
}
