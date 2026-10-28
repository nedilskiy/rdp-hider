#include "Logger.h"
#include <windows.h>
#include <fstream>
#include <mutex>

static std::wstring g_logFile;
static std::mutex g_mutex;

void Logger::Init(const std::wstring& logFile) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_logFile = logFile;
}

void Logger::Log(const std::wstring& message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_logFile.empty()) return;

    // Open in append mode
    std::wofstream file(g_logFile, std::ios::app);
    if (file.is_open()) {
        file << message << L"\n";
    }
}

void Logger::LogEvent(const std::wstring& message) {
    // Write to our local file as well
    Log(L"[EVENT] " + message);

    // Write to Windows Application Event Log
    HANDLE hEventLog = RegisterEventSourceW(NULL, L"RdpSessionManager");
    if (hEventLog) {
        LPCWSTR messages[] = { message.c_str() };
        ReportEventW(hEventLog, EVENTLOG_INFORMATION_TYPE, 0, 0, NULL, 1, 0, messages, NULL);
        DeregisterEventSource(hEventLog);
    }
}