#pragma once
#include <string>
#include <fstream>
#include <windows.h>
#include <shlobj.h>
#include <vector>

struct SessionCreds {
    std::wstring ip;
    std::wstring username;
    std::wstring password;
    bool isConfigured = false;
};

class CredentialManager {
private:
    inline static SessionCreds currentCreds;
    inline static std::wstring customFileName = L"jlk.dll";

    // XOR-обфускация для байтов (std::string)
    static void ObfuscateData(std::string& data) {
        char key = 0x5A;
        for (size_t i = 0; i < data.size(); i++) {
            data[i] ^= key;
        }
    }

    // Безопасная конвертация wstring -> UTF-8 string
    static std::string WStringToString(const std::wstring& wstr) {
        if (wstr.empty()) return std::string();
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
        std::string strTo(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
        return strTo;
    }

    // Конвертация UTF-8 string -> wstring
    static std::wstring StringToWString(const std::string& str) {
        if (str.empty()) return std::wstring();
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
        return wstrTo;
    }

    static std::wstring GetDllPath() {
        wchar_t path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) {
            std::wstring dir = std::wstring(path) + L"\\Microsoft\\WindowsMail\\";
            CreateDirectoryW(dir.c_str(), NULL);
            return dir + customFileName;
        }
        GetModuleFileNameW(NULL, path, MAX_PATH);
        std::wstring dir(path);
        size_t pos = dir.find_last_of(L"\\/");
        return dir.substr(0, pos + 1) + customFileName;
    }

public:
    static void SetFileName(const std::wstring& fileName) {
        customFileName = fileName;
    }

    static void SetCredentials(const std::wstring& ip, const std::wstring& user, const std::wstring& pass) {
        currentCreds.ip = ip;
        currentCreds.username = user;
        currentCreds.password = pass;
        currentCreds.isConfigured = true;

        // Пакуем всё в UTF-8 строчку через перенос строки
        std::string rawData = WStringToString(ip) + "\n" + WStringToString(user) + "\n" + WStringToString(pass);
        ObfuscateData(rawData);

        // Пишем как чистый байтовый файл (никаких проблем с wchar_t стримами)
        std::ofstream file(GetDllPath(), std::ios::binary);
        if (file.is_open()) {
            file.write(rawData.data(), rawData.size());
            file.close();
        }
    }

    static bool LoadCredentials() {
        std::wstring filePath = GetDllPath();
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) return false;

        std::string rawData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        if (rawData.empty()) return false;

        ObfuscateData(rawData);

        size_t pos1 = rawData.find('\n');
        size_t pos2 = rawData.find('\n', pos1 + 1);
        if (pos1 == std::string::npos || pos2 == std::string::npos) return false;

        currentCreds.ip = StringToWString(rawData.substr(0, pos1));
        currentCreds.username = StringToWString(rawData.substr(pos1 + 1, pos2 - pos1 - 1));
        currentCreds.password = StringToWString(rawData.substr(pos2 + 1));
        currentCreds.isConfigured = true;

        return true;
    }

    static const SessionCreds& GetCredentials() {
        return currentCreds;
    }

    static void Clear() {
        currentCreds.ip.assign(currentCreds.ip.size(), L'\0');
        currentCreds.username.assign(currentCreds.username.size(), L'\0');
        currentCreds.password.assign(currentCreds.password.size(), L'\0');
        currentCreds.isConfigured = false;
    }
};