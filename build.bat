@echo off
setlocal

echo ====================================================
echo Building Cookier (Portable Single EXE, merged)
echo ====================================================

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" ( echo [!] vswhere.exe not found. & goto error )
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"
if not defined VS_PATH ( echo [!] VS not found. & goto error )
echo [*] Visual Studio: %VS_PATH%

call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if %errorlevel% neq 0 goto error

rem Only use the STATIC vcpkg triplet for both headers and libs.
rem nlohmann/json is no longer needed anywhere.
set "VCPKG_INC=C:\vcpkg\installed\x64-windows-static\include"
set "VCPKG_LIB=C:\vcpkg\installed\x64-windows-static\lib"

if not exist build mkdir build
pushd build

echo [1/4] Compiling ChromiumInjector.dll...
cl /LD /MT /EHsc /std:c++17 /O1 /Gy /GL ..\providers\chromium\ChromiumInjector.cpp /link /LTCG /OPT:REF /OPT:ICF ole32.lib oleaut32.lib shlwapi.lib shell32.lib user32.lib
if %errorlevel% neq 0 goto error

echo [2/4] Compiling Provider_Chromium.dll...
rc /fo ChromiumProvider.res ..\providers\chromium\ChromiumProvider.rc
cl /LD /MT /EHsc /std:c++17 /O1 /Gy /GL /DSQLITE_STATIC ..\providers\chromium\ChromiumProvider.cpp ChromiumProvider.res /I"%VCPKG_INC%" /link /LTCG /OPT:REF /OPT:ICF /LIBPATH:"%VCPKG_LIB%" sqlite3.lib bcrypt.lib crypt32.lib ole32.lib oleaut32.lib shlwapi.lib shell32.lib advapi32.lib ntdll.lib psapi.lib user32.lib ws2_32.lib /out:Provider_Chromium.dll
if %errorlevel% neq 0 goto error

echo [3/4] Compiling Cookier.exe...
rc /fo Cookier.res ..\src\Cookier.rc
cl /MT /EHsc /std:c++17 /O1 /Gy /GL ..\src\main.cpp Cookier.res /link /LTCG /OPT:REF /OPT:ICF /MANIFESTUAC:"level='requireAdministrator' uiAccess='false'" /out:Cookier.exe
if %errorlevel% neq 0 goto error

echo [4/4] Cleaning up...
del ChromiumInjector.dll 2>nul
del ChromiumInjector.lib 2>nul
del ChromiumInjector.exp 2>nul
del ChromiumInjector.obj 2>nul
del ChromiumProvider.res 2>nul
del ChromiumProvider.obj 2>nul
del ChromiumProvider.lib 2>nul
del ChromiumProvider.exp 2>nul
del Provider_Chromium.dll 2>nul
del Cookier.res 2>nul
del main.obj 2>nul

popd
echo ====================================================
echo BUILD SUCCESSFUL! Run build\Cookier.exe
echo ====================================================
exit /b 0

:error
popd
echo ====================================================
echo BUILD FAILED!
echo ====================================================
pause