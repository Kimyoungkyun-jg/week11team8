#include "EnginePCH.h"
#include "EngineLog.h"

namespace
{
    TArray<ILogSink*>& GetSinks()
    {
        static TArray<ILogSink*> Sinks;
        return Sinks;
    }
}

void FLog::AddSink(ILogSink* Sink)
{
    if (Sink)
        GetSinks().Add(Sink);
}

void FLog::RemoveSink(ILogSink* Sink)
{
    TArray<ILogSink*>& Sinks = GetSinks();
    for (int32 Index = Sinks.Num() - 1; Index >= 0; --Index)
        if (Sinks[Index] == Sink)
            Sinks.RemoveAt(Index, 1);
}

FFileLogSink::FFileLogSink(const FString& InPath)
{
    const std::filesystem::path Path(InPath);
    std::error_code Error;
    if (Path.has_parent_path())
        std::filesystem::create_directories(Path.parent_path(), Error);
    File.open(Path, std::ios::out | std::ios::trunc);
}

void FFileLogSink::OnLog(ELogVerbosity Verbosity, const FString& Message)
{
    if (!File.is_open())
        return;

    const char* Label = "Info";
    switch (Verbosity)
    {
    case ELogVerbosity::Input:   Label = "Input"; break;
    case ELogVerbosity::Warning: Label = "Warning"; break;
    case ELogVerbosity::Error:   Label = "Error"; break;
    default: break;
    }

    std::lock_guard Lock(Mutex);
    File << '[' << Label << "] " << Message << '\n';
    File.flush();
}

void FLog::Emit(ELogVerbosity Verbosity, const FString& Message)
{
    for (ILogSink* Sink : GetSinks())
        Sink->OnLog(Verbosity, Message);
}