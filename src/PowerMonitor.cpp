#include "PowerMonitor.h"
#include "RdpController.h"
#include "Logger.h"
#include <windows.h>
#include <powrprof.h>
#include <initguid.h> // Needed for GUID definitions
#include <wtsapi32.h>
#include <thread>
#include "ArtifactCleaner.h"

// GUID for Lid Switch State Change
DEFINE_GUID(GUID_LIDSWITCH_STATE_CHANGE, 0xBA3E0F4D, 0xB817, 0x4094, 0xA2, 0xD1, 0xD5, 0x63, 0x79, 0xE6, 0xA0, 0xF3);

PowerMonitor::PowerMonitor() : m_hwnd(NULL), m_hLidNotify(NULL), m_running(false) {}

PowerMonitor::~PowerMonitor() {
    Stop();
}

void PowerMonitor::Start() {
    const wchar_t CLASS_NAME[] = L"RdpPowerMonitorClass";

    WNDCLASS wc = {};
    wc.lpfnWndProc = PowerMonitor::WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    // Create a message-only window
    m_hwnd = CreateWindowEx(
        0, CLASS_NAME, L"RdpPowerMonitorWindow", 0,
        0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandle(NULL), NULL
    );

    if (m_hwnd == NULL) {
        Logger::Log(L"Failed to create hidden window for power monitoring.");
        return;
    }

    // РЕГИСТРАЦИЯ: Слушаем блокировку сеанса (Win+L)
    WTSRegisterSessionNotification(m_hwnd, NOTIFY_FOR_THIS_SESSION);

    // Register for Lid Close notifications
    m_hLidNotify = RegisterPowerSettingNotification(m_hwnd, &GUID_LIDSWITCH_STATE_CHANGE, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (!m_hLidNotify) {
        Logger::Log(L"Failed to register for lid switch notifications.");
    }

    m_running = true;
    Logger::Log(L"PowerMonitor started. Listening for PBT_APMSUSPEND, Lid Switch and Hotkeys.");

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) && m_running) {
        if (msg.message == WM_HOTKEY) {
            // Передаем ID нажатого хоткея в контроллер
            if (msg.wParam == 1001) RdpController::LaunchClientAndInject(1); // mstsc
            else if (msg.wParam == 1002) RdpController::LaunchClientAndInject(2); // WinApp
            else if (msg.wParam == 1003) RdpController::LaunchClientAndInject(3); // Devolutions
            else if (msg.wParam == 1004) RdpController::ShowCredentials();
            else if (msg.wParam == 1005) RdpController::ReconfigureCredentials();
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void PowerMonitor::Stop() {
    m_running = false;

    if (m_hwnd) {
        WTSUnRegisterSessionNotification(m_hwnd); // <--- ДОБАВИТЬ ЭТО
    }

    if (m_hLidNotify) {
        UnregisterPowerSettingNotification(m_hLidNotify);
        m_hLidNotify = NULL;
    }
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = NULL;
    }
}

LRESULT CALLBACK PowerMonitor::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_POWERBROADCAST) {
        if (wParam == PBT_APMSUSPEND) {
            Logger::Log(L"Power Event: System entering Sleep/Hibernate.");
            // Зачистка в отдельном потоке: синхронная работа в обработчике
            // PBT_APMSUSPEND задерживает/ломает переход в сон, а восстановление
            // шелла во время засыпания обрывается на полпути.
            std::thread([]() { RdpController::DisconnectAll(); }).detach();
        }
        else if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
            Logger::Log(L"Power Event: System resumed from Sleep/Hibernate.");
            // Если система уснула раньше, чем шелл успел восстановиться,
            // поднимаем его после пробуждения.
            std::thread([]() { ArtifactCleaner::RestoreExplorer(); }).detach();
        }
        else if (wParam == PBT_POWERSETTINGCHANGE) {
            POWERBROADCAST_SETTING* setting = (POWERBROADCAST_SETTING*)lParam;
            if (setting->PowerSetting == GUID_LIDSWITCH_STATE_CHANGE && setting->DataLength == sizeof(DWORD)) {
                DWORD lidState = *(DWORD*)setting->Data;
                if (lidState == 0) { // 0 = Lid Closed, 1 = Lid Opened
                    Logger::Log(L"Power Event: Laptop Lid Closed.");
                    std::thread([]() { RdpController::DisconnectAll(); }).detach();
                }
            }
        }

    }
    else if (uMsg == WM_WTSSESSION_CHANGE) {
        if (wParam == WTS_SESSION_LOCK) {
            Logger::Log(L"Session Event: Workstation Locked (Win+L).");
            RdpController::DisconnectAll();
        }
        else if (wParam == WTS_SESSION_UNLOCK) {
            Logger::Log(L"Session Event: Workstation Unlocked.");
            std::thread([]() { ArtifactCleaner::RestoreExplorer(); }).detach();
        }
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}