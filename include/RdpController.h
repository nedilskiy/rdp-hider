#pragma once
#include <windows.h>
#include <vector>
#include <string>

class RdpController {
public:
    static void DisconnectAll();
    static bool LaunchSecureSession(const std::wstring& serverAddress);
    static void LaunchClientAndInject(int clientType); // <--- ДОБАВИТЬ ЭТО
    static void StartProcessMonitor(); // <--- Заменили на это
    static void ShowCredentials();
    static void ReconfigureCredentials();
private:
    static DWORD WINAPI CredentialWindowThread(LPVOID lpParam);
    static std::vector<DWORD> GetProcessIds(const std::wstring& processName);
    static void TerminateProcessGracefully(DWORD processId);
};