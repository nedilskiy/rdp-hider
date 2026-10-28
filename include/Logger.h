#pragma once
#include <string>

class Logger {
public:
    static void Init(const std::wstring& logFile);
    static void Log(const std::wstring& message);
    static void LogEvent(const std::wstring& message); // Writes to Windows Event Log
};