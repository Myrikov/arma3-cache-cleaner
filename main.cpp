#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <pdh.h>
#include <shlobj.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <iomanip>
#include <filesystem>
#include <functional>
#include <algorithm>
#include <ctime>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;

static const wchar_t* APP_NAME        = L"Arma 3 Hash & Cache Cleaner v2.0";
static const wchar_t* APP_NAME_SHORT  = L"A3Cleaner";
static const char*    APP_VERSION     = "2.0.0";
static const char*    APP_BUILD_DATE  = __DATE__ " " __TIME__;
static const char*    CONFIG_FILE     = "a3cleaner.ini";
static const char*    LOG_FILE        = "a3cleaner.log";

enum HotkeyId : int {
    HOTKEY_MANUAL_FLUSH       = 1,
    HOTKEY_SUPERFLUSH         = 2,
    HOTKEY_STATUS             = 3,
    HOTKEY_TOGGLE_ADAPTIVE    = 4,
    HOTKEY_FORCE_GC           = 5,
};

enum class FlushType {
    WorkingSetOnly,
    StandbyList,
    ModifiedPageList,
    SystemFileCache,
    Full,
    SuperFlush,
    EngineFlush,
    Adaptive,
};

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical,
};

typedef enum _SYSTEM_MEMORY_LIST_COMMAND {
    MemoryCaptureAccessedBits,
    MemoryCaptureAndResetAccessedBits,
    MemoryEmptyWorkingSets,
    MemoryFlushModifiedList,
    MemoryPurgeStandbyList,
    MemoryPurgeLowPriorityStandbyList,
    MemoryCommandMax
} SYSTEM_MEMORY_LIST_COMMAND;

static const INT SystemMemoryListInformation = 0x50;
static const INT SystemFileCacheInformation  = 0x15;

typedef NTSTATUS(NTAPI* pfnNtSetSystemInformation)(
    INT   SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength
);

typedef NTSTATUS(NTAPI* pfnNtQuerySystemInformation)(
    INT   SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength
);

namespace Color {
    enum Code : WORD {
        Default     = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
        Red         = FOREGROUND_RED | FOREGROUND_INTENSITY,
        Green       = FOREGROUND_GREEN | FOREGROUND_INTENSITY,
        Yellow      = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,
        Blue        = FOREGROUND_BLUE | FOREGROUND_INTENSITY,
        Cyan        = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
        Magenta     = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
        White       = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
        DarkGray    = FOREGROUND_INTENSITY,
        DarkRed     = FOREGROUND_RED,
        DarkGreen   = FOREGROUND_GREEN,
        DarkYellow  = FOREGROUND_RED | FOREGROUND_GREEN,
    };

    static HANDLE hConsole = INVALID_HANDLE_VALUE;

    void Init() {
        hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        GetConsoleMode(hConsole, &mode);
        SetConsoleMode(hConsole, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }

    void Set(WORD color) {
        if (hConsole != INVALID_HANDLE_VALUE)
            SetConsoleTextAttribute(hConsole, color);
    }

    void Reset() {
        Set(Default);
    }
}

namespace Fmt {
    std::string BytesToHuman(SIZE_T bytes) {
        const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        int idx = 0;
        double value = static_cast<double>(bytes);
        while (value >= 1024.0 && idx < 4) {
            value /= 1024.0;
            idx++;
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "%.1f %s", value, units[idx]);
        return buf;
    }

    std::string TimeToHMS(int seconds) {
        int h = seconds / 3600;
        int m = (seconds % 3600) / 60;
        int s = seconds % 60;
        char buf[32];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
        return buf;
    }

    std::string Now() {
        auto now = std::chrono::system_clock::now();
        auto t   = std::chrono::system_clock::to_time_t(now);
        struct tm lt;
        localtime_s(&lt, &t);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &lt);
        return buf;
    }

    std::string ProgressBar(double ratio, int width = 30) {
        int filled = static_cast<int>(ratio * width);
        filled = (std::max)(0, (std::min)(width, filled));
        std::string bar = "[";
        for (int i = 0; i < width; i++) {
            if (i < filled)       bar += '#';
            else if (i == filled) bar += '>';
            else                  bar += '-';
        }
        bar += "]";
        char pct[16];
        snprintf(pct, sizeof(pct), " %5.1f%%", ratio * 100.0);
        bar += pct;
        return bar;
    }
}

class Logger {
public:
    static Logger& Instance() {
        static Logger inst;
        return inst;
    }

    void Open(const std::string& filename) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_file.open(filename, std::ios::app);
        if (m_file.is_open()) {
            m_file << "\n\n========================================\n";
            m_file << "  Session started: " << Fmt::Now() << "\n";
            m_file << "  Version: " << APP_VERSION << "\n";
            m_file << "  Build: " << APP_BUILD_DATE << "\n";
            m_file << "========================================\n\n";
        }
    }

    void Close() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_file.is_open()) {
            m_file << "\n[" << Fmt::Now() << "] Session ended.\n";
            m_file.close();
        }
    }

    void SetLevel(LogLevel level) { m_level = level; }
    LogLevel GetLevel() const { return m_level; }

    void Log(LogLevel level, const std::string& message) {
        if (level < m_level) return;

        std::lock_guard<std::mutex> lock(m_mutex);

        WORD color = Color::Default;
        const char* prefix = "";
        switch (level) {
            case LogLevel::Trace:    color = Color::DarkGray;   prefix = "[TRACE]"; break;
            case LogLevel::Debug:    color = Color::DarkGray;   prefix = "[DEBUG]"; break;
            case LogLevel::Info:     color = Color::Cyan;       prefix = "[INFO] "; break;
            case LogLevel::Warning:  color = Color::Yellow;     prefix = "[WARN] "; break;
            case LogLevel::Error:    color = Color::Red;        prefix = "[ERROR]"; break;
            case LogLevel::Critical: color = Color::Red;        prefix = "[CRIT] "; break;
        }

        std::string timestamp = Fmt::Now();
        std::string fullLine  = "[" + timestamp + "] " + prefix + " " + message;

        Color::Set(Color::DarkGray);
        std::cout << "[" << timestamp << "] ";
        Color::Set(color);
        std::cout << prefix << " ";
        Color::Set(Color::White);
        std::cout << message << std::endl;
        Color::Reset();

        if (m_file.is_open()) {
            m_file << fullLine << std::endl;
            m_file.flush();
        }
    }

    void Trace(const std::string& msg)    { Log(LogLevel::Trace, msg); }
    void Debug(const std::string& msg)    { Log(LogLevel::Debug, msg); }
    void Info(const std::string& msg)     { Log(LogLevel::Info, msg); }
    void Warn(const std::string& msg)     { Log(LogLevel::Warning, msg); }
    void Error(const std::string& msg)    { Log(LogLevel::Error, msg); }
    void Critical(const std::string& msg) { Log(LogLevel::Critical, msg); }

