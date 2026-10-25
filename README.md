<p align="center">
  <img src="assets/icon.svg" width="160" height="160" alt="Cookier logo">
</p>

<h1 align="center">Cookier</h1>

<p align="center">
  <strong>A sleek, portable, multi-browser cookie extractor for Windows.</strong><br>
  Built in C++ with a modular provider architecture.
</p>

<p align="center">
  <a href="#"><img src="https://img.shields.io/badge/Version-0.0.1-6f42c1" alt="Version 0.0.1"></a>
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

**Cookier** is a lightweight, native command-line tool designed for security researchers, digital forensics analysts, and power users. It safely extracts browser cookies and exports them into a structured JSON file.

Unlike monolithic tools that break when a single browser updates its security model, Cookier is built on a **modular provider architecture**. The core executable handles the CLI, file output, and process management, while each browser's specific extraction logic is isolated in its own independent provider. This means adding support for a new browser is as simple as dropping in a new provider.

- **Fully portable.** `Cookier.exe` is a single, self-contained binary. All browser providers and their dependencies (like SQLite) are embedded directly inside the executable. No installers, no external DLLs.
- **Non-intrusive.** Cookier is designed to handle running browsers safely. It gracefully bypasses file locks without forcing the user to close their browser or risking database corruption.
- **Extensible.** Built from the ground up to support multiple browsers.

---

## Features

- ✅ **Modular Providers** — Separate, isolated logic for each browser. Adding a new browser does not affect existing ones.
- ✅ **Live Browser Extraction** — Safely extracts data from browsers that are currently open and running.
- ✅ **Modern Encryption Support** — Designed to handle the latest cryptographic protections employed by modern browsers.
- ✅ **Multi-Profile Support** — Extract from default profiles, specific named profiles, or all profiles at once.
- ✅ **Silent Operation** — If a target browser is not running, Cookier can launch a hidden, background instance to extract the data and then cleanly terminate itself.
- ✅ **Structured Output** — Exports organized JSON grouped cleanly by profile name.

---

## How it works

1.  **Provider Selection** — Based on your CLI arguments, Cookier loads the appropriate embedded provider module into a secure temporary environment.
2.  **Data Acquisition** — The provider locates the browser's cookie database and safely accesses it, even if the browser is actively running and holding file locks.
3.  **Decryption** — The provider utilizes browser-specific techniques to unwrap the encryption keys required to decrypt the cookie payload.
4.  **Processing** — The raw SQLite database is parsed, cookies are decrypted, and the data is formatted into JSON.
5.  **Cleanup** — All temporary files, injected modules, and hidden browser instances are destroyed, leaving no trace.

---

## Supported Browsers

| Browser | Status | Notes |
| :--- | :--- | :--- |
| **Google Chrome** | ✅ Supported | Full support for modern Chrome versions (including App-Bound Encryption). |
| Chromium | 🚧 Planned | Requires separate provider. |
| Microsoft Edge | 🚧 Planned | Requires separate provider. |
| Brave | 🚧 Planned | Requires separate provider. |
| Firefox | 🚧 Planned | Requires separate provider (uses NSS). |
| Opera / Vivaldi | 🚧 Planned | |

---

## Building from source

### Prerequisites

- **Windows 10/11 (x64)**
- **Visual Studio 2022 or newer** with the *Desktop development with C++* workload
- **[vcpkg](https://github.com/microsoft/vcpkg)** with the following packages installed:

  ```powershell
  C:\vcpkg\vcpkg install sqlite3:x64-windows-static
  C:\vcpkg\vcpkg install nlohmann-json:x64-windows
  ```

### Build steps

1.  Clone the repository and enter the project folder.

2.  Build:

    ```cmd
    .\build.bat
    ```

4.  The final portable executable is at `build\Cookier.exe`. You can move it anywhere on your system.

---

## Usage

Cookier operates silently by default. You must pass the `--browser` flag to initiate an extraction.

```powershell
# Extract cookies from the default Chrome profile
.\Cookier.exe --browser chrome

# Extract from ALL Chrome profiles into one file
.\Cookier.exe --browser chrome --profile all

# Extract from specific profiles
.\Cookier.exe --browser chrome --profile "Default" "Profile 1"

# Save to a custom path
.\Cookier.exe --browser chrome --output C:\Users\Public\my_cookies.json
```

### Arguments

| Flag | Short | Description |
| :--- | :--- | :--- |
| `--browser <name>` | `-b` | Required. `chrome` or `all`. |
| `--profile <names>` | `-p` | `all` or a space-separated list of profile folder names. Defaults to `Default`. |
| `--output <path>` | `-o` | Output JSON path. Defaults to `%USERPROFILE%\Downloads\cookies.json`. |
| `--verbose` | `-v` | Enable debug logs. |
| `--help` | `-h` | Show usage. |

### Output format

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

---

## Technical details

| Component | Purpose |
| :--- | :--- |
| **Provider Architecture** | Browser-specific logic is compiled into separate DLLs, embedded as resources in the main EXE, and extracted dynamically at runtime. |
| **Database Handling** | Utilizes advanced handle management to safely read SQLite databases (including `.wal` and `.shm` files) without disrupting the browser's active processes. |
| **Cryptography** | Leverages Windows BCrypt APIs and browser-specific interfaces to decrypt modern cookie payloads. |
| **Process Management** | Capable of launching and managing invisible browser instances on isolated desktops to guarantee stealth and zero user disruption. |

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