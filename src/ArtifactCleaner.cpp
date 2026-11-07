#include "ArtifactCleaner.h"
#include "Logger.h"
#include <windows.h>
#include <shlobj.h>
#include <wincred.h>
#include <vector>
#include <string>
#include <tlhelp32.h>
#include <filesystem> // ДОБАВЛЕНО: для рекурсивного удаления папок UWP
#include <wtsapi32.h>

namespace fs = std::filesystem;
static HANDLE g_hExplorerToken = NULL;

void RunHiddenCommand(const std::wstring& command) {
    STARTUPINFOW si = { sizeof(si) };
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = { 0 };

    // CreateProcessW требует мутируемый буфер строки
    std::vector<wchar_t> cmdBuffer(command.begin(), command.end());
    cmdBuffer.push_back(L'\0');

    if (CreateProcessW(NULL, cmdBuffer.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 5000); // Ждем завершения максимум 5 сек
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

//void RestartExplorerSafely() {
//    // Получаем ID активной сессии пользователя (кто сидит за экраном)
//    DWORD sessionId = WTSGetActiveConsoleSessionId();
//    HANDLE hUserToken = NULL;
//
//    // Извлекаем токен обычного пользователя из сессии
//    if (WTSQueryUserToken(sessionId, &hUserToken)) {
//        HANDLE hDuplicatedToken = NULL;
//
//        // Дублируем токен для создания процесса
//        if (DuplicateTokenEx(hUserToken, MAXIMUM_ALLOWED, NULL, SecurityIdentification, TokenPrimary, &hDuplicatedToken)) {
//            STARTUPINFOW si = { sizeof(si) };
//            PROCESS_INFORMATION pi = { 0 };
//            std::wstring cmd = L"explorer.exe";
//            std::vector<wchar_t> cmdBuffer(cmd.begin(), cmd.end());
//            cmdBuffer.push_back(L'\0');
//
//            // Запускаем Проводник с правами обычного пользователя (без админа)
//            // Это заставит Windows восстановить рабочий стол и панель задач
//            if (CreateProcessWithTokenW(hDuplicatedToken, LOGON_WITH_PROFILE, NULL, cmdBuffer.data(), 0, NULL, NULL, &si, &pi)) {
//                CloseHandle(pi.hProcess);
//                CloseHandle(pi.hThread);
//            }
//            CloseHandle(hDuplicatedToken);
//        }
//        CloseHandle(hUserToken);
//    }
//}
void ArtifactCleaner::RestoreExplorer() {
    if (g_hExplorerToken) {
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = { 0 };
        std::wstring cmd = L"explorer.exe";
        std::vector<wchar_t> cmdBuffer(cmd.begin(), cmd.end());
        cmdBuffer.push_back(L'\0');

        // Запускаем Проводник с родным токеном пользователя
        if (CreateProcessWithTokenW(g_hExplorerToken, LOGON_WITH_PROFILE, NULL, cmdBuffer.data(), 0, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        CloseHandle(g_hExplorerToken);
        g_hExplorerToken = NULL; // Очищаем память
    }
}

void ArtifactCleaner::ClearTempDirectories() {
    Logger::Log(L"Initiating deep wipe of %TEMP% directory and Windows 365 logs...");

    wchar_t tempPathBuffer[MAX_PATH];
    DWORD pathLen = GetTempPathW(MAX_PATH, tempPathBuffer);
    if (pathLen == 0 || pathLen > MAX_PATH) return;

    fs::path tempDir(tempPathBuffer);
    std::error_code ec;

    // 1. Точечное гарантированное уничтожение тяжелых логов Windows 365 / AVD
    std::vector<fs::path> targetedRdpTempDirs = {
        tempDir / L"DiagOutputDir",
        tempDir / L"Windows365",
        tempDir / L"RemoteDesktop",
        tempDir / L"msrdcw"
    };

    for (const auto& target : targetedRdpTempDirs) {
        if (fs::exists(target)) {
            fs::remove_all(target, ec);
        }
    }

    // 2. Глобальная зачистка всего остального мусора в %TEMP%
    // Проходимся по всем файлам. Заблокированные файлы (например, от антивируса или запущенных программ) 
    // выдадут ошибку в 'ec', мы её просто игнорируем и идём дальше.
    if (fs::exists(tempDir) && fs::is_directory(tempDir)) {
        for (const auto& entry : fs::directory_iterator(tempDir, ec)) {
            if (ec) {
                ec.clear();
                continue;
            }

            // Пытаемся удалить рекурсивно. Если файл занят — remove_all тихо вернет ошибку в ec.
            fs::remove_all(entry.path(), ec);
        }
    }

    Logger::Log(L"Temp directory cleanup completed.");
}

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

    wchar_t localAppData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
        fs::path cachePath = fs::path(localAppData) / L"Microsoft" / L"Terminal Server Client" / L"Cache";
        std::error_code ec;

        if (fs::exists(cachePath)) {
            // Сносим всю папку с файлами RDP8bmp
            fs::remove_all(cachePath, ec);

            // Сразу создаем её заново пустой, чтобы клиент не выдавал ошибок при следующем запуске
            fs::create_directory(cachePath, ec);
            Logger::Log(L"RDP Bitmap Cache wiped successfully.");
        }
    }

    // 4. Очистка Windows Credential Manager (Диспетчер учетных данных)
    PCREDENTIALW* credentials;
    DWORD count;
    if (CredEnumerateW(L"TERMSRV/*", 0, &count, &credentials)) {
        for (DWORD i = 0; i < count; i++) {
            CredDeleteW(credentials[i]->TargetName, credentials[i]->Type, 0);
        }
        CredFree(credentials);
    }

    ClearWindowsAppArtifacts();

    ClearDevolutionsRdmArtifacts();

    ClearJumpListsAndSearchCache();

    ClearTempDirectories();

    Logger::Log(L"RDP artifacts cleared.");
}

void ArtifactCleaner::ClearJumpListsAndSearchCache() {
    Logger::Log(L"Executing hardcore wipe of all Jump Lists and Search UWP cache...");

    SHAddToRecentDocs(0, NULL);

    // 0.1 Очистка кэша Быстрого доступа, истории "Выполнить" (Win+R) и адресной строки
    const wchar_t* explorerKeys[] = {
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\RecentDocs",
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\RunMRU",
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\TypedPaths"
    };
    for (const auto& key : explorerKeys) {
        RegDeleteTreeW(HKEY_CURRENT_USER, key);
    }

    HANDLE hExplorerToken = NULL;

    // 1. Убиваем процессы, удерживающие кэш
    //HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    //if (hSnapshot != INVALID_HANDLE_VALUE) {
    //    PROCESSENTRY32W pe32;
    //    pe32.dwSize = sizeof(PROCESSENTRY32W);
    //    std::vector<std::wstring> procsToKill = { L"explorer.exe", L"SearchUI.exe", L"SearchApp.exe", L"SearchHost.exe", L"StartMenuExperienceHost.exe" };

    //    if (Process32FirstW(hSnapshot, &pe32)) {
    //        do {
    //            for (const auto& proc : procsToKill) {
    //                if (_wcsicmp(pe32.szExeFile, proc.c_str()) == 0) {
    //                    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe32.th32ProcessID);
    //                    if (hProcess) {
    //                        TerminateProcess(hProcess, 0);
    //                        CloseHandle(hProcess);
    //                    }
    //                }
    //            }
    //        } while (Process32NextW(hSnapshot, &pe32));
    //    }
    //    CloseHandle(hSnapshot);
    //}

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32W);
        std::vector<std::wstring> procsToKill = { L"explorer.exe", L"SearchUI.exe", L"SearchApp.exe", L"SearchHost.exe", L"StartMenuExperienceHost.exe" };

        if (Process32FirstW(hSnapshot, &pe32)) {
            do {
                for (const auto& proc : procsToKill) {
                    if (_wcsicmp(pe32.szExeFile, proc.c_str()) == 0) {
                        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_TERMINATE, FALSE, pe32.th32ProcessID);
                        if (hProcess) {

                            // Сохраняем токен Проводника ПЕРЕД убийством
                            if (_wcsicmp(pe32.szExeFile, L"explorer.exe") == 0 && !g_hExplorerToken) {
                                HANDLE hToken;
                                if (OpenProcessToken(hProcess, TOKEN_DUPLICATE | TOKEN_QUERY, &hToken)) {
                                    DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &g_hExplorerToken);
                                    CloseHandle(hToken);
                                }
                            }

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
    RunHiddenCommand(L"net stop WSearch >nul 2>&1");
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
    RunHiddenCommand(L"net start WSearch >nul 2>&1");
    if (hExplorerToken) {
        //RestartExplorerSafely(hExplorerToken);
        CloseHandle(hExplorerToken);
    }
}

void ArtifactCleaner::ClearWindowsAppArtifacts() {
    Logger::Log(L"Aggressively terminating Windows365.exe and clearing UWP databases...");

    // 1. Принудительное завершение всех фоновых процессов Windows App через Win32 API
    std::vector<std::wstring> targetProcs = {
        L"msrdc.exe",
        L"msrdcw.exe",
        L"RemoteDesktop.exe",
        L"WindowsApp.exe",
        L"Windows365.exe"
    };

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(hSnapshot, &pe32)) {
            do {
                for (const auto& proc : targetProcs) {
                    if (_wcsicmp(pe32.szExeFile, proc.c_str()) == 0) {
                        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe32.th32ProcessID);
                        if (hProcess) {
                            TerminateProcess(hProcess, 0);
                            CloseHandle(hProcess);
                            Logger::Log(L"Successfully killed: " + proc);
                        }
                    }
                }
            } while (Process32NextW(hSnapshot, &pe32));
        }
        CloseHandle(hSnapshot);
    }

    // Даем 1 секунду Windows на снятие блокировок с файлов SQLite
    Sleep(1000);

    // 2. Очистка UWP-контейнеров на диске
    wchar_t localAppData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
        fs::path packagesPath(localAppData);
        packagesPath /= L"Packages";

        std::vector<std::wstring> packagePrefixes = {
            L"Microsoft.RemoteDesktop_",
            L"Microsoft.WindowsApp_",
            L"MicrosoftCorporationII.Windows365_"
        };

        if (fs::exists(packagesPath) && fs::is_directory(packagesPath)) {
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(packagesPath, ec)) {
                if (!entry.is_directory()) continue;

                std::wstring dirName = entry.path().filename().wstring();
                for (const auto& prefix : packagePrefixes) {
                    if (dirName.find(prefix) == 0) {
                        std::vector<std::wstring> subDirsToWipe = {
                            L"LocalState", L"RoamingState", L"Settings", L"LocalCache"
                        };

                        for (const auto& subDir : subDirsToWipe) {
                            fs::path targetPath = entry.path() / subDir;
                            if (fs::exists(targetPath)) {
                                fs::remove_all(targetPath, ec);
                                fs::create_directory(targetPath, ec);
                            }
                        }
                    }
                }
            }
        }
    }
    HKEY hTscKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Terminal Server Client", 0, KEY_SET_VALUE, &hTscKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hTscKey, L"UsernameHint");
        RegDeleteValueW(hTscKey, L"DefaultDomain");
        RegCloseKey(hTscKey);
    }

    HKEY hDefaultKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Terminal Server Client\\Default", 0, KEY_SET_VALUE, &hDefaultKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hDefaultKey, L"UsernameHint");
        RegCloseKey(hDefaultKey);
    }
}

