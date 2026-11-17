<p align="center">
  <img src="assets/icon.svg" width="160" height="160" alt="Cookier logo">
</p>

<h1 align="center">Cookier</h1>

<p align="center">
  <strong>A sleek, portable, multi-browser cookie extractor for Windows.</strong><br>
  Built in C++ with a modular provider architecture.
</p>

<p align="center">
  <a href="#"><img src="https://img.shields.io/badge/Version-0.0.3-6f42c1" alt="Version 0.0.3"></a>
  <a href="https://isocpp.org/"><img src="https://img.shields.io/badge/C%2B%2B-17-00599C?logo=c%2B%2B&logoColor=white" alt="C++17"></a>
  <a href="#"><img src="https://img.shields.io/badge/Platform-Windows%2010%2F11%20(x64)-0078D6?logo=windows&logoColor=white" alt="Platform"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License"></a>
</p>

---

## Contents

- [What is Cookier?](#what-is-cookier)
- [Features](#features)
- [How it works](#how-it-works)
- [Supported Browsers](#supported-browsers)
- [Building from source](#building-from-source)
- [Usage](#usage)
- [Technical details](#technical-details)
- [Disclaimer](#disclaimer)
- [License](#license)

---

## What is Cookier?

**Cookier** is a lightweight, native command-line tool designed for security researchers, digital forensics analysts, and power users. It safely extracts browser cookies and exports them as structured JSON.

Unlike monolithic tools that break when a single browser updates its security model, Cookier is built on a **modular provider architecture**. The core executable handles the CLI, file output, and process management, while each browser's specific extraction logic is isolated in its own provider. This means adding support for a new browser is as simple as dropping in a new provider.

- **Fully portable.** `Cookier.exe` is a single, self-contained binary (~2.3 MB). All browser providers and their dependencies (like SQLite) are embedded directly inside the executable. No installers, no external DLLs.
- **Non-intrusive.** Cookier handles running browsers safely. It gracefully bypasses file locks without forcing the user to close their browser or risking database corruption.
- **Extensible.** Built from the ground up to support multiple browsers.

---

## Features

- ✅ **Modular Providers** — Separate, isolated logic for each browser. Adding a new browser does not affect existing ones.
- ✅ **Chrome + Edge + Brave support** — Full support for the modern App-Bound Encryption (ABE) era (Chrome 127+, Edge 127+, Brave 1.63+).
- ✅ **Live Browser Extraction** — Safely extracts data from browsers that are currently open and running.
- ✅ **Multi-Profile Support** — Extract from default profiles, specific named profiles, or all profiles at once.
- ✅ **Silent Operation** — If a target browser is not running, Cookier can launch a hidden, background instance to extract the data and then cleanly terminate itself.
- ✅ **Pretty-Printed JSON** — Structured output grouped by profile, piped to stdout by default so any external tool can consume it.
- ✅ **No external dependencies** — SQLite, the injector, and the provider are all statically linked and embedded.
- ✅ **Deep diagnostics** — `--verbose` writes a timestamped trace of every COM, injector and SQLite step to `C:\Users\Public\cookier_debug.log`.

---

## How it works

1.  **Provider Selection** — Based on your CLI arguments, Cookier loads the embedded provider module into a secure temporary environment.
2.  **Data Acquisition** — The provider locates the browser's cookie database and safely accesses it, even if the browser is actively running and holding file locks (via NT handle duplication).
3.  **Decryption** — The provider injects a helper DLL into the target browser's **main** process only (child processes are skipped via a `--type=` command-line guard) to unwrap the App-Bound Encryption key through the browser's own elevation service, then decrypts each cookie with AES-256-GCM.
4.  **Processing** — The raw SQLite database is parsed, cookies are decrypted, and the data is formatted into JSON.
5.  **Cleanup** — All temporary files, injected modules, and hidden browser instances are destroyed, leaving no trace.

---

## Supported Browsers

| Browser | Status | Notes |
| :--- | :--- | :--- |
| **Google Chrome** | ✅ Supported | Chrome 127+ including App-Bound Encryption and the `IElevator2` interface (144+). |
| **Microsoft Edge** | ✅ Supported | Edge 127+ including App-Bound Encryption and the `IElevator2` interface (144+). |
| **Brave** | ✅ Supported | Brave 1.63+ including App-Bound Encryption. Uses Brave's own CLSID and IElevator v1 IID, plus Chrome's shared IElevator2 IID. |
| Chromium | 🚧 Planned | Requires separate provider. |
| Firefox | 🚧 Planned | Requires separate provider (uses NSS). |
| LibreWolf | 🚧 Planned | Requires separate provider (Gecko, plaintext `cookies.sqlite`). |
| Waterfox | 🚧 Planned | Requires separate provider (Gecko, plaintext `cookies.sqlite`). |
| Opera / Vivaldi | 🚧 Planned | |

### Version range

- **Chrome:** 127 and newer. The tool targets the App-Bound Encryption era exclusively; pre-127 `v10` DPAPI cookies are not supported.
- **Edge:** 127 and newer. Same rationale as Chrome.
- **Brave:** 1.63 and newer (first Brave release to ship App-Bound Encryption).

---

## Building from source

### Prerequisites

- **Windows 10/11 (x64)**
- **Visual Studio 2022 or newer** with the *Desktop development with C++* workload
- **[vcpkg](https://github.com/microsoft/vcpkg)** with the following package installed:

  ```powershell
  C:\vcpkg\vcpkg install sqlite3:x64-windows-static
  ```

### Build steps

1.  Clone the repository and enter the project folder.

2.  Build:

    ```cmd
    .\build.bat
    ```

3.  The final portable executable is at `build\Cookier.exe`. You can move it anywhere on your system.

---

## Usage

Cookier does **nothing** without the `--browser` flag — this prevents accidental execution in scripts or pipelines.

### Extract from a single browser

```powershell
# Default profile
.\Cookier.exe --browser chrome
.\Cookier.exe --browser edge
.\Cookier.exe --browser brave

# All profiles
.\Cookier.exe --browser chrome --profile all
.\Cookier.exe --browser edge --profile all
.\Cookier.exe --browser brave --profile all

# Specific profiles
.\Cookier.exe --browser chrome --profile "Default" "Profile 1"
```

### Extract from every supported browser at once

```powershell
.\Cookier.exe --browser all
```

This nests results as `{"chrome": {...}, "edge": {...}, "brave": {...}}` — one key per browser, each containing the per-profile cookie groups.

### Output

By default, JSON is **printed to stdout**. Status messages (`[*]`, `[+]`, `[!]`) go to **stderr**, so piping into external tools is clean:

```powershell
.\Cookier.exe --browser chrome | jq '."Default"[0]'
.\Cookier.exe --browser chrome > cookies.json
```

To write to a file explicitly:

```powershell
.\Cookier.exe --browser chrome --output C:\Users\Public\my_cookies.json
```

### Arguments

| Flag | Short | Description |
| :--- | :--- | :--- |
| `--browser <name>` | `-b` | **Required.** `chrome`, `edge`, `brave`, or `all`. |
| `--profile <names>` | `-p` | `all` or a space-separated list of profile folder names. Defaults to `Default`. |
| `--output <path>` | `-o` | Write JSON to this file. If omitted, JSON goes to stdout. |
| `--verbose` | `-v` | Enable debug logs. Writes a timestamped trace to `C:\Users\Public\cookier_debug.log` covering every injector and provider step. |
| `--version` | | Print version and exit. |
| `--help` | `-h` | Print usage and exit. |

### Output format (single browser)

```json
{
  "Default": [
    {
      "host_key": ".example.com",
      "name": "session_id",
      "value": "abc123...",
      "path": "/",
      "expires_utc": 13456789012345678,
      "is_secure": true,
      "is_httponly": true
    }
  ],
  "Profile 1": [ ... ]
}
```

### Output format (`--browser all`)

```json
{
  "chrome": {
    "Default": [ ... ],
    "Profile 1": [ ... ]
  },
  "edge": {
    "Default": [ ... ]
  },
  "brave": {
    "Default": [ ... ]
  }
}
```

---

## Technical details

| Component | Purpose |
| :--- | :--- |
| **Provider Architecture** | Browser-specific logic is compiled into a single shared `Provider_Chromium.dll`, embedded as a resource in the main EXE, and extracted to a unique temp directory at runtime. The provider takes the browser name (`chrome` / `edge` / `brave`) as a runtime parameter. |
| **Injector** | A single self-detecting `ChromiumInjector.dll` is embedded inside the provider. It inspects `GetModuleFileNameW` inside the target process to select the correct CLSID, and only runs in the browser's **main** process — child processes (identified by a `--type=` command-line flag) are skipped to prevent mutex races and key-file truncation. |
| **Brave COM identifiers** | Brave uses a distinct CLSID (`{576B31AF-6369-4B6B-8560-E4B203A97A8B}`) and a Brave-specific IElevator v1 IID (`{F396861E-0C8E-4C71-8256-2FAE6D759CE9}`), while reusing Chrome's shared IElevator2 IID (`{1BF5208B-295F-4992-B5F4-3A9BB6494838}`). Brave's `Local State` lives at `%LOCALAPPDATA%\BraveSoftware\Brave-Browser\User Data\Local State`. |
| **Executable discovery** | `FindBrowserExe` prefers the canonical path from `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\brave.exe` (and its `WOW6432Node` mirror), then falls back to Program Files candidates and finally the per-user `%LOCALAPPDATA%` install. The elevation service path-validates the calling executable, so launching anything other than the registered binary returns `PATH_NOT_FOUND` from `DecryptData`. |
| **Database Handling** | Utilizes NT handle duplication (`NtQuerySystemInformation` + `DuplicateHandle`) to read SQLite databases (including `.wal` and `.shm`) without disrupting the browser's active processes. |
| **Cryptography** | Uses Windows BCrypt for AES-256-GCM. Chrome/Edge/Brave 130+ prepend a 32-byte `SHA-256(host_key)` to the decrypted `v20` plaintext; Cookier detects and strips this prefix with backwards-compatible fallback. |
| **Elevation** | The EXE ships with a `requireAdministrator` manifest. `SeDebugPrivilege` is required for handle duplication; the elevation service also requires the caller to be elevated. The injector waits up to 30 s on the cross-process mutex and the provider polls for the resulting key file for up to 20 s, covering Brave's cold elevation-service startup. |
| **Process Management** | Capable of launching and managing invisible browser instances on isolated desktops (`CreateDesktopW`) to guarantee stealth and zero user disruption. |
| **Debug logging** | When `--verbose` is passed, both the provider and the injector append to `C:\Users\Public\cookier_debug.log`, capturing PIDs, browser-kind detection, main-vs-child decisions, every `CoCreateInstance` and `DecryptData` HRESULT, wait-loop progress, and SQLite extraction stats. The log is truncated at the start of each verbose run. |

---

## Disclaimer

> **⚠️ FOR EDUCATIONAL AND RESEARCH PURPOSES ONLY**
>
> Cookier is designed for **authorized security research**, **digital forensics on systems you own**, **malware analysis in isolated lab environments**, and **academic study of Windows security mechanisms**.
>
> Using this tool to access cookies on a machine you do not own, or without explicit permission from the owner, is **illegal** in most jurisdictions and violates the Terms of Service of every browser it targets. The author assumes **no liability** for misuse, damages, or legal consequences resulting from the use of this software.
>
> If you are a security researcher: always use this in a controlled sandbox and obtain written authorization before testing on any system you do not own.

---

## License

Cookier is licensed under the **GNU General Public License v3.0**. See the [`LICENSE`](LICENSE) file for the full text.