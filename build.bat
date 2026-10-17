@echo off
setlocal

where cl >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo Building with MSVC...
    cl /O2 /EHsc /W3 /DUNICODE /D_UNICODE main.cpp /Fe:ArmaCleaner.exe /link psapi.lib pdh.lib advapi32.lib shell32.lib user32.lib kernel32.lib
    cl /O2 /LD /EHsc /W3 cleaner_extension.cpp /Fe:cleaner_x64.dll /link psapi.lib
    del /Q *.obj *.exp *.lib 2>nul
    goto done
)

where g++ >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo Building with MinGW...
    g++ -O2 -DUNICODE -D_UNICODE -std=c++17 main.cpp -o ArmaCleaner.exe -lpsapi -lpdh -ladvapi32 -lshell32
    g++ -O2 -shared -std=c++17 cleaner_extension.cpp -o cleaner_x64.dll -lpsapi
    goto done
)

echo Error: No suitable C++ compiler found (cl or g++).
exit /b 1

:done
echo Build finished successfully.
endlocal
