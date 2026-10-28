#pragma once
#include <windows.h>
#include <vector>
#include <string>

class RdpController {
public:
    static void DisconnectAll();
    static bool LaunchSecureSession(const std::wstring& serverAddress);
    static void AutoTypeCredentials();
private:
    static std::vector<DWORD> GetProcessIds(const std::wstring& processName);
    static void TerminateProcessGracefully(DWORD processId);
};