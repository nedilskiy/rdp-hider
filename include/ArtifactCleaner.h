#pragma once
#include <string>

class ArtifactCleaner {
public:
    // Clears MRU (Most Recently Used) registry keys and temporary RDP files.
    // Call before connecting and after disconnecting.
    static void ClearRdpArtifacts();
    static void DeleteTempRdpFile(const std::wstring& filePath);
    static void ClearRdpEventLogs();
    static void ClearJumpListsAndSearchCache();
    static void RestartSearchUI();
};