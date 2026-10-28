#include "ArtifactCleaner.h"
#include "Logger.h"
#include <windows.h>
#include <shlobj.h>
#include <wincred.h> // Добавлено для Credential Manager
#include <vector>  // Обязательно для std::vector
#include <string>  // Обязательно для std::wstring
#include <tlhelp32.h> // Обязательно для работы со списком процессов

void ArtifactCleaner::ClearRdpArtifacts() {
    Logger::Log(L"Clearing RDP connection artifacts (Registry, Default.rdp, Credentials)...");

    // 1. Очистка реестра MRU
    HKEY hKey;
    LSTATUS status = RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Terminal Server Client\\Default", 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &hKey);
    if (status == ERROR_SUCCESS) {
        wchar_t valueName[256];
        DWORD valueNameLen = 256;
        while (RegEnumValue(hKey, 0, valueName, &valueNameLen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
            RegDeleteValue(hKey, valueName);
            valueNameLen = 256;
        }
        RegCloseKey(hKey);
    }

    // 2. Очистка ключа Servers в реестре
    status = RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Terminal Server Client", 0, KEY_ALL_ACCESS, &hKey);
    if (status == ERROR_SUCCESS) {
        RegDeleteTree(hKey, L"Servers");
        HKEY hNewServerKey;
        RegCreateKeyEx(hKey, L"Servers", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hNewServerKey, NULL);
        if (hNewServerKey) RegCloseKey(hNewServerKey);
        RegCloseKey(hKey);
    }

    // 3. Удаление скрытого файла Default.rdp
    wchar_t docPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docPath))) {
        std::wstring defaultRdp = std::wstring(docPath) + L"\\Default.rdp";
        SetFileAttributesW(defaultRdp.c_str(), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(defaultRdp.c_str());
    }

    // 4. НОВОЕ: Очистка Windows Credential Manager (Диспетчер учетных данных)
    PCREDENTIALW* credentials;
    DWORD count;
    // Ищем все сохраненные учетные данные для RDP (начинаются с TERMSRV/)
    if (CredEnumerateW(L"TERMSRV/*", 0, &count, &credentials)) {
        for (DWORD i = 0; i < count; i++) {
            CredDeleteW(credentials[i]->TargetName, credentials[i]->Type, 0);
        }
        CredFree(credentials);
    }

    ClearJumpListsAndSearchCache();


    Logger::Log(L"RDP artifacts cleared.");
}

void ArtifactCleaner::ClearJumpListsAndSearchCache() {
    Logger::Log(L"Executing hardcore wipe of all Jump Lists and Search UWP cache...");

    // 1. Убиваем процессы, удерживающие кэш
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32W);
        std::vector<std::wstring> procsToKill = { L"explorer.exe", L"SearchUI.exe", L"SearchApp.exe", L"SearchHost.exe", L"StartMenuExperienceHost.exe" };

        if (Process32FirstW(hSnapshot, &pe32)) {
            do {
                for (const auto& proc : procsToKill) {
                    if (_wcsicmp(pe32.szExeFile, proc.c_str()) == 0) {
                        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe32.th32ProcessID);
                        if (hProcess) {
                            TerminateProcess(hProcess, 0);
                            CloseHandle(hProcess);
                        }
                    }
                }
            } while (Process32NextW(hSnapshot, &pe32));
        }
        CloseHandle(hSnapshot);
    }

    // 2. Останавливаем службу поиска
    system("net stop WSearch >nul 2>&1");
    Sleep(500);

    wchar_t appDataPath[MAX_PATH];
    wchar_t localAppDataPath[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath);
    SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppDataPath);

    // 3. Тотальная очистка всех Jump Lists (без привязки к хэшам)
    std::vector<std::wstring> jumpListDirs = {
        std::wstring(appDataPath) + L"\\Microsoft\\Windows\\Recent\\AutomaticDestinations\\",
        std::wstring(appDataPath) + L"\\Microsoft\\Windows\\Recent\\CustomDestinations\\"
    };

    for (const auto& dir : jumpListDirs) {
        std::wstring searchPattern = dir + L"*.*";
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::wstring filePath = dir + fd.cFileName;
                    SetFileAttributesW(filePath.c_str(), FILE_ATTRIBUTE_NORMAL);
                    if (!DeleteFileW(filePath.c_str())) {
                        // Принудительное обнуление, если файл заблокирован
                        HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                        if (hFile != INVALID_HANDLE_VALUE) {
                            CloseHandle(hFile);
                            DeleteFileW(filePath.c_str());
                        }
                    }
                }
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
    }

    // 4. Очистка UWP кэша Windows Search
    std::wstring searchCacheDir = std::wstring(localAppDataPath) + L"\\Packages\\Microsoft.Windows.Search_cw5n1h2txyewy\\LocalState\\DeviceSearchCache\\";
    std::wstring searchPattern = searchCacheDir + L"*.*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                DeleteFileW((searchCacheDir + fd.cFileName).c_str());
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    // 5. Очистка реестра Search
    const wchar_t* searchKeys[] = {
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Search\\RecentApps",
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Search\\ItemHistory"
    };
    for (const auto& key : searchKeys) {
        RegDeleteTreeW(HKEY_CURRENT_USER, key);
        HKEY hNewKey;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, NULL, 0, KEY_WRITE, NULL, &hNewKey, NULL) == ERROR_SUCCESS) {
            RegCloseKey(hNewKey);
        }
    }

    // 6. Восстановление работы системы
    system("net start WSearch >nul 2>&1");
    system("start explorer.exe");
}

void ArtifactCleaner::RestartSearchUI() {
    Logger::Log(L"Restarting Search UI processes to flush visual cache...");

    // В зависимости от версии Windows (10 или 11) процесс поиска называется по-разному
    std::vector<std::wstring> searchProcesses = { L"SearchUI.exe", L"SearchApp.exe", L"SearchHost.exe" };

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(hSnapshot, &pe32)) {
            do {
                for (const auto& proc : searchProcesses) {
                    if (_wcsicmp(pe32.szExeFile, proc.c_str()) == 0) {
                        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe32.th32ProcessID);
                        if (hProcess) {
                            TerminateProcess(hProcess, 0);
                            CloseHandle(hProcess);
                            Logger::Log(L"Killed search process: " + proc);
                        }
                    }
                }
            } while (Process32NextW(hSnapshot, &pe32));
        }
        CloseHandle(hSnapshot);
    }
}

void ArtifactCleaner::ClearRdpEventLogs() {
        system("wevtutil cl Security >nul 2>&1");
        system("wevtutil cl \"Microsoft-Windows-TerminalServices-RDPClient/Operational\" >nul 2>&1");
        system("wevtutil cl \"Microsoft-Windows-TerminalServices-LocalSessionManager/Operational\" >nul 2>&1");
        system("wevtutil cl \"Microsoft-Windows-TerminalServices-RemoteConnectionManager/Operational\" >nul 2>&1");
}



void ArtifactCleaner::DeleteTempRdpFile(const std::wstring& filePath) {
    if (DeleteFile(filePath.c_str())) {
        Logger::Log(L"Deleted temporary RDP file: " + filePath);
    }
}