private:
    Logger() : m_level(LogLevel::Info) {}
    ~Logger() { Close(); }

    std::ofstream m_file;
    std::mutex    m_mutex;
    LogLevel      m_level;
};

#define LOG_TRACE(msg)    Logger::Instance().Trace(msg)
#define LOG_DEBUG(msg)    Logger::Instance().Debug(msg)
#define LOG_INFO(msg)     Logger::Instance().Info(msg)
#define LOG_WARN(msg)     Logger::Instance().Warn(msg)
#define LOG_ERROR(msg)    Logger::Instance().Error(msg)
#define LOG_CRITICAL(msg) Logger::Instance().Critical(msg)

class Config {
public:
    static Config& Instance() {
        static Config inst;
        return inst;
    }

    bool Load(const std::string& filename) {
        m_filename = filename;
        std::ifstream f(filename);
        if (!f.is_open()) {
            LOG_WARN("Config file not found: " + filename + ". Using defaults.");
            SetDefaults();
            Save(filename);
            return false;
        }

        std::string section;
        std::string line;
        while (std::getline(f, line)) {
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            line = line.substr(start);

            if (line[0] == ';' || line[0] == '#') continue;

            if (line[0] == '[') {
                size_t end = line.find(']');
                if (end != std::string::npos)
                    section = line.substr(1, end - 1);
                continue;
            }

            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = line.substr(0, eq);
                std::string val = line.substr(eq + 1);
                key.erase(key.find_last_not_of(" \t") + 1);
                val.erase(0, val.find_first_not_of(" \t"));
                val.erase(val.find_last_not_of(" \t\r\n") + 1);
                m_data[section + "." + key] = val;
            }
        }

        LOG_INFO("Config loaded from: " + filename);
        return true;
    }

    void Save(const std::string& filename) {
        std::ofstream f(filename);
        if (!f.is_open()) return;

        f << "[General]\n";
        f << "AutoFlushInterval = " << GetInt("General.AutoFlushInterval", 900) << "\n";
        f << "AutoFlushType = " << GetString("General.AutoFlushType", "adaptive") << "\n";
        f << "BeepOnFlush = " << GetInt("General.BeepOnFlush", 1) << "\n";
        f << "LogLevel = " << GetString("General.LogLevel", "info") << "\n\n";

        f << "[Hotkeys]\n";
        f << "ManualFlush = " << GetInt("Hotkeys.ManualFlush", VK_F11) << "\n";
        f << "SuperFlush = " << GetInt("Hotkeys.SuperFlush", VK_F10) << "\n";
        f << "Status = " << GetInt("Hotkeys.Status", VK_F9) << "\n";
        f << "ToggleAdaptive = " << GetInt("Hotkeys.ToggleAdaptive", VK_F8) << "\n\n";

        f << "[Adaptive]\n";
        f << "Enabled = " << GetInt("Adaptive.Enabled", 1) << "\n";
        f << "PageFaultThreshold = " << GetInt("Adaptive.PageFaultThreshold", 50000) << "\n";
        f << "WorkingSetGrowthThresholdMB = " << GetInt("Adaptive.WorkingSetGrowthThresholdMB", 512) << "\n";
        f << "CheckInterval = " << GetInt("Adaptive.CheckInterval", 30) << "\n";
        f << "MinCooldown = " << GetInt("Adaptive.MinCooldown", 120) << "\n\n";

        f << "[SuperFlush]\n";
        f << "PreDelay = " << GetInt("SuperFlush.PreDelay", 150) << "\n";
        f << "KeyDelay = " << GetInt("SuperFlush.KeyDelay", 35) << "\n";
        f << "AutoSendWithFullFlush = " << GetInt("SuperFlush.AutoSendWithFullFlush", 0) << "\n\n";

        f << "[Monitor]\n";
        f << "UpdateInterval = " << GetInt("Monitor.UpdateInterval", 5) << "\n";
        f << "ShowInTitle = " << GetInt("Monitor.ShowInTitle", 1) << "\n";

        LOG_INFO("Config saved to: " + filename);
    }

    std::string GetString(const std::string& key, const std::string& def = "") const {
        auto it = m_data.find(key);
        return (it != m_data.end()) ? it->second : def;
    }

    int GetInt(const std::string& key, int def = 0) const {
        auto it = m_data.find(key);
        if (it != m_data.end()) {
            try { return std::stoi(it->second); }
            catch (...) { return def; }
        }
        return def;
    }

    bool GetBool(const std::string& key, bool def = false) const {
        return GetInt(key, def ? 1 : 0) != 0;
    }

private:
    Config() { SetDefaults(); }

    void SetDefaults() {
        m_data["General.AutoFlushInterval"]               = "900";
        m_data["General.AutoFlushType"]                   = "adaptive";
        m_data["General.BeepOnFlush"]                     = "1";
        m_data["General.LogLevel"]                        = "info";
        m_data["Hotkeys.ManualFlush"]                     = std::to_string(VK_F11);
        m_data["Hotkeys.SuperFlush"]                      = std::to_string(VK_F10);
        m_data["Hotkeys.Status"]                          = std::to_string(VK_F9);
        m_data["Hotkeys.ToggleAdaptive"]                  = std::to_string(VK_F8);
        m_data["Adaptive.Enabled"]                        = "1";
        m_data["Adaptive.PageFaultThreshold"]             = "50000";
        m_data["Adaptive.WorkingSetGrowthThresholdMB"]    = "512";
        m_data["Adaptive.CheckInterval"]                  = "30";
        m_data["Adaptive.MinCooldown"]                    = "120";
        m_data["SuperFlush.PreDelay"]                     = "150";
        m_data["SuperFlush.KeyDelay"]                     = "35";
        m_data["SuperFlush.AutoSendWithFullFlush"]        = "0";
        m_data["Monitor.UpdateInterval"]                  = "5";
        m_data["Monitor.ShowInTitle"]                     = "1";
    }

    std::string                        m_filename;
    std::map<std::string, std::string> m_data;
};

