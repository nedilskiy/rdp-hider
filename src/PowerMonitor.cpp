#include "PowerMonitor.h"
#include "RdpController.h"
#include "Logger.h"
#include <windows.h>
#include <powrprof.h>
#include <initguid.h> // Needed for GUID definitions

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

    // Register for Lid Close notifications
    m_hLidNotify = RegisterPowerSettingNotification(m_hwnd, &GUID_LIDSWITCH_STATE_CHANGE, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (!m_hLidNotify) {
        Logger::Log(L"Failed to register for lid switch notifications.");
    }

    m_running = true;
    Logger::Log(L"PowerMonitor started. Listening for PBT_APMSUSPEND and Lid Switch.");

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) && m_running) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void PowerMonitor::Stop() {
    m_running = false;
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
            RdpController::DisconnectAll();
        }
        else if (wParam == PBT_POWERSETTINGCHANGE) {
            POWERBROADCAST_SETTING* setting = (POWERBROADCAST_SETTING*)lParam;
            if (setting->PowerSetting == GUID_LIDSWITCH_STATE_CHANGE && setting->DataLength == sizeof(DWORD)) {
                DWORD lidState = *(DWORD*)setting->Data;
                if (lidState == 0) { // 0 = Lid Closed, 1 = Lid Opened
                    Logger::Log(L"Power Event: Laptop Lid Closed.");
                    RdpController::DisconnectAll();
                }
            }
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}