@echo off
setlocal
cd /d "%~dp0"

title Building Arma 3 Cache Cleaner...
echo ============================================
echo   Arma 3 Cache Cleaner - Build System
echo ============================================
echo.

where cl >nul 2>&1
if %ERRORLEVEL% EQU 0 goto use_msvc

where g++ >nul 2>&1
if %ERRORLEVEL% EQU 0 goto use_sys_mingw

if exist "%~dp0compiler\w64devkit\bin\g++.exe" goto use_portable_mingw

echo No compiler found in system.
echo Automatically downloading portable C++ compiler (w64devkit)...
echo Please wait a moment...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$url = 'https://github.com/skeeto/w64devkit/releases/download/v2.10.1/w64devkit-x64-2.10.1.7z.exe'; $out = \"$env:TEMP\w64devkit.exe\"; (New-Object Net.WebClient).DownloadFile($url, $out); Start-Process -FilePath $out -ArgumentList \"-y\", \"-o\"\"%~dp0compiler\"\"\" -Wait"

if exist "%~dp0compiler\w64devkit\bin\g++.exe" goto use_portable_mingw

echo Error: Failed to setup compiler automatically.
goto finish

:use_msvc
echo Using MSVC compiler...
echo Compiling ArmaCleaner.exe...
cl /O2 /EHsc /W3 /DUNICODE /D_UNICODE "%~dp0main.cpp" /Fe:"%~dp0ArmaCleaner.exe" /link psapi.lib pdh.lib advapi32.lib shell32.lib user32.lib kernel32.lib
echo Compiling cleaner_x64.dll...
cl /O2 /LD /EHsc /W3 "%~dp0cleaner_extension.cpp" /Fe:"%~dp0cleaner_x64.dll" /link psapi.lib
del /Q *.obj *.exp *.lib 2>nul
goto verify

:use_sys_mingw
echo Using system MinGW (g++)...
set GXX=g++
goto mingw_build

:use_portable_mingw
echo Using portable MinGW (w64devkit)...
set GXX="%~dp0compiler\w64devkit\bin\g++.exe"
goto mingw_build

:mingw_build
echo Compiling ArmaCleaner.exe...
%GXX% -O2 -DUNICODE -D_UNICODE -std=c++17 "%~dp0main.cpp" -o "%~dp0ArmaCleaner.exe" -lpsapi -lpdh -ladvapi32 -lshell32
echo Compiling cleaner_x64.dll...
%GXX% -O2 -shared -std=c++17 "%~dp0cleaner_extension.cpp" -o "%~dp0cleaner_x64.dll" -lpsapi

:verify
echo.
if exist "%~dp0ArmaCleaner.exe" (
    echo [OK] ArmaCleaner.exe built successfully.
) else (
    echo [FAIL] ArmaCleaner.exe build failed.
)

if exist "%~dp0cleaner_x64.dll" (
    echo [OK] cleaner_x64.dll built successfully.
) else (
    echo [FAIL] cleaner_x64.dll build failed.
)

:finish
echo.
echo ============================================
echo   Press any key to exit.
echo ============================================
pause
endlocal