class Privileges {
public:
    static bool Enable(LPCWSTR privilegeName) {
        HANDLE hToken = NULL;
        if (!OpenProcessToken(GetCurrentProcess(),
                              TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
            return false;
        }

        TOKEN_PRIVILEGES tp;
        LUID luid;
        if (!LookupPrivilegeValueW(NULL, privilegeName, &luid)) {
            CloseHandle(hToken);
            return false;
        }

        tp.PrivilegeCount           = 1;
        tp.Privileges[0].Luid       = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        BOOL ok = AdjustTokenPrivileges(hToken, FALSE, &tp,
                                         sizeof(TOKEN_PRIVILEGES), NULL, NULL);
        DWORD err = GetLastError();
        CloseHandle(hToken);
        return (ok && err == ERROR_SUCCESS);
    }

    static int EnableAll() {
        int count = 0;
        struct { LPCWSTR name; const char* desc; } privs[] = {
            { SE_DEBUG_NAME,                  "SeDebugPrivilege"                  },
            { SE_INCREASE_QUOTA_NAME,         "SeIncreaseQuotaPrivilege"          },
            { SE_PROF_SINGLE_PROCESS_NAME,    "SeProfileSingleProcessPrivilege"   },
            { SE_INC_BASE_PRIORITY_NAME,      "SeIncreaseBasePriorityPrivilege"   },
            { SE_INC_WORKING_SET_NAME,        "SeIncreaseWorkingSetSizePrivilege" },
        };

        for (auto& p : privs) {
            if (Enable(p.name)) {
                LOG_DEBUG(std::string("Privilege enabled: ") + p.desc);
                count++;
            } else {
                LOG_WARN(std::string("Failed to enable: ") + p.desc);
            }
        }
        return count;
    }

    static bool IsElevated() {
        BOOL isElevated = FALSE;
        HANDLE hToken   = NULL;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
            TOKEN_ELEVATION elevation;
            DWORD size = sizeof(elevation);
            if (GetTokenInformation(hToken, TokenElevation,
                                    &elevation, sizeof(elevation), &size)) {
                isElevated = elevation.TokenIsElevated;
            }
            CloseHandle(hToken);
        }
        return isElevated != FALSE;
    }
};

struct ProcessInfo {
    DWORD  pid             = 0;
    bool   found           = false;
    bool   is64bit         = false;
    bool   isServer        = false;
    bool   isProfiling     = false;
    std::wstring exeName;
    std::wstring windowTitle;
};

struct ProcessMemoryStats {
    SIZE_T workingSetSize         = 0;
    SIZE_T peakWorkingSetSize     = 0;
    SIZE_T privateUsage           = 0;
    SIZE_T pageFaultCount         = 0;
    SIZE_T pagedPoolUsage         = 0;
    SIZE_T nonPagedPoolUsage      = 0;
    SIZE_T pagefileUsage          = 0;
    double pageFaultsPerSec       = 0.0;
    double workingSetGrowthPerSec = 0.0;
};

class ProcessMonitor {
public:
    ProcessInfo FindArmaProcess() {
        ProcessInfo info;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return info;

        static const wchar_t* targets[] = {
            L"arma3profiling_x64.exe",
            L"arma3serverprofiling_x64.exe",
            L"arma3_x64.exe",
            L"arma3server_x64.exe",
            L"arma3.exe",
            L"arma3server.exe",
        };

        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);

        struct Candidate {
            DWORD       pid;
            std::wstring name;
            int         priority;
        };
        std::vector<Candidate> candidates;

        if (Process32FirstW(snapshot, &pe)) {
            do {
                for (int i = 0; i < _countof(targets); i++) {
                    if (_wcsicmp(pe.szExeFile, targets[i]) == 0) {
                        candidates.push_back({ pe.th32ProcessID, pe.szExeFile, i });
                    }
                }
            } while (Process32NextW(snapshot, &pe));
        }
        CloseHandle(snapshot);

        if (candidates.empty()) return info;

        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate& a, const Candidate& b) {
                      return a.priority < b.priority;
                  });

        auto& best   = candidates[0];
        info.pid      = best.pid;
        info.found    = true;
        info.exeName  = best.name;
        info.is64bit  = (best.name.find(L"x64") != std::wstring::npos);
        info.isServer = (best.name.find(L"server") != std::wstring::npos);
        info.isProfiling = (best.name.find(L"profiling") != std::wstring::npos);

        struct EnumData { DWORD pid; std::wstring title; };
        EnumData ed = { info.pid, L"" };
        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            auto* data = reinterpret_cast<EnumData*>(lParam);
            DWORD wndPid = 0;
            GetWindowThreadProcessId(hwnd, &wndPid);
            if (wndPid == data->pid && IsWindowVisible(hwnd)) {
                wchar_t title[256] = {};
                GetWindowTextW(hwnd, title, 256);
                if (wcslen(title) > 0) {
                    data->title = title;
                    return FALSE;
                }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ed));
        info.windowTitle = ed.title;

        return info;
    }

    ProcessMemoryStats GetMemoryStats(DWORD pid) {
        ProcessMemoryStats stats;
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                       FALSE, pid);
        if (!hProcess) return stats;

        PROCESS_MEMORY_COUNTERS_EX pmc;
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(hProcess,
                                  reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                                  sizeof(pmc))) {
            stats.workingSetSize     = pmc.WorkingSetSize;
            stats.peakWorkingSetSize = pmc.PeakWorkingSetSize;
            stats.privateUsage       = pmc.PrivateUsage;
            stats.pageFaultCount     = pmc.PageFaultCount;
            stats.pagedPoolUsage     = pmc.QuotaPagedPoolUsage;
            stats.nonPagedPoolUsage  = pmc.QuotaNonPagedPoolUsage;
            stats.pagefileUsage      = pmc.PagefileUsage;
        }

        CloseHandle(hProcess);

        auto now = std::chrono::steady_clock::now();
        if (m_lastPid == pid && m_lastPageFaults > 0) {
            double elapsed = std::chrono::duration<double>(now - m_lastTime).count();
            if (elapsed > 0.1) {
                stats.pageFaultsPerSec =
                    static_cast<double>(stats.pageFaultCount - m_lastPageFaults) / elapsed;
                stats.workingSetGrowthPerSec =
                    static_cast<double>(
                        static_cast<long long>(stats.workingSetSize) -
                        static_cast<long long>(m_lastWorkingSet)
                    ) / elapsed;
            }
        }

        m_lastPid         = pid;
        m_lastPageFaults  = stats.pageFaultCount;
        m_lastWorkingSet  = stats.workingSetSize;
        m_lastTime        = now;

        return stats;
    }

    MEMORYSTATUSEX GetSystemMemoryInfo() {
        MEMORYSTATUSEX ms;
        ms.dwLength = sizeof(ms);
        GlobalMemoryStatusEx(&ms);
        return ms;
    }

