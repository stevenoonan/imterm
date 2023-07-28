#include <atomic>
#include <assert.h>
#include "terminal_logger.h"

namespace imterm {

	TerminalLogger::TerminalLogger() : TerminalLogger(true, "", ".log", GetDefaultLogPath(), Options()) { }

	TerminalLogger::TerminalLogger(std::string aFileNamePostfix) : TerminalLogger(true, aFileNamePostfix, ".log", GetDefaultLogPath(), Options()) { }

	TerminalLogger::TerminalLogger(Options aOptions) : TerminalLogger(true, "", ".log", GetDefaultLogPath(), aOptions) { }

	TerminalLogger::TerminalLogger(std::string aFileNamePostfix, Options aOptions) : TerminalLogger(true, aFileNamePostfix, ".log", GetDefaultLogPath(), aOptions) { }

	TerminalLogger::TerminalLogger(
		bool aUsePrefixTimestamp,
		std::string aFileNamePostfix,
		std::string aFileNameExtension = ".log",
		std::filesystem::path aPathWithoutFileName=GetDefaultLogPath(),
		Options aOptions = Options()
	) : mUsePrefixTimestamp(aUsePrefixTimestamp), mFileNamePostfix(aFileNamePostfix), mFileNameExtension(aFileNameExtension), mBasePath(aPathWithoutFileName)
	{
		SetOptions(aOptions);
	}

	TerminalLogger::~TerminalLogger()
	{
		Close();
	}

	void TerminalLogger::RegisterLogClosingWatcher(LogClosingCallback callback) {
		mLogClosingWatchers.push_back(std::move(callback));
	}

	bool TerminalLogger::DeregisterLogClosingWatcher(LogClosingCallback callback)
	{
		const size_t originalSize = mLogClosingWatchers.size();

		mLogClosingWatchers.erase(
			std::remove_if(mLogClosingWatchers.begin(), mLogClosingWatchers.end(),
				[callback](const LogClosingCallback& storedCallback) {
					return callback.target<void()>() == storedCallback.target<void()>();
				}),
			mLogClosingWatchers.end());

		return mLogClosingWatchers.size() < originalSize;
	}

	std::string TerminalLogger::GetDateTimeNowString() {
		// Get the current time
		auto now = std::chrono::system_clock::now();

		// Convert it to time_t
		std::time_t time = std::chrono::system_clock::to_time_t(now);

		// Format the time as UTC string
		std::ostringstream oss;
		oss << std::put_time(std::gmtime(&time), "%Y%d%m_%H%M%S");
		return oss.str();
	}

	std::filesystem::path TerminalLogger::GetDefaultLogPath() {
		std::filesystem::path path = GetUserProfilePath();
		path /= "imterm";
		path /= "logs";
		return path;
	}

	std::filesystem::path TerminalLogger::GetUserProfilePath() {

#if defined(_WIN32)
		// On Windows, use the USERPROFILE environment variable
		const char* userProfile = std::getenv("USERPROFILE");
		if (userProfile) {
			return std::filesystem::path(userProfile);
		}
#elif defined(__linux__) || defined(__APPLE__)
		// On Linux and macOS, use the HOME environment variable
		const char* homeDir = std::getenv("HOME");
		if (homeDir) {
			return std::filesystem::path(homeDir);
		}
#endif
		else {
			return std::filesystem::current_path();
		}
	}

	void TerminalLogger::SetUsePrefixTimestamp(bool aUsePrefixTimestamp) {
		if (aUsePrefixTimestamp != mUsePrefixTimestamp) {
			mUsePrefixTimestamp = aUsePrefixTimestamp;
			Close();
		}
	}

	void TerminalLogger::SetPostfix(const std::string& aPostfix) {
		if (aPostfix != mFileNamePostfix) {
			mFileNamePostfix = aPostfix;
			Close();
		}
	}

	void TerminalLogger::SetBasePath(const std::filesystem::path& aBasePath) {
		if (aBasePath != mBasePath) {
			mBasePath = aBasePath;
			Close();
		}
	}

	// Function to trigger the LogClosing event using a lambda
	void TerminalLogger::Close() {

		// Call the watchers first. They may call Log() which can open mOutput
		for (const auto& watcher : mLogClosingWatchers) {
			watcher();
		}

		if (mOutput.get() && mOutput->is_open()) {
			mOutput->flush();
			mOutput->close();
			
		}
	}

	void TerminalLogger::Log(Line& aLine, int aLineNumber) {

		static std::atomic_flag reentrantFlag = ATOMIC_FLAG_INIT;

		if (!mOptions.Enabled) return;

		if (reentrantFlag.test_and_set()) {
			std::cerr << "TerminalLogger::Log reentrancy, skipping...\n";
			assert(0);
			return;
		}


		if (!mOutput.get() || (mOutput.get() && !mOutput->is_open())) {

			mLogFilePath = mBasePath;
			mLogFilePath /= ""; // Concatenate with an empty path to add a trailing slash
			std::filesystem::create_directories(mLogFilePath);
			if (mUsePrefixTimestamp) {
				mLogFilePath += GetDateTimeNowString();
				if (mFileNamePostfix != "") {
					mLogFilePath += std::string("_");
				}
			}
			if (mFileNamePostfix != "") {
				mLogFilePath += mFileNamePostfix;
			}
			mLogFilePath += mFileNameExtension;

			mOutput = std::make_unique<std::ofstream>(mLogFilePath, std::ios::app | std::ios::ate);

		}

		if (mOutput.get() && mOutput->is_open()) {
			std::ostringstream oss;

			if (mOptions.LineNumbers) {
				oss << aLineNumber << " ";
			}

			if (mOptions.TimeStamps) {
				std::time_t time_t_timestamp = std::chrono::system_clock::to_time_t(aLine.getTimestamp());
				std::tm* time_tm_timestamp = std::localtime(&time_t_timestamp);
				if (time_tm_timestamp) {
					oss << std::put_time(time_tm_timestamp, "%H:%M:%S ");
				}
				else {
					oss << "00:00:00";
				}
			}

			if (aLine.size() > 0) {
				for (Glyph g : aLine) {
					oss << g.mChar;
				}
			}

			*mOutput.get() << oss.str() << "\n";
			mOutput->flush();
		}

		reentrantFlag.clear();

	}

}