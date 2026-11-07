#include "RdpController.h"
#include "ArtifactCleaner.h"
#include "CredentialManager.h"
#include "Logger.h"
#include <windows.h>
#include <tlhelp32.h>
#include <fstream>
#include <thread>
#include <vector>
#include <psapi.h> // Обязательно
#include <shellapi.h>
#include <wincred.h>
#include <iostream>

static void SendKey(WORD vkCode) {
    INPUT input[2] = {};
    input[0].type = INPUT_KEYBOARD; input[0].ki.wVk = vkCode;
    input[1].type = INPUT_KEYBOARD; input[1].ki.wVk = vkCode; input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, input, sizeof(INPUT));
    Sleep(350); // Увеличено со 100ms до 350ms, чтобы UI точно успел перевести фокус
}

static void SendText(const std::wstring& text) {
    for (wchar_t c : text) {
        INPUT input[2] = {};
        input[0].type = INPUT_KEYBOARD; input[0].ki.wScan = c; input[0].ki.dwFlags = KEYEVENTF_UNICODE;
        input[1].type = INPUT_KEYBOARD; input[1].ki.wScan = c; input[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(2, input, sizeof(INPUT));
        Sleep(40); // Увеличено с 20ms до 40ms (имитация реальной скорости печати человека)
    }
}

static void ResetModifierKeys() {
    WORD modifiers[] = { VK_CONTROL, VK_LCONTROL, VK_RCONTROL, VK_MENU, VK_LMENU, VK_RMENU, VK_SHIFT, VK_LSHIFT, VK_RSHIFT, '1', '2', '3' };
    for (WORD vk : modifiers) {
        if (GetAsyncKeyState(vk) & 0x8000) {
            INPUT input[1] = {};
            input[0].type = INPUT_KEYBOARD;
            input[0].ki.wVk = vk;
            input[0].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, input, sizeof(INPUT));
        }
    }
    Sleep(200);
}

void RdpController::ReconfigureCredentials() {
    ResetModifierKeys();

    if (AllocConsole()) {
        FILE* fpOut = nullptr;
        FILE* fpIn = nullptr;
        freopen_s(&fpOut, "CONOUT$", "w", stdout);
        freopen_s(&fpIn, "CONIN$", "r", stdin);

        std::wcout << L"\n=== Update RDP Credentials ===" << std::endl;
        std::wstring ip, user, pass;

        std::wcout << L"Enter New Target IP: ";
        std::wcin >> ip;
        std::wcout << L"Enter New Username: ";
        std::wcin >> user;
        std::wcout << L"Enter New Password: ";
        std::wcin >> pass;

        CredentialManager::SetCredentials(ip, user, pass);
        std::wcout << L"[SUCCESS] Credentials updated successfully!" << std::endl;

        Sleep(2000);

        if (fpOut) fclose(fpOut);
        if (fpIn) fclose(fpIn);
        FreeConsole();
    }
}

// --- Главная логика запуска ---

