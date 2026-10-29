#include "Logger.h"
#include <windows.h>
#include <fstream>
#include <mutex>

static std::wstring g_logFile;
static std::mutex g_mutex;

void Logger::Init(const std::wstring& logFile) {
    // Временно отключено
}

void Logger::Log(const std::wstring& message) {
    // Ничего не пишем в файл
}

void Logger::LogEvent(const std::wstring& message) {
    // Ничего не пишем ни в файл, ни в системный Event Log
}