# a3_cleaner

A lightweight memory management toolkit and cache flush utility for Arma 3 client and dedicated servers.

## Features

- **Standby List & Working Set Flush**: Flushes Windows Standby List and process Working Set using native NT calls.
- **In-engine SUPERFLUSH Trigger**: Automates the engine-level cache purge routine introduced in v2.18.
- **Heap Compaction**: Native `callExtension` DLL for in-process heap defragmentation (`HeapCompact`).
- **Adaptive Trimming**: Automatically initiates cleanup upon detecting elevated page fault rates or aggressive working set expansion.
- **SQF Garbage Collection**: Server-side script for periodic pruning of discarded weapon holders, craters, and stale entities.

## Project Structure

```
├── main.cpp                  # Background monitor and external cleaner
├── cleaner_extension.cpp     # Arma 3 callExtension native DLL
├── a3cleaner.ini             # Configuration file
├── build.bat                 # Build script (MSVC / MinGW)
└── scripts/
    └── fn_cacheCleaner.sqf   # Server/client SQF GC routine
```

## Building

### MSVC (Developer Command Prompt)
```cmd
build.bat
```
Or manually:
```cmd
cl /O2 /EHsc /DUNICODE /D_UNICODE main.cpp /Fe:ArmaCleaner.exe /link psapi.lib pdh.lib advapi32.lib shell32.lib user32.lib kernel32.lib
cl /O2 /LD /EHsc cleaner_extension.cpp /Fe:cleaner_x64.dll /link psapi.lib
```

### MinGW-w64
```cmd
g++ -O2 -DUNICODE -D_UNICODE -std=c++17 main.cpp -o ArmaCleaner.exe -lpsapi -lpdh -ladvapi32 -lshell32
g++ -O2 -shared -std=c++17 cleaner_extension.cpp -o cleaner_x64.dll -lpsapi
```

## Usage

1. Run `ArmaCleaner.exe` as Administrator.
2. Launch Arma 3.
3. Default hotkeys:
   - `F11`: Immediate full flush (Working Set + Standby List).
   - `F10`: Trigger in-engine `SUPERFLUSH`.
   - `F9`: Print diagnostics to console.
   - `F8`: Toggle adaptive auto-flush.

To use the extension DLL inside missions, place `cleaner_x64.dll` in the Arma 3 root directory and execute:
```sqf
[] execVM "scripts\fn_cacheCleaner.sqf";
```

## License

MIT