void RdpController::LaunchClientAndInject(int clientType) {
    auto creds = CredentialManager::GetCredentials();
    if (!creds.isConfigured) return;

    ResetModifierKeys();
    Logger::Log(L"Hotkey detected. Initiating smart launch for client ID: " + std::to_wstring(clientType));

    //if (clientType == 1) {
    //    // 1. Классический mstsc.exe (передаем IP сразу в ключ /v:)
    //    std::wstring cmd = L"mstsc.exe /v:" + creds.ip;
    //    STARTUPINFOW si = { sizeof(si) };
    //    PROCESS_INFORMATION pi;
    //    std::vector<wchar_t> cmdBuffer(cmd.begin(), cmd.end());
    //    cmdBuffer.push_back(L'\0');

    //    if (CreateProcessW(NULL, cmdBuffer.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
    //        CloseHandle(pi.hProcess);
    //        CloseHandle(pi.hThread);

    //        Sleep(2500); // Ожидание окна Windows Security

    //        ResetModifierKeys();
    //        SendText(creds.username);
    //        SendKey(VK_TAB);
    //        SendText(creds.password);
    //        SendKey(VK_RETURN);
    //    }
    //}
    if (clientType == 1) {
        // 1. Отделяем IP от порта, чтобы порт не попадал в имя домена
        std::wstring rawAddress = creds.ip; // Например: "185.184.42.103:33912"
        std::wstring cleanIp = rawAddress;
        std::wstring port = L"";                                                

        size_t colonPos = rawAddress.find(L':');
        if (colonPos != std::wstring::npos) {
            cleanIp = rawAddress.substr(0, colonPos);
            port = rawAddress.substr(colonPos + 1);
        }

        // 2. Формируем логин строго с cleanIp (получится чистый "185.184.42.103\user" без двоеточия)
        std::wstring targetUser = creds.username;
        if (targetUser.find(L'\\') == std::wstring::npos && targetUser.find(L'@') == std::wstring::npos) {
            targetUser = cleanIp + L"\\" + targetUser;
        }

        // 3. Сохраняем данные в Диспетчер учетных данных Windows
        auto saveCred = [&](const std::wstring& host) {
            std::wstring targetName = L"TERMSRV/" + host;
            CREDENTIALW cred = { 0 };
            cred.Type = CRED_TYPE_GENERIC;
            cred.TargetName = (LPWSTR)targetName.c_str();
            cred.UserName = (LPWSTR)targetUser.c_str();
            cred.CredentialBlobSize = (DWORD)(creds.password.size() * sizeof(wchar_t));
            cred.CredentialBlob = (LPBYTE)creds.password.c_str();
            cred.Persist = CRED_PERSIST_SESSION;
            CredWriteW(&cred, 0);
            };

        // Записываем таргетинг и для чистого IP, и для комбинации с портом
        saveCred(cleanIp);
        if (!port.empty()) {
            saveCred(rawAddress); // TERMSRV/185.184.42.103:33912
        }

        // 4. Создаем .rdp файл (в адрес отправляем полный rawAddress с портом)
        wchar_t tempPath[MAX_PATH];
        GetTempPathW(MAX_PATH, tempPath);
        std::wstring rdpFilePath = std::wstring(tempPath) + L"session.rdp";

        std::wofstream rdpFile(rdpFilePath);
        rdpFile << L"full address:s:" << rawAddress << L"\n";
        rdpFile << L"username:s:" << targetUser << L"\n";
        rdpFile << L"promptcredentialonce:i:1\n";
        rdpFile << L"authentication level:i:2\n";
        rdpFile << L"audiomode:i:0\n";
        rdpFile << L"audiocapturemode:i:1\n";
        rdpFile << L"use multimon:i:1\n";
        rdpFile.close();

        // 5. Запуск mstsc.exe
        std::wstring cmd = L"mstsc.exe \"" + rdpFilePath + L"\"";
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        std::vector<wchar_t> cmdBuffer(cmd.begin(), cmd.end());
        cmdBuffer.push_back(L'\0');

        if (CreateProcessW(NULL, cmdBuffer.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            std::thread([pi, rdpFilePath]() {
                Sleep(2000);
                DeleteFileW(rdpFilePath.c_str());
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                }).detach();
        }
    }
    else if (clientType == 2) {
        // 2. Windows App (открываем через системный URI-протокол ms-avd:)
        ShellExecuteW(NULL, L"open", L"ms-avd:", NULL, NULL, SW_SHOWNORMAL);

        Sleep(4000); // Ожидание прогрузки окна Windows App

        ResetModifierKeys();
        //SendKey(VK_TAB);
        //SendKey(VK_TAB);
        //SendKey(VK_TAB);
        //SendKey(VK_RETURN);
        //SendKey(VK_TAB);
        //SendKey(VK_RETURN);
        //SendKey(VK_TAB);
        //SendKey(VK_TAB);
        //SendText(creds.ip);
        //SendKey(VK_TAB);
        //SendKey(VK_TAB);
        //SendKey(VK_TAB);
        //SendKey(VK_RETURN);
        //SendText(creds.username);
        //SendKey(VK_TAB);
        //SendText(creds.password);
        //SendKey(VK_RETURN);
    }
    else if (clientType == 3) {
        // 3. Devolutions Remote Desktop Manager
        ShellExecuteW(NULL, L"open", L"RemoteDesktopManager.exe", NULL, NULL, SW_SHOWNORMAL);

        Sleep(6000); // Пауза на прогрузку главного окна RDM

        ResetModifierKeys();
        //SendText(creds.ip);
        //SendKey(VK_TAB);
        //SendText(creds.username);
        //SendKey(VK_TAB);
        //SendText(creds.password);
        //SendKey(VK_RETURN);
    }
}

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

void RdpController::StartProcessMonitor() {
    std::thread([]() {
        Logger::Log(L"Process Monitor started. Watching for target processes to exit...");
        bool wasAnyTargetRunning = false;

        // Список всех процессов, за которыми мы следим
        std::vector<std::wstring> targetProcs = {
            L"mstsc.exe",
            L"msrdc.exe",
            L"windowsapp.exe",
            L"windows365.exe",
            L"remotedesktopmanager.exe",
            L"remotedesktopmanager64.exe"
        };

        while (true) {
            bool isAnyRunningNow = false;

            // Делаем снимок всех процессов в системе
            HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnapshot != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32W pe32;
                pe32.dwSize = sizeof(PROCESSENTRY32W);

                if (Process32FirstW(hSnapshot, &pe32)) {
                    do {
                        for (const auto& target : targetProcs) {
                            if (_wcsicmp(pe32.szExeFile, target.c_str()) == 0) {
                                isAnyRunningNow = true;
                                break; // Нашли хотя бы один процесс, дальше искать нет смысла
                            }
                        }
                        if (isAnyRunningNow) break;
                    } while (Process32NextW(hSnapshot, &pe32));
                }
                CloseHandle(hSnapshot);
            }

            // ТРИГГЕР: Раньше процесс был, а теперь его нет ни одного
            if (wasAnyTargetRunning && !isAnyRunningNow) {
                Logger::Log(L"All RDP processes terminated (closed, crashed, or killed). Triggering cleanup...");
                ArtifactCleaner::ClearRdpArtifacts();
            }

            wasAnyTargetRunning = isAnyRunningNow;

            // Проверяем каждые 2 секунды (не нагружает процессор)
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        }).detach();
}

void RdpController::ShowCredentials() {
    ResetModifierKeys();

    auto creds = CredentialManager::GetCredentials();
    if (!creds.isConfigured) {
        MessageBoxW(NULL, L"Данные не заданы в памяти.", L"RDP Session Manager", MB_OK | MB_ICONWARNING | MB_TOPMOST);
        return;
    }

    // Запускаем виджет в отдельном потоке, чтобы он не морозил главный цикл
    CreateThread(NULL, 0, CredentialWindowThread, NULL, 0, NULL);
}

LRESULT CALLBACK CredentialWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        // Устанавливаем таймер на 45 секунд (45000 миллисекунд)
        SetTimer(hwnd, 1, 45000, NULL);

        // Создаем текст с данными
        auto creds = CredentialManager::GetCredentials();
        std::wstring text = L" [ Сессия в RAM ]\n\n IP: " + creds.ip +
            L"\n User: " + creds.username +
            L"\n Pass: " + creds.password;

        CreateWindowW(
            L"STATIC", text.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            15, 15, 270, 85,
            hwnd, NULL, GetModuleHandle(NULL), NULL
        );

        // Создаем кнопку "Закрыть"
        CreateWindowW(
            L"BUTTON", L"Закрыть [X]",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            200, 105, 90, 25,
            hwnd, (HMENU)101, GetModuleHandle(NULL), NULL
        );
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 101) { // Нажали кнопку "Закрыть"
            DestroyWindow(hwnd);
        }
        break;
    case WM_TIMER:
        if (wParam == 1) { // Прошло 45 секунд
            DestroyWindow(hwnd);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

DWORD WINAPI RdpController::CredentialWindowThread(LPVOID lpParam) {
    HINSTANCE hInstance = GetModuleHandle(NULL);
    const wchar_t* CLASS_NAME = L"RdpCredWidgetClass";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = CredentialWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc);

    // Рассчитываем координаты правого нижнего угла экрана
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int winWidth = 310;
    int winHeight = 145;
    int posX = screenWidth - winWidth - 20; // 20 пикселей отступа справа
    int posY = screenHeight - winHeight - 60; // 60 пикселей отступа снизу (выше панели задач)

    // Создаем окно поверх всех (WS_EX_TOPMOST) без отображения в панели задач (WS_EX_TOOLWINDOW)
    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        CLASS_NAME, L"RDP Session Manager - RAM Data",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        posX, posY, winWidth, winHeight,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) return 0;

    // Слегка полупрозрачное окно (alpha = 240 из 255) для технологичного вида
    SetLayeredWindowAttributes(hwnd, 0, 240, LWA_ALPHA);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Локальный цикл сообщений для этого окна
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnregisterClassW(CLASS_NAME, hInstance);
    return 0;
}


void RdpController::DisconnectAll() {
    Logger::Log(L"Initiating DisconnectAll for RDP sessions.");

    // Полный список процессов классического RDP и нового Windows App
    std::vector<std::wstring> targets = {
        L"mstsc.exe",
        L"msrdc.exe",
        L"msrdcw.exe",
        L"RemoteDesktop.exe",
        L"WindowsApp.exe",
        L"Windows365.exe"
    };

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