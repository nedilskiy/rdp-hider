#include "PowerMonitor.h"
#include "Logger.h"
#include <windows.h>


void EnableDebugPrivilege() {
    HANDLE hToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        TOKEN_PRIVILEGES tp;
        LUID luid;
        if (LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid)) {
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Luid = luid;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL);
        }
        CloseHandle(hToken);
    }
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    EnableDebugPrivilege();
    // Initialize logging (Creates log in the same dir as the exe)
    Logger::Init(L"RdpSessionManager.log");
    Logger::LogEvent(L"RdpSessionManager Service Started.");

    // Setup and start listening for power events
    PowerMonitor monitor;
    monitor.Start(); // Blocks and processes messages

    return 0;
}