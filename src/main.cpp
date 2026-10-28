#include "PowerMonitor.h"
#include "Logger.h"
#include <windows.h>

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    // Initialize logging (Creates log in the same dir as the exe)
    Logger::Init(L"RdpSessionManager.log");
    Logger::LogEvent(L"RdpSessionManager Service Started.");

    // Setup and start listening for power events
    PowerMonitor monitor;
    monitor.Start(); // Blocks and processes messages

    return 0;
}