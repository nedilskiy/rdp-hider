#include "RdpController.h"
#include "ArtifactCleaner.h"
#include "Logger.h"
#include <windows.h>
#include <tlhelp32.h>
#include <fstream>
#include <thread>

struct EnumWindowsCallbackArgs {
    DWORD processId;
    HWND hwndFound;
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    EnumWindowsCallbackArgs* args = (EnumWindowsCallbackArgs*)lParam;
    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (windowPid == args->processId) {
        // Отправляем вежливый запрос на закрытие
        PostMessage(hwnd, WM_CLOSE, 0, 0);
        args->hwndFound = hwnd;
    }
    return TRUE; // Продолжить перечисление
}

void RdpController::TerminateProcessGracefully(DWORD processId) {
    EnumWindowsCallbackArgs args = { processId, NULL };
    EnumWindows(EnumWindowsProc, (LPARAM)&args);

    // Ждем корректного завершения процесса
    HANDLE hProcess = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, processId);
    if (hProcess) {
        if (WaitForSingleObject(hProcess, 3000) == WAIT_TIMEOUT) {
            // Принудительное завершение, если он не закрылся за 3 секунды
            Logger::Log(L"Process timeout. Force terminating PID: " + std::to_wstring(processId));
            TerminateProcess(hProcess, 1);
        }
        CloseHandle(hProcess);
    }
}

std::vector<DWORD> RdpController::GetProcessIds(const std::wstring& processName) {
    std::vector<DWORD> pids;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32);
        if (Process32First(hSnapshot, &pe32)) {
            do {
                if (_wcsicmp(pe32.szExeFile, processName.c_str()) == 0) {
                    pids.push_back(pe32.th32ProcessID);
                }
            } while (Process32Next(hSnapshot, &pe32));
        }
        CloseHandle(hSnapshot);
    }
    return pids;
}

void RdpController::DisconnectAll() {
    Logger::Log(L"Initiating DisconnectAll for RDP sessions.");

    // Определяем целевые клиенты
    std::vector<std::wstring> targets = { L"mstsc.exe", L"msrdc.exe" };

    for (const auto& target : targets) {
        std::vector<DWORD> pids = GetProcessIds(target);
        for (DWORD pid : pids) {
            Logger::Log(L"Terminating " + target + L" (PID: " + std::to_wstring(pid) + L")");
            TerminateProcessGracefully(pid);
        }
    }

    // Очищаем артефакты после закрытия сессий
    ArtifactCleaner::ClearRdpArtifacts();
    ArtifactCleaner::ClearRdpEventLogs();
}



bool RdpController::LaunchSecureSession(const std::wstring& serverAddress) {
    ArtifactCleaner::ClearRdpArtifacts(); // Предварительная очистка

    wchar_t tempPath[MAX_PATH];
    GetTempPath(MAX_PATH, tempPath);
    std::wstring rdpFilePath = std::wstring(tempPath) + L"secure_session.rdp";

    // Записываем временную конфигурацию RDP
    std::wofstream rdpFile(rdpFilePath);
    rdpFile << L"full address:s:" << serverAddress << L"\n";
    rdpFile << L"prompt for credentials:i:1\n";
    rdpFile << L"audiomode:i:2\n";
    rdpFile << L"disable wallpaper:i:1\n";
    rdpFile.close();

    // Запуск процесса
    STARTUPINFO si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    std::wstring cmdLine = L"mstsc.exe \"" + rdpFilePath + L"\"";

    std::vector<wchar_t> cmdBuffer(cmdLine.begin(), cmdLine.end());
    cmdBuffer.push_back(L'\0');

    if (CreateProcess(NULL, cmdBuffer.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        // Фоновый поток, который ждет закрытия mstsc.exe
        std::thread([pi, rdpFilePath]() {
            // mstsc читает файл при запуске, ждем пару секунд и удаляем временный профиль
            Sleep(1000);
            ArtifactCleaner::DeleteTempRdpFile(rdpFilePath);

            // Блокируем поток до закрытия пользователем окна RDP
            WaitForSingleObject(pi.hProcess, INFINITE);
            Sleep(1000);

            // Процесс mstsc.exe закрылся (и сохранил свои логины/IP), уничтожаем эти артефакты!
            ArtifactCleaner::ClearRdpArtifacts();
            ArtifactCleaner::ClearRdpEventLogs();

            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            }).detach();

        return true;
    }

    ArtifactCleaner::DeleteTempRdpFile(rdpFilePath);
    return false;
}