private:
    DWORD   m_lastPid         = 0;
    SIZE_T  m_lastPageFaults  = 0;
    SIZE_T  m_lastWorkingSet  = 0;
    std::chrono::steady_clock::time_point m_lastTime;
};

struct FlushResult {
    bool   success        = false;
    int    operationsRun  = 0;
    int    operationsFail = 0;
    SIZE_T freedEstimate  = 0;
    double elapsedMs      = 0.0;
    std::string details;
};

class MemoryCleaner {
public:
    MemoryCleaner() {
        m_hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (m_hNtdll) {
            m_pNtSetSystemInformation = reinterpret_cast<pfnNtSetSystemInformation>(
                GetProcAddress(m_hNtdll, "NtSetSystemInformation"));
            m_pNtQuerySystemInformation = reinterpret_cast<pfnNtQuerySystemInformation>(
                GetProcAddress(m_hNtdll, "NtQuerySystemInformation"));
        }
    }

    bool FlushWorkingSet(DWORD pid) {
        HANDLE hProcess = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_INFORMATION,
                                       FALSE, pid);
        if (!hProcess) {
            LOG_ERROR("Failed to open process (PID: " + std::to_string(pid) +
                      "), error: " + std::to_string(GetLastError()));
            return false;
        }

        PROCESS_MEMORY_COUNTERS pmc;
        pmc.cb = sizeof(pmc);
        SIZE_T before = 0;
        if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc)))
            before = pmc.WorkingSetSize;

        BOOL ok = EmptyWorkingSet(hProcess);

        SIZE_T after = 0;
        if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc)))
            after = pmc.WorkingSetSize;

        CloseHandle(hProcess);

        if (ok && before > after) {
            LOG_INFO("Working Set flushed: " + Fmt::BytesToHuman(before) +
                     " -> " + Fmt::BytesToHuman(after) +
                     " (freed ~" + Fmt::BytesToHuman(before - after) + ")");
        } else if (ok) {
            LOG_INFO("Working Set flush executed (no significant reduction).");
        }

        return ok != FALSE;
    }

    bool PurgeStandbyList() {
        if (!m_pNtSetSystemInformation) {
            LOG_ERROR("NtSetSystemInformation not available!");
            return false;
        }

        SYSTEM_MEMORY_LIST_COMMAND cmd = MemoryPurgeStandbyList;
        NTSTATUS status = m_pNtSetSystemInformation(
            SystemMemoryListInformation, &cmd, sizeof(cmd));

        if (status >= 0) {
            LOG_INFO("Standby List purged successfully.");
            return true;
        } else {
            LOG_ERROR("Standby List purge failed (NTSTATUS: 0x" +
                      ToHex(static_cast<unsigned>(status)) + ")");
            return false;
        }
    }

    bool FlushModifiedPageList() {
        if (!m_pNtSetSystemInformation) return false;

        SYSTEM_MEMORY_LIST_COMMAND cmd = MemoryFlushModifiedList;
        NTSTATUS status = m_pNtSetSystemInformation(
            SystemMemoryListInformation, &cmd, sizeof(cmd));

        if (status >= 0) {
            Logger::Instance().Info("Modified Page List flushed.");
            return true;
        }
        LOG_ERROR("Modified Page List flush failed.");
        return false;
    }

    bool EmptySystemWorkingSets() {
        if (!m_pNtSetSystemInformation) return false;

        SYSTEM_MEMORY_LIST_COMMAND cmd = MemoryEmptyWorkingSets;
        NTSTATUS status = m_pNtSetSystemInformation(
            SystemMemoryListInformation, &cmd, sizeof(cmd));

        if (status >= 0) {
            Logger::Instance().Info("System-wide Working Sets emptied.");
            return true;
        }
        return false;
    }

    bool PurgeSystemFileCache() {
        SIZE_T minCache = static_cast<SIZE_T>(-1);
        SIZE_T maxCache = static_cast<SIZE_T>(-1);
        DWORD  flags    = 0;

        if (SetSystemFileCacheSize(minCache, maxCache, flags)) {
            Logger::Instance().Info("System File Cache purged.");
            return true;
        }
        return false;
    }

    FlushResult PerformFlush(FlushType type, DWORD armaPid = 0) {
        FlushResult result;
        auto start = std::chrono::high_resolution_clock::now();

        MEMORYSTATUSEX msBefore;
        msBefore.dwLength = sizeof(msBefore);
        GlobalMemoryStatusEx(&msBefore);

        switch (type) {
            case FlushType::WorkingSetOnly:
                if (armaPid) {
                    result.success = FlushWorkingSet(armaPid);
                    result.operationsRun++;
                    if (!result.success) result.operationsFail++;
                }
                break;

            case FlushType::StandbyList:
                result.success = PurgeStandbyList();
                result.operationsRun++;
                if (!result.success) result.operationsFail++;
                break;

            case FlushType::ModifiedPageList:
                result.success = FlushModifiedPageList();
                result.operationsRun++;
                if (!result.success) result.operationsFail++;
                break;

            case FlushType::SystemFileCache:
                result.success = PurgeSystemFileCache();
                result.operationsRun++;
                if (!result.success) result.operationsFail++;
                break;

            case FlushType::Full:
            case FlushType::Adaptive: {
                int ok = 0, fail = 0;
                if (FlushModifiedPageList()) ok++; else fail++;
                if (PurgeStandbyList()) ok++; else fail++;
                PurgeSystemFileCache();
                if (armaPid) {
                    if (FlushWorkingSet(armaPid)) ok++; else fail++;
                }
                result.operationsRun  = ok + fail;
                result.operationsFail = fail;
                result.success        = (ok > 0);
                break;
            }

            case FlushType::SuperFlush:
                result.success = SendSuperFlush();
                result.operationsRun++;
                if (!result.success) result.operationsFail++;
                break;

            case FlushType::EngineFlush:
                result.success = SendEngineFlush();
                result.operationsRun++;
                if (!result.success) result.operationsFail++;
                break;
        }

        auto end = std::chrono::high_resolution_clock::now();
        result.elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();

        MEMORYSTATUSEX msAfter;
        msAfter.dwLength = sizeof(msAfter);
        GlobalMemoryStatusEx(&msAfter);

        if (msAfter.ullAvailPhys > msBefore.ullAvailPhys)
            result.freedEstimate = static_cast<SIZE_T>(
                msAfter.ullAvailPhys - msBefore.ullAvailPhys);

        std::ostringstream oss;
        oss << "Flush complete: type=" << FlushTypeName(type)
            << ", ops=" << result.operationsRun
            << ", fails=" << result.operationsFail
            << ", freed~" << Fmt::BytesToHuman(result.freedEstimate)
            << ", took " << std::fixed << std::setprecision(1)
            << result.elapsedMs << "ms";
        result.details = oss.str();

        if (result.success) {
            LOG_INFO(result.details);
            if (Config::Instance().GetBool("General.BeepOnFlush", true)) {
                Beep(800, 120);
                Beep(1000, 80);
            }
        }

        m_totalFlushes++;
        m_totalFreed += result.freedEstimate;

        return result;
    }

    bool SendSuperFlush() {
        return SendCheatCommand("SUPERFLUSH");
    }

    bool SendEngineFlush() {
        return SendCheatCommand("FLUSH");
    }

    int    GetTotalFlushes() const { return m_totalFlushes; }
    SIZE_T GetTotalFreed()   const { return m_totalFreed; }

    static const char* FlushTypeName(FlushType type) {
        switch (type) {
            case FlushType::WorkingSetOnly:   return "WorkingSet";
            case FlushType::StandbyList:      return "StandbyList";
            case FlushType::ModifiedPageList: return "ModifiedPageList";
            case FlushType::SystemFileCache:  return "SystemFileCache";
            case FlushType::Full:             return "Full";
            case FlushType::SuperFlush:       return "SUPERFLUSH";
            case FlushType::EngineFlush:      return "FLUSH";
            case FlushType::Adaptive:         return "Adaptive";
            default:                          return "Unknown";
        }
    }

