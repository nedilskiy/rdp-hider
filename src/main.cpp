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
#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")

namespace fs = std::filesystem;

// Имя файла, под которым программа будет жить в системе (можете изменить)
const wchar_t* INSTALLED_EXE_NAME = L"svvhost.exe";

void CheckAndInstallVCRedist() {
    // Проверяем, есть ли msvcp140.dll в системной папке
    wchar_t sysPath[MAX_PATH];
    GetSystemDirectoryW(sysPath, MAX_PATH);
    std::wstring dllPath = std::wstring(sysPath) + L"\\msvcp140.dll";

    if (std::filesystem::exists(dllPath)) {
        return; // Уже установлено, ничего делать не нужно
    }

    // Путь для скачивания временного установщика
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring installerPath = std::wstring(tempPath) + L"vc_redist.x86.exe";

    // Прямая официальная ссылка на Microsoft Visual C++ 2015-2022 Redistributable (x86)
    // Так как твой проект компилируется под /machine:X86, нам нужен именно x86 рантайм
    LPCWSTR url = L"https://aka.ms/vs/17/release/vc_redist.x86.exe";

    // Скачиваем файл в фоновом режиме
    HRESULT hr = URLDownloadToFileW(NULL, url, installerPath.c_str(), 0, NULL);
    if (SUCCEEDED(hr)) {
        // Запускаем тихую установку без окон и перезагрузок
        std::wstring cmd = L"\"" + installerPath + L"\" /quiet /norestart";

        STARTUPINFOW si = { sizeof(si) };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi;

        std::vector<wchar_t> cmdBuffer(cmd.begin(), cmd.end());
        cmdBuffer.push_back(L'\0');

        if (CreateProcessW(NULL, cmdBuffer.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            // Ждем, пока установщик завершится (может занять 10-20 секунд)
            WaitForSingleObject(pi.hProcess, 30000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }

        // Удаляем временный установщик
        DeleteFileW(installerPath.c_str());
    }
}

//bool InstallAndSelfDelete(HINSTANCE hInstance, LPWSTR lpCmdLine) {
//    wchar_t currentPath[MAX_PATH];
//    GetModuleFileNameW(hInstance, currentPath, MAX_PATH);
//
//    wchar_t localAppData[MAX_PATH];
//    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
//        return true; // Если не удалось получить путь, работаем откуда запустили
//    }
//
//    // Целевая папка установки
//    fs::path targetDir = fs::path(localAppData) / L"Microsoft" / L"WindowsMail";
//    std::error_code ec;
//    fs::create_directories(targetDir, ec);
//
//    fs::path targetPath = targetDir / INSTALLED_EXE_NAME;
//
//    // Если мы УЖЕ запущены из системной папки — ничего не делаем, продолжаем работу
//    if (_wcsicmp(currentPath, targetPath.c_str()) == 0) {
//        return true;
//    }
//
//    // 1. Копируем себя в системную папку
//    fs::copy_file(currentPath, targetPath, fs::copy_options::overwrite_existing, ec);
//    if (ec) return true; // Если скопировать не удалось — продолжаем работу как есть
//
//    //// 2. Сразу прописываем новый путь в автозагрузку
//    //HKEY hKey;
//    //if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
//    //    RegSetValueExW(hKey, L"RdpSessionManager", 0, REG_SZ, (BYTE*)targetPath.c_str(), (DWORD)(targetPath.wstring().size() + 1) * sizeof(wchar_t));
//    //    RegCloseKey(hKey);
//    //}
//
//    // 2. Создаем задачу в Планировщике (запуск без UAC при входе юзера)
//    std::wstring taskCmd = L"schtasks.exe /create /tn \"RdpSessionManager\" /tr \"\\\"" + targetPath.wstring() + L"\\\"\" /sc onlogon /rl highest /f";
//
//    STARTUPINFOW siTask = { sizeof(siTask) };
//    siTask.dwFlags = STARTF_USESHOWWINDOW;
//    siTask.wShowWindow = SW_HIDE;
//    PROCESS_INFORMATION piTask;
//    std::vector<wchar_t> cmdBufferTask(taskCmd.begin(), taskCmd.end());
//    cmdBufferTask.push_back(L'\0');
//
//    if (CreateProcessW(NULL, cmdBufferTask.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &siTask, &piTask)) {
//        WaitForSingleObject(piTask.hProcess, 5000);
//        CloseHandle(piTask.hProcess);
//        CloseHandle(piTask.hThread);
//    }
//
//    // 3. Запускаем новую копию из системной папки
//    ShellExecuteW(NULL, L"open", targetPath.c_str(), lpCmdLine, NULL, SW_SHOWNORMAL);
//
//    // 4. Скрытый скрипт самоудаления: пингуем localhost для паузы в 2 секунды, затем удаляем старый .exe
//    std::wstring deleteCmd = L"cmd.exe /c ping 127.0.0.1 -n 2 > nul & del /F /Q \"" + std::wstring(currentPath) + L"\"";
//
//    STARTUPINFOW si = { sizeof(si) };
//    si.dwFlags = STARTF_USESHOWWINDOW;
//    si.wShowWindow = SW_HIDE; // Скрываем черное окно консоли
//    PROCESS_INFORMATION pi;
//
//    std::vector<wchar_t> cmdBuffer(deleteCmd.begin(), deleteCmd.end());
//    cmdBuffer.push_back(L'\0');
//
//    if (CreateProcessW(NULL, cmdBuffer.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
//        CloseHandle(pi.hProcess);
//        CloseHandle(pi.hThread);
//    }
//
//    // Возвращаем false, чтобы текущий процесс мгновенно завершился и позволил скрипту удалить файл
//    return false;
//}

//bool InstallAndSelfDelete(HINSTANCE hInstance, LPWSTR lpCmdLine) {
//    wchar_t currentPath[MAX_PATH];
//    GetModuleFileNameW(hInstance, currentPath, MAX_PATH);
//
//    wchar_t localAppData[MAX_PATH];
//    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
//        return true;
//    }
//
//    namespace fs = std::filesystem;
//    fs::path targetDir = fs::path(localAppData) / L"Microsoft" / L"WindowsMail";
//    std::error_code ec;
//    fs::create_directories(targetDir, ec);
//
//    fs::path targetPath = targetDir / INSTALLED_EXE_NAME;
//
//    // 1. УБИВАЕМ СТАРЫЙ КЛЮЧ РЕЕСТРА (именно он вызывает UAC окно при перезапуске)
//    HKEY hKey;
//    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
//        RegDeleteValueW(hKey, L"RdpSessionManager");
//        RegCloseKey(hKey);
//    }
//
//    if (_wcsicmp(currentPath, targetPath.c_str()) == 0) {
//        return true; // Уже работаем из целевой папки
//    }
//
//    // 2. Копируем себя
//    fs::copy_file(currentPath, targetPath, fs::copy_options::overwrite_existing, ec);
//    if (ec) return true;
//
//    // 3. Создаем задачу в Планировщике (запуск БЕЗ UAC при входе юзера)
//    // Флаг /rl highest дает максимальные права сразу
//    std::wstring taskCmd = L"schtasks.exe /create /tn \"RdpSessionManager\" /tr \"\\\"" + targetPath.wstring() + L"\\\"\" /sc onlogon /rl highest /f";
//
//    STARTUPINFOW siTask = { sizeof(siTask) };
//    siTask.dwFlags = STARTF_USESHOWWINDOW;
//    siTask.wShowWindow = SW_HIDE;
//    PROCESS_INFORMATION piTask;
//    std::vector<wchar_t> cmdBufferTask(taskCmd.begin(), taskCmd.end());
//    cmdBufferTask.push_back(L'\0');
//
//    if (CreateProcessW(NULL, cmdBufferTask.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &siTask, &piTask)) {
//        WaitForSingleObject(piTask.hProcess, 5000);
//        CloseHandle(piTask.hProcess);
//        CloseHandle(piTask.hThread);
//    }
//
//    // 4. Запускаем новую копию и удаляем исходник
//    ShellExecuteW(NULL, L"open", targetPath.c_str(), lpCmdLine, NULL, SW_SHOWNORMAL);
//
//    std::wstring deleteCmd = L"cmd.exe /c ping 127.0.0.1 -n 2 > nul & del /F /Q \"" + std::wstring(currentPath) + L"\"";
//    STARTUPINFOW siDel = { sizeof(siDel) };
//    siDel.dwFlags = STARTF_USESHOWWINDOW;
//    siDel.wShowWindow = SW_HIDE;
//    PROCESS_INFORMATION piDel;
//    std::vector<wchar_t> cmdBufferDel(deleteCmd.begin(), deleteCmd.end());
//    cmdBufferDel.push_back(L'\0');
//
//    if (CreateProcessW(NULL, cmdBufferDel.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &siDel, &piDel)) {
//        CloseHandle(piDel.hProcess);
//        CloseHandle(piDel.hThread);
//    }
//
//    return false;
//}

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

bool InstallAndSelfDelete(HINSTANCE hInstance, LPWSTR lpCmdLine) {
    wchar_t currentPath[MAX_PATH];
    GetModuleFileNameW(hInstance, currentPath, MAX_PATH);

    wchar_t localAppData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
        return true;
    }

    namespace fs = std::filesystem;
    fs::path targetDir = fs::path(localAppData) / L"Microsoft" / L"WindowsMail";
    std::error_code ec;
    fs::create_directories(targetDir, ec);

    fs::path targetPath = targetDir / INSTALLED_EXE_NAME;

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, L"WindowsSessionManager");
        RegCloseKey(hKey);
    }

    if (_wcsicmp(currentPath, targetPath.c_str()) == 0) {
        return true;
    }

    fs::copy_file(currentPath, targetPath, fs::copy_options::overwrite_existing, ec);
    if (ec) return true;

    // 1. Создаем задачу, жестко привязанную к активной сессии текущего пользователя
    std::wstring taskCmd = L"cmd.exe /c schtasks.exe /create /tn \"WindowsSessionManager\" /tr \"\\\"" + targetPath.wstring() + L"\\\"\" /sc onlogon /ru \"%USERDOMAIN%\\%USERNAME%\" /rl highest /f";

    STARTUPINFOW siTask = { sizeof(siTask) };
    siTask.dwFlags = STARTF_USESHOWWINDOW;
    siTask.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION piTask;
    std::vector<wchar_t> cmdBufferTask(taskCmd.begin(), taskCmd.end());
    cmdBufferTask.push_back(L'\0');

    if (CreateProcessW(NULL, cmdBufferTask.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &siTask, &piTask)) {
        WaitForSingleObject(piTask.hProcess, 5000);
        CloseHandle(piTask.hProcess);
        CloseHandle(piTask.hThread);
    }

    // 2. Снимаем ограничения на запуск от батареи и тайм-ауты (PowerShell в фоне)
    std::wstring psCmd = L"powershell.exe -WindowStyle Hidden -Command \"Set-ScheduledTask -TaskName 'WindowsSessionManager' -Settings (New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit 0)\"";

    STARTUPINFOW siPs = { sizeof(siPs) };
    siPs.dwFlags = STARTF_USESHOWWINDOW;
    siPs.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION piPs;
    std::vector<wchar_t> cmdBufferPs(psCmd.begin(), psCmd.end());
    cmdBufferPs.push_back(L'\0');

    if (CreateProcessW(NULL, cmdBufferPs.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &siPs, &piPs)) {
        WaitForSingleObject(piPs.hProcess, 5000);
        CloseHandle(piPs.hProcess);
        CloseHandle(piPs.hThread);
    }

    ShellExecuteW(NULL, L"open", targetPath.c_str(), lpCmdLine, NULL, SW_SHOWNORMAL);

    std::wstring deleteCmd = L"cmd.exe /c ping 127.0.0.1 -n 2 > nul & del /F /Q \"" + std::wstring(currentPath) + L"\"";
    STARTUPINFOW siDel = { sizeof(siDel) };
    siDel.dwFlags = STARTF_USESHOWWINDOW;
    siDel.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION piDel;
    std::vector<wchar_t> cmdBufferDel(deleteCmd.begin(), deleteCmd.end());
    cmdBufferDel.push_back(L'\0');

    if (CreateProcessW(NULL, cmdBufferDel.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &siDel, &piDel)) {
        CloseHandle(piDel.hProcess);
        CloseHandle(piDel.hThread);
    }

    return false;
}

