#pragma once
#include <windows.h>

class PowerMonitor {
public:
    PowerMonitor();
    ~PowerMonitor();

    // Starts the hidden window message loop to listen for power events
    void Start();
    void Stop();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd;
    HPOWERNOTIFY m_hLidNotify;
    bool m_running;
};