private:
    bool SendCheatCommand(const char* command) {
        HWND hwnd = NULL;

        static const wchar_t* titles[] = {
            L"Arma 3",
            L"Arma 3 - Pair Programming",
            L"Arma 3 - Profiling",
        };

        for (auto& title : titles) {
            hwnd = FindWindowW(NULL, title);
            if (hwnd) break;
        }

        if (!hwnd) {
            hwnd = FindWindowW(L"Arma 3", NULL);
        }

        if (!hwnd) {
            ProcessMonitor pm;
            ProcessInfo info = pm.FindArmaProcess();
            if (info.found) {
                struct EnumCtx { DWORD pid; HWND result; };
                EnumCtx ctx = { info.pid, NULL };
                EnumWindows([](HWND h, LPARAM lp) -> BOOL {
                    auto* c = reinterpret_cast<EnumCtx*>(lp);
                    DWORD p;
                    GetWindowThreadProcessId(h, &p);
                    if (p == c->pid && IsWindowVisible(h)) {
                        c->result = h;
                        return FALSE;
                    }
                    return TRUE;
                }, reinterpret_cast<LPARAM>(&ctx));
                hwnd = ctx.result;
            }
        }

        if (!hwnd) {
            LOG_ERROR("Arma 3 window not found! Cannot send " +
                      std::string(command) + ".");
            return false;
        }

        int preDelay = Config::Instance().GetInt("SuperFlush.PreDelay", 150);
        int keyDelay = Config::Instance().GetInt("SuperFlush.KeyDelay", 35);

        Logger::Instance().Info("Sending " + std::string(command) +
                  " to Arma 3 (HWND: 0x" + ToHex(reinterpret_cast<uintptr_t>(hwnd)) + ")...");

        DWORD armaTid = GetWindowThreadProcessId(hwnd, NULL);
        DWORD ourTid  = GetCurrentThreadId();
        AttachThreadInput(ourTid, armaTid, TRUE);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
        AttachThreadInput(ourTid, armaTid, FALSE);

        Sleep(preDelay);

        INPUT inputs[128];
        int idx = 0;

        inputs[idx] = {};
        inputs[idx].type           = INPUT_KEYBOARD;
        inputs[idx].ki.wVk         = VK_LSHIFT;
        inputs[idx].ki.dwFlags     = 0;
        idx++;

        inputs[idx] = {};
        inputs[idx].type           = INPUT_KEYBOARD;
        inputs[idx].ki.wVk         = VK_SUBTRACT;
        inputs[idx].ki.dwFlags     = 0;
        idx++;

        inputs[idx] = {};
        inputs[idx].type           = INPUT_KEYBOARD;
        inputs[idx].ki.wVk         = VK_SUBTRACT;
        inputs[idx].ki.dwFlags     = KEYEVENTF_KEYUP;
        idx++;

        inputs[idx] = {};
        inputs[idx].type           = INPUT_KEYBOARD;
        inputs[idx].ki.wVk         = VK_LSHIFT;
        inputs[idx].ki.dwFlags     = KEYEVENTF_KEYUP;
        idx++;

        SendInput(idx, inputs, sizeof(INPUT));
        Sleep(preDelay);

        for (int i = 0; command[i] != '\0'; i++) {
            INPUT keyDown = {};
            keyDown.type       = INPUT_KEYBOARD;
            keyDown.ki.wVk     = VkKeyScanA(command[i]) & 0xFF;
            keyDown.ki.dwFlags = 0;

            INPUT keyUp = {};
            keyUp.type       = INPUT_KEYBOARD;
            keyUp.ki.wVk     = keyDown.ki.wVk;
            keyUp.ki.dwFlags = KEYEVENTF_KEYUP;

            SendInput(1, &keyDown, sizeof(INPUT));
            Sleep(keyDelay);
            SendInput(1, &keyUp, sizeof(INPUT));
            Sleep(keyDelay);
        }

        LOG_INFO(std::string(command) + " command sent to engine!");
        return true;
    }

    static std::string ToHex(unsigned long long val) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%llX", val);
        return buf;
    }

    HMODULE                     m_hNtdll                    = NULL;
    pfnNtSetSystemInformation   m_pNtSetSystemInformation   = nullptr;
    pfnNtQuerySystemInformation m_pNtQuerySystemInformation = nullptr;

    int    m_totalFlushes = 0;
    SIZE_T m_totalFreed   = 0;
};