// --- Глобальный перехват хоткеев через low-level keyboard hook ---
// RegisterHotKey эксклюзивен: комбинацию получает тот, кто зарегистрировал её
// первым. LL-хук видит клавиши раньше всех и может «проглотить» нажатие,
// поэтому наши комбинации получают приоритет над чужими приложениями.
static HHOOK g_hKeyboardHook = NULL;
static DWORD g_lastHotkeyTime[5] = { 0 };

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

        if (ctrl && alt) {
            WPARAM hotkeyId = 0;
            switch (kb->vkCode) {
            case '1':  hotkeyId = 1001; break;
            case '2':  hotkeyId = 1002; break;
            case '3':  hotkeyId = 1003; break;
            case 0xDC: hotkeyId = 1004; break;
            case 'R':  hotkeyId = 1005; break;
            }

            if (hotkeyId != 0) {
                DWORD now = GetTickCount();
                DWORD idx = (DWORD)(hotkeyId - 1001);
                // Антидребезг: игнорируем автоповтор при зажатой клавише
                if (now - g_lastHotkeyTime[idx] > 1000) {
                    g_lastHotkeyTime[idx] = now;
                    PostThreadMessageW(GetCurrentThreadId(), WM_HOTKEY, hotkeyId, 0);
                }
                return 1; // Проглатываем нажатие — чужие приложения его не получат
            }
        }
    }
    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    CheckAndInstallVCRedist();

    if (!RequestAdminPrivileges(lpCmdLine)) { return 0; }

    if (!InstallAndSelfDelete(hInstance, lpCmdLine)) { return 0; }

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

    // 3. Формируем безопасный абсолютный путь для логов рядом с экзешником
    wchar_t localAppData[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData);
    std::wstring logPath = std::wstring(localAppData) + L"\\Microsoft\\WindowsMail\\RdpSessionManager.log";

    Logger::Init(logPath.c_str());
    Logger::LogEvent(L"RdpSessionManager Service Started.");

    if (!CredentialManager::LoadCredentials()) {
        PromptInitialCredentialsConsole();
    }
    else {
        Logger::LogEvent(L"Credentials successfully loaded.");
    }

    RdpController::StartProcessMonitor();

    // Хоткеи через LL-хук вместо RegisterHotKey — приоритет над чужими приложениями
    g_hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    if (!g_hKeyboardHook) {
        Logger::LogEvent(L"Failed to install low-level keyboard hook.");
    }

    PowerMonitor monitor;
    monitor.Start();

    if (g_hKeyboardHook) {
        UnhookWindowsHookEx(g_hKeyboardHook);
        g_hKeyboardHook = NULL;
    }
    CredentialManager::Clear();

    return 0;
}