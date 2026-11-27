#pragma once

#include <string>
#include <list>
#include <functional>

void LogMessage(const std::string& message);
void LogUnimplemented(const std::string& message);

struct LogMessageLine
{
	float Time = 0.0f;
	std::string Source;
	std::string Text;
};

class Logger
{
public:
	static Logger* Get();

	void LogMessage(const std::string& message);
	void LogUnimplemented(const std::string& message);
	void SetTimeSeconds(float value) { time = value; }
	void SetCallback(std::function<void(const LogMessageLine& line)> cb) { printLogDebugger = std::move(cb); }
	const std::list<LogMessageLine>& GetLog() const { return Log; }

	void SaveLog(const std::string& filename);
	void SaveLogAsPlaintext(const std::string& filename) const;
	static std::list<LogMessageLine> LoadLog(const std::string& filename);

private:
	float time = 0.0f;
	std::list<LogMessageLine> Log;
	// Bounded memory: keep startup lines (tests parse them from SE-Log-LastRun)
	// and the newest lines; drop the middle once the cap is reached.
	static constexpr size_t KeepHeadLines = 5000, MaxLines = 50000;
	std::list<LogMessageLine>::iterator trimAt;
	bool trimming = false;
	size_t droppedLines = 0;
	void Append(LogMessageLine line);
	std::function<void(const LogMessageLine& line)> printLogDebugger;
	static std::vector<std::string> SplitNewlines(const std::string& str);

	Logger() = default;
	Logger(const Logger&) = delete;
	Logger& operator=(const Logger&) = delete;
};
