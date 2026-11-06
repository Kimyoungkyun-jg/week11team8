#pragma once

#include <fstream>
#include <mutex>

enum class ELogVerbosity
{
	Input,
	Info,
	Warning,
	Error
};

class ILogSink
{
public:
	virtual void OnLog(ELogVerbosity, const FString&) = 0;
};

// 로그를 파일에 남긴다. 에디터 로그는 프로젝트 루트 Logs에 저장하고 줄마다 flush한다.
class FFileLogSink : public ILogSink
{
public:
	explicit FFileLogSink(const FString& InPath);
	virtual void OnLog(ELogVerbosity Verbosity, const FString& Message) override;
	bool IsOpen() const { return File.is_open(); }

private:
	std::mutex Mutex;
	std::ofstream File;
};

class FLog
{
public:
    static void AddSink(ILogSink* Sink);
    static void RemoveSink(ILogSink* Sink);
    static void Emit(ELogVerbosity Verbosity, const FString& Message);

    template <typename... Args>
    static void Log(ELogVerbosity Verbosity, std::format_string<Args...> Fmt, Args&&... InArgs)
    {
        Emit(Verbosity, std::format(Fmt, std::forward<Args>(InArgs)...));
    }
};

#define HTR_LOG(Verbosity, ...) FLog::Log(ELogVerbosity::Verbosity, __VA_ARGS__)