class AdaptiveMonitor {
public:
    struct Decision {
        bool        shouldFlush     = false;
        FlushType   recommendedType = FlushType::Full;
        std::string reason;
    };

    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }
    void ToggleEnabled() { m_enabled = !m_enabled; }

    Decision Evaluate(const ProcessMemoryStats& stats,
                      const MEMORYSTATUSEX& sysMem) {
        Decision d;
        if (!m_enabled) return d;

        auto now = std::chrono::steady_clock::now();
        double sinceLast = std::chrono::duration<double>(now - m_lastFlushTime).count();
        int cooldown = Config::Instance().GetInt("Adaptive.MinCooldown", 120);

        if (sinceLast < cooldown) return d;

        int pfThreshold = Config::Instance().GetInt("Adaptive.PageFaultThreshold", 50000);
        int wsGrowthMB  = Config::Instance().GetInt("Adaptive.WorkingSetGrowthThresholdMB", 512);

        if (stats.pageFaultsPerSec > pfThreshold) {
            d.shouldFlush     = true;
            d.recommendedType = FlushType::Full;
            d.reason = "Page Faults/sec = " +
                       std::to_string(static_cast<int>(stats.pageFaultsPerSec)) +
                       " (threshold: " + std::to_string(pfThreshold) + ")";
            m_lastFlushTime = now;
            return d;
        }

        if (stats.workingSetGrowthPerSec > static_cast<double>(wsGrowthMB) * 1024.0 * 1024.0 / 60.0) {
            d.shouldFlush     = true;
            d.recommendedType = FlushType::Full;
            d.reason = "Working Set growing at " +
                       Fmt::BytesToHuman(static_cast<SIZE_T>(stats.workingSetGrowthPerSec)) +
                       "/sec";
            m_lastFlushTime = now;
            return d;
        }

        if (sysMem.dwMemoryLoad > 85) {
            d.shouldFlush     = true;
            d.recommendedType = FlushType::Full;
            d.reason = "System memory load: " + std::to_string(sysMem.dwMemoryLoad) + "%";
            m_lastFlushTime = now;
            return d;
        }

        if (stats.workingSetSize > 8ULL * 1024 * 1024 * 1024) {
            d.shouldFlush     = true;
            d.recommendedType = FlushType::Full;
            d.reason = "Working Set exceeded 8 GB (" +
                       Fmt::BytesToHuman(stats.workingSetSize) + ")";
            m_lastFlushTime = now;
            return d;
        }

        return d;
    }

    void ResetCooldown() {
        m_lastFlushTime = std::chrono::steady_clock::now();
    }

private:
    bool m_enabled = true;
    std::chrono::steady_clock::time_point m_lastFlushTime;
};

struct SessionStats {
    std::chrono::steady_clock::time_point startTime;
    int    manualFlushes        = 0;
    int    autoFlushes          = 0;
    int    adaptiveFlushes      = 0;
    int    superFlushes         = 0;
    SIZE_T totalFreed           = 0;
    SIZE_T peakWorkingSet       = 0;
    double peakPageFaultsPerSec = 0.0;

    std::string GetUptime() const {
        auto now = std::chrono::steady_clock::now();
        int secs = static_cast<int>(
            std::chrono::duration<double>(now - startTime).count());
        return Fmt::TimeToHMS(secs);
    }
};

class Application {
public:
    Application() {
        m_stats.startTime = std::chrono::steady_clock::now();
    }

