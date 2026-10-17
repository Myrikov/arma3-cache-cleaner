<div align="center">
  <h1>⚡ A3 Cleaner — Arma 3 Memory & Cache Toolkit</h1>
  <p><b>High-performance memory management toolkit and cache flush utility for Arma 3.<br>Combats engine hash degradation, eliminates FPS rot, and purges bloated memory caches.</b></p>

  <img src="https://img.shields.io/badge/Windows-10%20%2F%2011-0078D4?style=for-the-badge&logo=windows&logoColor=white"/>
  <img src="https://img.shields.io/badge/C%2B%2B-17-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white"/>
  <img src="https://img.shields.io/badge/Arma%203-v2.18%2B-FF6B00?style=for-the-badge&logo=bohemia-interactive&logoColor=white"/>
  <img src="https://img.shields.io/badge/BattlEye-Safe-brightgreen?style=for-the-badge&logo=shield"/>
  <img src="https://img.shields.io/badge/License-MIT-yellow?style=for-the-badge"/>
</div>

---

> **A3 Cleaner tackles the notorious Arma 3 "FPS Rot" phenomenon.**  
> During prolonged sessions, the Real Virtuality engine accumulates unmanaged hash table references, bloated working sets, and stale standby caches. A3 Cleaner cleans them safely without requiring a server reboot.

---

## ✨ What is A3 Cleaner?

**A3 Cleaner** is an all-in-one performance utility designed for both Arma 3 clients and dedicated servers:

1. 🔍 **Engine Cache Flush** — Triggers the internal `SUPERFLUSH` routine introduced in Arma 3 v2.18 via synthetic input automation.
2. 🧹 **Native Memory Purge** — Uses undocumented Windows NT system routines (`NtSetSystemInformation`) to clear Standby and Modified Page lists.
3. 📉 **Working Set Trimming** — Evicts unreferenced physical memory pages back to disk cache via `EmptyWorkingSet`.
4. 🧠 **Heap Compaction (DLL)** — A native `callExtension` C++ DLL providing in-engine heap compaction (`HeapCompact`) to counter SQF hash map fragmentation.
5. ♻️ **SQF Garbage Collection** — Configurable server-side cleaner removing abandoned weapon holders, craters, ruined vehicles, and empty AI groups.
6. 🎯 **Adaptive Mode** — Real-time telemetry monitoring Page Faults/sec and Working Set expansion rate to automatically trigger purges when stutter is imminent.

---

## 🖥 Diagnostic Telemetry

| Metric | Details |
|--------|---------|
| 💾 **Working Set** | Current and peak physical RAM allocated to Arma 3 |
| 📄 **Private Usage** | Actual dedicated memory footprint of the game process |
| ⚡ **Page Faults / sec** | Primary indicator of engine hash table expansion and memory fragmentation |
| 📈 **WS Growth Rate** | Real-time byte allocation velocity (detects runaway script leaks) |
| 📊 **System Load** | Global physical memory pressure and commit charge |

---

## ⚙️ Cleaning Modes & Commands

| Mode / Command | Target Layer | Description |
|----------------|--------------|-------------|
| ⚡ **SUPERFLUSH** (`F10`) | Engine VRAM / Textures | Invokes engine-level texture and PBO file bank cache flush |
| 🧹 **Full Flush** (`F11`) | Windows NT Kernel + RAM | Purges Standby List, Modified List, and Arma 3 Working Set |
| 🧠 **Heap Compact** (`DLL`) | SQF Virtual Machine | Compacts process memory heaps to defragment hash tables |
| 🤖 **Adaptive Flush** (`F8`) | Autonomous | Automatically cleans when Page Faults exceed threshold |
| 🗑 **SQF GC Routine** | Mission Entities | Clears dead bodies, dropped loot, craters, and empty groups |

---

## 🎮 Hotkeys Reference

| Key | Action |
|:---:|--------|
| `F11` | **Manual Full Flush** — Clears Working Set + Windows Standby List immediately |
| `F10` | **Engine SUPERFLUSH** — Dispatches native `Shift`+`Num -` `SUPERFLUSH` to the game window |
| `F9` | **Diagnostic Status** — Prints live process memory metrics, peak usage, and session uptime |
| `F8` | **Toggle Adaptive Mode** — Enables/disables auto-flush based on Page Fault rates |

---

## 🚫 Safety & Anti-Cheat Compatibility

- 🛡️ **BattlEye Safe**: Does **not** inject code, patch executable memory, or hook game routines.
- 🔒 **Zero Modding Flags**: External utility interacts via standard Windows APIs (`EmptyWorkingSet`, `SendInput`).
- ⚡ **No Permanent Tweaks**: Leaves zero permanent registry or system footprint.

---

## 🚀 Quick Start

### Option 1: Standalone Cleaner (`ArmaCleaner.exe`)

1. Run `build.bat` (automatically installs a portable compiler if none is detected).
2. Launch `ArmaCleaner.exe` as **Administrator**.
3. Start Arma 3 and play. Press `F11` or `F10` whenever FPS drops occur, or let Adaptive Mode handle it automatically.

### Option 2: Server / Mission Integration (`cleaner_x64.dll` + SQF)

1. Place `cleaner_x64.dll` into your root Arma 3 directory (alongside `arma3server_x64.exe`).
2. Add `fn_cacheCleaner.sqf` to your mission directory (`scripts/fn_cacheCleaner.sqf`).
3. Call it in your mission's `init.sqf` or `initServer.sqf`:
   ```sqf
   [] execVM "scripts\fn_cacheCleaner.sqf";
   ```

---

## 📁 File Layout

```
├── main.cpp                  # Background monitor, diagnostics & hotkey controller
├── cleaner_extension.cpp     # Arma 3 callExtension native C++ DLL
├── a3cleaner.ini             # Runtime configuration file
├── build.bat                 # One-click automated build script (MSVC / MinGW)
└── scripts/
    └── fn_cacheCleaner.sqf   # Server/client SQF entity garbage collector
```

---

## 🛠 Configuration (`a3cleaner.ini`)

```ini
[General]
AutoFlushInterval = 900          ; Periodic auto-flush in seconds (0 = disabled)
AutoFlushType = adaptive         ; Options: full, workingset, standby, superflush, adaptive
BeepOnFlush = 1                  ; Audio confirmation on clean (1 = on, 0 = off)
LogLevel = info

[Adaptive]
Enabled = 1                      ; Enable automatic telemetry-driven flushes
PageFaultThreshold = 50000       ; Page Faults/sec trigger threshold
WorkingSetGrowthThresholdMB = 512 ; Max allowed memory growth per interval
CheckInterval = 30               ; Telemetry sample rate in seconds
MinCooldown = 120                ; Minimum interval between auto-flushes

[SuperFlush]
PreDelay = 150                   ; Window activation delay (ms)
KeyDelay = 35                    ; Synthetic key input cadence (ms)
```

---

## 📄 License

MIT © [Myrikov](https://github.com/Myrikov)