void ArtifactCleaner::RestartSearchUI() {
    Logger::Log(L"Restarting Search UI processes to flush visual cache...");

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


void ArtifactCleaner::ClearDevolutionsRdmArtifacts() {
    Logger::Log(L"Aggressively clearing Devolutions Remote Desktop Manager (RDM)...");

    // 1. Убиваем процессы RDM (обычный, 64-битный и бесплатную версию)
    const std::vector<std::wstring> killCmds = {
        L"taskkill.exe /F /IM RemoteDesktopManager.exe /T",
        L"taskkill.exe /F /IM RemoteDesktopManager64.exe /T",
        L"taskkill.exe /F /IM RemoteDesktopManagerFree.exe /T"
    };

    for (const auto& cmd : killCmds) {
        RunHiddenCommand(cmd);
    }

    Sleep(1000); // Пауза для снятия блокировок с баз данных SQLite/XML

    // 2. Очистка реестра
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Devolutions");

    // 3. Очистка баз данных классической установки (Win32 / .exe)
    std::error_code ec;
    wchar_t localAppData[MAX_PATH];
    wchar_t roamingAppData[MAX_PATH];

    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
        fs::path localDevolutions = fs::path(localAppData) / L"Devolutions";
        if (fs::exists(localDevolutions)) {
            fs::remove_all(localDevolutions, ec);
        }
    }

    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, roamingAppData))) {
        fs::path roamingDevolutions = fs::path(roamingAppData) / L"Devolutions";
        if (fs::exists(roamingDevolutions)) {
            fs::remove_all(roamingDevolutions, ec);
        }
    }

    // 4. Очистка баз данных Microsoft Store установки (UWP / MSIX)
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
        fs::path packagesPath = fs::path(localAppData) / L"Packages";
        std::wstring rdmPrefix = L"Devolutions.RemoteDesktopManager"; // Префикс Store-пакета

        if (fs::exists(packagesPath) && fs::is_directory(packagesPath)) {
            for (const auto& entry : fs::directory_iterator(packagesPath, ec)) {
                if (!entry.is_directory()) continue;

                std::wstring dirName = entry.path().filename().wstring();
                if (dirName.find(rdmPrefix) == 0) {
                    std::vector<std::wstring> subDirsToWipe = {
                        L"LocalState", L"RoamingState", L"Settings", L"LocalCache"
                    };

                    for (const auto& subDir : subDirsToWipe) {
                        fs::path targetPath = entry.path() / subDir;
                        if (fs::exists(targetPath)) {
                            fs::remove_all(targetPath, ec);
                            fs::create_directory(targetPath, ec);
                        }
                    }
                }
            }
        }
    }
}



void ArtifactCleaner::ClearRdpEventLogs() {
    RunHiddenCommand(L"wevtutil cl Security >nul 2>&1");
    RunHiddenCommand(L"wevtutil cl \"Microsoft-Windows-TerminalServices-RDPClient/Operational\" >nul 2>&1");
    RunHiddenCommand(L"wevtutil cl \"Microsoft-Windows-TerminalServices-LocalSessionManager/Operational\" >nul 2>&1");
    RunHiddenCommand(L"wevtutil cl \"Microsoft-Windows-TerminalServices-RemoteConnectionManager/Operational\" >nul 2>&1");
    RunHiddenCommand(L"ipconfig /flushdns >nul 2>&1");
}



void ArtifactCleaner::DeleteTempRdpFile(const std::wstring& filePath) {
    if (DeleteFile(filePath.c_str())) {
        Logger::Log(L"Deleted temporary RDP file: " + filePath);
    }
}