    int Run() {
        SetConsoleTitleW(APP_NAME);
        Color::Init();
        PrintBanner();

        if (!Privileges::IsElevated()) {
            Color::Set(Color::Red);
            std::cout << "\n  [!] RUN AS ADMINISTRATOR!\n\n";
            Color::Reset();
            LOG_CRITICAL("Not running as Administrator!");
            std::cout << "Press Enter to exit...";
            std::cin.get();
            return 1;
        }

        Config::Instance().Load(CONFIG_FILE);

        Logger::Instance().Open(LOG_FILE);
        std::string logLvl = Config::Instance().GetString("General.LogLevel", "info");
        if      (logLvl == "trace")   Logger::Instance().SetLevel(LogLevel::Trace);
        else if (logLvl == "debug")   Logger::Instance().SetLevel(LogLevel::Debug);
        else if (logLvl == "info")    Logger::Instance().SetLevel(LogLevel::Info);
        else if (logLvl == "warning") Logger::Instance().SetLevel(LogLevel::Warning);
        else if (logLvl == "error")   Logger::Instance().SetLevel(LogLevel::Error);

        LOG_INFO("=== Arma 3 Hash & Cache Cleaner v" + std::string(APP_VERSION) + " ===");

        int privCount = Privileges::EnableAll();
        LOG_INFO("Enabled " + std::to_string(privCount) + "/5 system privileges.");

        m_adaptiveMonitor.SetEnabled(
            Config::Instance().GetBool("Adaptive.Enabled", true));

        RegisterHotkeys();

        m_running = true;
        m_monitorThread = std::thread(&Application::MonitorLoop, this);
        m_timerThread = std::thread(&Application::TimerLoop, this);

        PrintHelp();

        MSG msg = {};
        while (GetMessage(&msg, NULL, 0, 0) > 0) {
            if (msg.message == WM_HOTKEY) {
                HandleHotkey(static_cast<int>(msg.wParam));
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        m_running = false;
        if (m_monitorThread.joinable()) m_monitorThread.join();
        if (m_timerThread.joinable())   m_timerThread.join();

        PrintSessionSummary();
        return 0;
    }

private:
    void PrintBanner() {
        Color::Set(Color::Cyan);
        std::cout << R"(
   ___   ____  __  ______   ____  _    _____   ___   _  __ ___ ___
  / _ | / __ \/  |/  / _ | |_  / | |  / / __/ / _ | / |/ // __/ _ \
 / __ |/ /_/ / /|_/ / __ | /_ <  | | / / _/  / __ |/    // _// , _/
/_/ |_|\____/_/  /_/_/ |_|/____/ |_|/_/___/ /_/ |_/_/|_//___/_/|_|
        )" << std::endl;

        Color::Set(Color::White);
        std::cout << "  Hash & Cache Cleaner v" << APP_VERSION << std::endl;
        Color::Set(Color::DarkGray);
        std::cout << "  Build: " << APP_BUILD_DATE << std::endl;
        std::cout << std::endl;
        Color::Reset();
    }

    void PrintHelp() {
        std::cout << std::endl;
        Color::Set(Color::Yellow);
        std::cout << "  ========== HOTKEYS ==========\n";
        Color::Set(Color::White);
        std::cout << "  F11  - Full memory flush (Working Set + Standby List)\n";
        std::cout << "  F10  - Send SUPERFLUSH to Arma 3 engine\n";
        std::cout << "  F9   - Show current process stats\n";
        std::cout << "  F8   - Toggle adaptive auto-flush mode\n";
        Color::Set(Color::Yellow);
        std::cout << "  =============================\n\n";
        Color::Reset();
    }

    void RegisterHotkeys() {
        struct { int id; UINT vk; const char* name; } keys[] = {
            { HOTKEY_MANUAL_FLUSH,    (UINT)Config::Instance().GetInt("Hotkeys.ManualFlush", VK_F11), "F11" },
            { HOTKEY_SUPERFLUSH,      (UINT)Config::Instance().GetInt("Hotkeys.SuperFlush",  VK_F10), "F10" },
            { HOTKEY_STATUS,          (UINT)Config::Instance().GetInt("Hotkeys.Status",      VK_F9),  "F9"  },
            { HOTKEY_TOGGLE_ADAPTIVE, (UINT)Config::Instance().GetInt("Hotkeys.ToggleAdaptive", VK_F8), "F8" },
        };

        for (auto& k : keys) {
            if (RegisterHotKey(NULL, k.id, 0, k.vk)) {
                LOG_DEBUG(std::string("Hotkey registered: ") + k.name);
            } else {
                LOG_WARN(std::string("Failed to register hotkey: ") + k.name);
            }
        }
    }

    void HandleHotkey(int id) {
        ProcessInfo arma = m_processMonitor.FindArmaProcess();

        switch (id) {
            case HOTKEY_MANUAL_FLUSH: {
                LOG_INFO("=== MANUAL FLUSH (F11) ===");
                FlushResult res = m_cleaner.PerformFlush(FlushType::Full, arma.pid);
                m_stats.manualFlushes++;
                m_stats.totalFreed += res.freedEstimate;

                if (Config::Instance().GetBool("SuperFlush.AutoSendWithFullFlush", false) &&
                    arma.found && !arma.isServer) {
                    Sleep(500);
                    m_cleaner.SendSuperFlush();
                    m_stats.superFlushes++;
                }
                break;
            }

            case HOTKEY_SUPERFLUSH: {
                LOG_INFO("=== SUPERFLUSH (F10) ===");
                if (!arma.found) {
                    LOG_WARN("Arma 3 process not found!");
                    Beep(300, 200);
                    break;
                }
                if (arma.isServer) {
                    LOG_WARN("SUPERFLUSH unavailable on server process (no window).");
                    Beep(300, 200);
                    break;
                }

                m_cleaner.PerformFlush(FlushType::Full, arma.pid);
                Sleep(300);
                FlushResult res = m_cleaner.PerformFlush(FlushType::SuperFlush, arma.pid);
                m_stats.superFlushes++;
                m_stats.totalFreed += res.freedEstimate;
                break;
            }

            case HOTKEY_STATUS: {
                PrintDetailedStatus(arma);
                break;
            }

            case HOTKEY_TOGGLE_ADAPTIVE: {
                m_adaptiveMonitor.ToggleEnabled();
                bool state = m_adaptiveMonitor.IsEnabled();
                LOG_INFO(std::string("Adaptive mode: ") + (state ? "ENABLED" : "DISABLED"));
                Beep(state ? 1200 : 400, 150);
                break;
            }
        }
    }

    void PrintDetailedStatus(const ProcessInfo& arma) {
        std::cout << std::endl;
        Color::Set(Color::Yellow);
        std::cout << "  ================ STATUS ================\n";
        Color::Reset();

        Color::Set(Color::White);
        std::cout << "  Process:    ";
        if (arma.found) {
            Color::Set(Color::Green);
            std::wcout << arma.exeName;
            std::cout << " (PID: " << arma.pid << ")";
            if (arma.isProfiling) {
                Color::Set(Color::Magenta);
                std::cout << " [PROFILING]";
            }
            if (arma.isServer) {
                Color::Set(Color::Blue);
                std::cout << " [SERVER]";
            }
            std::cout << std::endl;
        } else {
            Color::Set(Color::Red);
            std::cout << "NOT FOUND" << std::endl;
        }

        if (arma.found) {
            ProcessMemoryStats mem = m_processMonitor.GetMemoryStats(arma.pid);

            Color::Set(Color::White);
            std::cout << "  Working Set:     ";
            Color::Set(mem.workingSetSize > 6ULL * 1024 * 1024 * 1024 ? Color::Red : Color::Green);
            std::cout << Fmt::BytesToHuman(mem.workingSetSize);
            Color::Set(Color::DarkGray);
            std::cout << "  (peak: " << Fmt::BytesToHuman(mem.peakWorkingSetSize) << ")" << std::endl;

            Color::Set(Color::White);
            std::cout << "  Private Bytes:   ";
            Color::Set(Color::Cyan);
            std::cout << Fmt::BytesToHuman(mem.privateUsage) << std::endl;

            Color::Set(Color::White);
            std::cout << "  Page Faults/sec: ";
            Color::Set(mem.pageFaultsPerSec > 30000 ? Color::Red : Color::Green);
            std::cout << static_cast<int>(mem.pageFaultsPerSec) << std::endl;

            Color::Set(Color::White);
            std::cout << "  WS Growth/sec:   ";
            if (mem.workingSetGrowthPerSec > 0) {
                Color::Set(Color::Yellow);
                std::cout << "+" << Fmt::BytesToHuman(static_cast<SIZE_T>(mem.workingSetGrowthPerSec));
            } else {
                Color::Set(Color::Green);
                std::cout << Fmt::BytesToHuman(0);
            }
            std::cout << std::endl;

            double ratio = static_cast<double>(mem.workingSetSize) /
                           static_cast<double>(mem.peakWorkingSetSize > 0 ? mem.peakWorkingSetSize : 1);
            Color::Set(Color::White);
            std::cout << "  RAM Usage:       ";
            Color::Set(ratio > 0.8 ? Color::Red : (ratio > 0.5 ? Color::Yellow : Color::Green));
            std::cout << Fmt::ProgressBar(ratio, 25) << std::endl;

            if (mem.workingSetSize > m_stats.peakWorkingSet)
                m_stats.peakWorkingSet = mem.workingSetSize;
            if (mem.pageFaultsPerSec > m_stats.peakPageFaultsPerSec)
                m_stats.peakPageFaultsPerSec = mem.pageFaultsPerSec;
        }

        MEMORYSTATUSEX sysMem = m_processMonitor.GetSystemMemoryInfo();
        Color::Set(Color::White);
        std::cout << "  System RAM:      ";
        Color::Set(sysMem.dwMemoryLoad > 85 ? Color::Red : Color::Green);
        std::cout << Fmt::BytesToHuman(sysMem.ullTotalPhys - sysMem.ullAvailPhys)
                  << " / " << Fmt::BytesToHuman(sysMem.ullTotalPhys)
                  << " (" << sysMem.dwMemoryLoad << "% used)" << std::endl;

        Color::Set(Color::White);
        std::cout << "  Session uptime:  ";
        Color::Set(Color::Cyan);
        std::cout << m_stats.GetUptime() << std::endl;

        Color::Set(Color::White);
        std::cout << "  Total flushes:   ";
        Color::Set(Color::Cyan);
        std::cout << (m_stats.manualFlushes + m_stats.autoFlushes + m_stats.adaptiveFlushes)
                  << " (manual: " << m_stats.manualFlushes
                  << ", auto: " << m_stats.autoFlushes
                  << ", adaptive: " << m_stats.adaptiveFlushes
                  << ")" << std::endl;

        Color::Set(Color::White);
        std::cout << "  SUPERFLUSH sent: ";
        Color::Set(Color::Magenta);
        std::cout << m_stats.superFlushes << std::endl;

        Color::Set(Color::White);
        std::cout << "  Total freed:     ";
        Color::Set(Color::Green);
        std::cout << Fmt::BytesToHuman(m_stats.totalFreed) << std::endl;

        Color::Set(Color::White);
        std::cout << "  Adaptive mode:   ";
        Color::Set(m_adaptiveMonitor.IsEnabled() ? Color::Green : Color::DarkGray);
        std::cout << (m_adaptiveMonitor.IsEnabled() ? "ENABLED" : "DISABLED") << std::endl;

        Color::Set(Color::Yellow);
        std::cout << "  =========================================\n\n";
        Color::Reset();
    }

    void MonitorLoop() {
        while (m_running) {
            int interval = Config::Instance().GetInt("Monitor.UpdateInterval", 5);

            for (int i = 0; i < interval * 10 && m_running; i++)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

            if (!m_running) break;

            ProcessInfo arma = m_processMonitor.FindArmaProcess();

            if (arma.found) {
                ProcessMemoryStats mem = m_processMonitor.GetMemoryStats(arma.pid);
                MEMORYSTATUSEX sysMem  = m_processMonitor.GetSystemMemoryInfo();

                if (Config::Instance().GetBool("Monitor.ShowInTitle", true)) {
                    std::wstring title = std::wstring(APP_NAME_SHORT) +
                        L" | WS: " + ToWide(Fmt::BytesToHuman(mem.workingSetSize)) +
                        L" | PF/s: " + std::to_wstring(static_cast<int>(mem.pageFaultsPerSec)) +
                        L" | RAM: " + std::to_wstring(sysMem.dwMemoryLoad) + L"%";
                    SetConsoleTitleW(title.c_str());
                }

                if (mem.workingSetSize > m_stats.peakWorkingSet)
                    m_stats.peakWorkingSet = mem.workingSetSize;
                if (mem.pageFaultsPerSec > m_stats.peakPageFaultsPerSec)
                    m_stats.peakPageFaultsPerSec = mem.pageFaultsPerSec;

                if (m_adaptiveMonitor.IsEnabled()) {
                    auto decision = m_adaptiveMonitor.Evaluate(mem, sysMem);
                    if (decision.shouldFlush) {
                        LOG_WARN("=== ADAPTIVE FLUSH: " + decision.reason + " ===");
                        FlushResult res = m_cleaner.PerformFlush(
                            decision.recommendedType, arma.pid);
                        m_stats.adaptiveFlushes++;
                        m_stats.totalFreed += res.freedEstimate;
                    }
                }

                LOG_TRACE("Monitor: WS=" + Fmt::BytesToHuman(mem.workingSetSize) +
                          " PF/s=" + std::to_string(static_cast<int>(mem.pageFaultsPerSec)));
            } else {
                if (Config::Instance().GetBool("Monitor.ShowInTitle", true)) {
                    SetConsoleTitleW((std::wstring(APP_NAME_SHORT) +
                                     L" | Waiting for Arma 3...").c_str());
                }
            }
        }
    }

    void TimerLoop() {
        int interval = Config::Instance().GetInt("General.AutoFlushInterval", 900);
        if (interval <= 0) {
            LOG_INFO("Auto-flush timer disabled.");
            return;
        }

        LOG_INFO("Auto-flush timer: every " + Fmt::TimeToHMS(interval));

        while (m_running) {
            for (int i = 0; i < interval && m_running; i++)
                std::this_thread::sleep_for(std::chrono::seconds(1));

            if (!m_running) break;

            ProcessInfo arma = m_processMonitor.FindArmaProcess();
            if (!arma.found) continue;

            std::string typeStr = Config::Instance().GetString("General.AutoFlushType", "adaptive");
            FlushType type = FlushType::Full;
            if      (typeStr == "workingset")  type = FlushType::WorkingSetOnly;
            else if (typeStr == "standby")     type = FlushType::StandbyList;
            else if (typeStr == "superflush")  type = FlushType::SuperFlush;
            else if (typeStr == "adaptive")    type = FlushType::Adaptive;
            else if (typeStr == "full")        type = FlushType::Full;

            LOG_INFO("=== AUTO-FLUSH ===");
            FlushResult res = m_cleaner.PerformFlush(type, arma.pid);
            m_stats.autoFlushes++;
            m_stats.totalFreed += res.freedEstimate;

            if (Config::Instance().GetBool("SuperFlush.AutoSendWithFullFlush", false) &&
                arma.found && !arma.isServer) {
                Sleep(500);
                m_cleaner.SendSuperFlush();
                m_stats.superFlushes++;
            }
        }
    }

    void PrintSessionSummary() {
        std::cout << std::endl;
        Color::Set(Color::Yellow);
        std::cout << "  =========== SESSION SUMMARY ===========\n";
        Color::Set(Color::White);
        std::cout << "  Uptime:              " << m_stats.GetUptime() << std::endl;
        std::cout << "  Manual flushes:      " << m_stats.manualFlushes << std::endl;
        std::cout << "  Auto flushes:        " << m_stats.autoFlushes << std::endl;
        std::cout << "  Adaptive flushes:    " << m_stats.adaptiveFlushes << std::endl;
        std::cout << "  SUPERFLUSH commands: " << m_stats.superFlushes << std::endl;
        std::cout << "  Total freed (est):   " << Fmt::BytesToHuman(m_stats.totalFreed) << std::endl;
        std::cout << "  Peak Working Set:    " << Fmt::BytesToHuman(m_stats.peakWorkingSet) << std::endl;
        std::cout << "  Peak PF/sec:         " << static_cast<int>(m_stats.peakPageFaultsPerSec) << std::endl;
        Color::Set(Color::Yellow);
        std::cout << "  ========================================\n\n";
        Color::Reset();
    }

    static std::wstring ToWide(const std::string& s) {
        return std::wstring(s.begin(), s.end());
    }

    std::atomic<bool>  m_running{false};
    std::thread        m_monitorThread;
    std::thread        m_timerThread;

    ProcessMonitor     m_processMonitor;
    MemoryCleaner      m_cleaner;
    AdaptiveMonitor    m_adaptiveMonitor;
    SessionStats       m_stats;
};

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    Application app;
    return app.Run();
}
