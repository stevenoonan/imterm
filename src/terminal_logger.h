#pragma once

#include <filesystem>
#include <iostream>
#include <fstream>
#include <chrono>
#include <vector>
#include <functional>

#include "terminal_types.h"

namespace imterm {

	class TerminalLogger {

	public:

		using LogClosingCallback = std::function<void()>;



		struct Options {
			bool Enabled = true;
			bool LineNumbers = true;
			bool TimeStamps = true;
		};

		TerminalLogger();
		TerminalLogger(std::string aFileNamePostfix);
		TerminalLogger(Options aOptions);
		TerminalLogger(std::string aFileNamePostfix, Options aOptions);
		TerminalLogger(
			bool aUsePrefixTimestamp,
			std::string aFileNamePostfix,
			std::string aFileNameExtension,
			std::filesystem::path aPathWithoutFileName,
			Options aOptions
		);
		~TerminalLogger();

		void Log(Line& aLine, int aLineNumber);

		inline Options GetOptions() { return mOptions; }
		void SetOptions(const Options& aOptions) { mOptions = aOptions; }

		inline bool GetUsePrefixTimestamp() { return mUsePrefixTimestamp; }
		void SetUsePrefixTimestamp(bool aUsePrefixTimestamp);
		
		inline std::string GetPostfix() { return mFileNamePostfix; }
		void SetPostfix(const std::string& aPostfix);
		

		inline std::filesystem::path GetBasePath() { return mBasePath; }
		void SetBasePath(const std::filesystem::path& aLogFilePath);
		

		static std::string GetDateTimeNowString();
		static std::filesystem::path GetUserProfilePath();
		static std::filesystem::path GetDefaultLogPath();

		// Function to register a watcher for the LogClosing event using a lambda
		void RegisterLogClosingWatcher(LogClosingCallback callback);

		bool DeregisterLogClosingWatcher(LogClosingCallback callback);

		void Close();

	private:

		bool mUsePrefixTimestamp = true;
		std::string mFileNamePostfix;
		std::string mFileNameExtension;
		std::filesystem::path mBasePath;

		std::filesystem::path mLogFilePathPending;
		std::filesystem::path mLogFilePath;
		//std::ofstream * mLogger;
		std::unique_ptr<std::ofstream> mOutput = nullptr;

		Options mOptions;

		//bool mNewLogFile = true;

		std::vector<LogClosingCallback> mLogClosingWatchers;
	};

}