#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#pragma comment(lib, "psapi.lib")

static const char* DLL_VERSION = "2.0.0";
static const char* DLL_NAME    = "a3_cleaner";

static int    g_flushCount        = 0;
static int    g_trimCount         = 0;
static int    g_fullFlushCount    = 0;
static DWORD  g_initTime          = 0;
static SIZE_T g_lastWorkingSet    = 0;
static SIZE_T g_totalFreed        = 0;

typedef enum _SYSTEM_MEMORY_LIST_COMMAND {
    MemoryCaptureAccessedBits,
    MemoryCaptureAndResetAccessedBits,
    MemoryEmptyWorkingSets,
    MemoryFlushModifiedList,
    MemoryPurgeStandbyList,
    MemoryPurgeLowPriorityStandbyList,
    MemoryCommandMax
} SYSTEM_MEMORY_LIST_COMMAND;

typedef LONG (NTAPI *pfnNtSetSystemInformation)(
    INT   SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength
);

static bool EnablePrivilege(LPCWSTR privName) {
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return false;

    TOKEN_PRIVILEGES tp;
    LUID luid;
    if (!LookupPrivilegeValueW(NULL, privName, &luid)) {
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

static SIZE_T GetCurrentWorkingSet() {
    PROCESS_MEMORY_COUNTERS pmc;
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return pmc.WorkingSetSize;
    return 0;
}

static SIZE_T GetCurrentPrivateBytes() {
    PROCESS_MEMORY_COUNTERS_EX pmc;
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                              sizeof(pmc)))
        return pmc.PrivateUsage;
    return 0;
}

static DWORD GetPageFaultCount() {
    PROCESS_MEMORY_COUNTERS pmc;
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return pmc.PageFaultCount;
    return 0;
}

static const char* FormatBytes(SIZE_T bytes, char* buf, size_t bufSize) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int idx = 0;
    double val = (double)bytes;
    while (val >= 1024.0 && idx < 3) { val /= 1024.0; idx++; }
    snprintf(buf, bufSize, "%.1f%s", val, units[idx]);
    return buf;
}

static bool DoFlush() {
    SIZE_T before = GetCurrentWorkingSet();
    BOOL ok = EmptyWorkingSet(GetCurrentProcess());
    SIZE_T after = GetCurrentWorkingSet();

    if (ok && before > after) {
        g_totalFreed += (before - after);
    }

    g_lastWorkingSet = after;
    g_flushCount++;
    return ok != FALSE;
}

static int DoTrim() {
    HANDLE heaps[64];
    DWORD count = GetProcessHeaps(64, heaps);
    int compacted = 0;
    for (DWORD i = 0; i < count && i < 64; i++) {
        if (HeapCompact(heaps[i], 0) > 0)
            compacted++;
    }
    g_trimCount++;
    return compacted;
}

static bool DoShrink() {
    HANDLE hProcess = GetCurrentProcess();
    SIZE_T current = GetCurrentWorkingSet();
    SIZE_T minWS = 1024 * 1024;
    SIZE_T maxWS = (current * 3) / 4;
    if (maxWS < minWS) maxWS = minWS * 2;

    return SetProcessWorkingSetSize(hProcess, minWS, maxWS) != FALSE;
}

static bool DoStandbyPurge() {
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll) return false;

    pfnNtSetSystemInformation pNtSetSystemInfo =
        (pfnNtSetSystemInformation)GetProcAddress(hNtdll, "NtSetSystemInformation");
    if (!pNtSetSystemInfo) return false;

    SYSTEM_MEMORY_LIST_COMMAND cmd = MemoryPurgeStandbyList;
    LONG status = pNtSetSystemInfo(0x50, &cmd, sizeof(cmd));
    return (status >= 0);
}

static void DoFullFlush(char* output, int outputSize) {
    SIZE_T before = GetCurrentWorkingSet();

    bool flushOk   = DoFlush();
    int  trimCount = DoTrim();
    bool shrinkOk  = DoShrink();

    SIZE_T after = GetCurrentWorkingSet();
    SIZE_T freed = (before > after) ? (before - after) : 0;
    g_totalFreed += freed;
    g_fullFlushCount++;

    char freedStr[32], beforeStr[32], afterStr[32];
    FormatBytes(freed, freedStr, sizeof(freedStr));
    FormatBytes(before, beforeStr, sizeof(beforeStr));
    FormatBytes(after, afterStr, sizeof(afterStr));

    snprintf(output, outputSize,
             "OK:fullflush|before=%s|after=%s|freed=%s|heaps=%d|flush=%s|shrink=%s",
             beforeStr, afterStr, freedStr, trimCount,
             flushOk ? "ok" : "fail",
             shrinkOk ? "ok" : "fail");
}

extern "C" {

__declspec(dllexport) void __stdcall RVExtension(
    char*       output,
    int         outputSize,
    const char* function)
{
    if (!function || outputSize < 2) {
        if (output && outputSize > 0) output[0] = '\0';
        return;
    }

    if (_stricmp(function, "flush") == 0) {
        SIZE_T before = GetCurrentWorkingSet();
        bool ok = DoFlush();
        SIZE_T after = GetCurrentWorkingSet();
        SIZE_T freed = (before > after) ? (before - after) : 0;

        char freedStr[32], beforeStr[32], afterStr[32];
        FormatBytes(freed, freedStr, sizeof(freedStr));
        FormatBytes(before, beforeStr, sizeof(beforeStr));
        FormatBytes(after, afterStr, sizeof(afterStr));

        snprintf(output, outputSize,
                 "%s:flush|before=%s|after=%s|freed=%s|count=%d",
                 ok ? "OK" : "FAIL", beforeStr, afterStr, freedStr, g_flushCount);
    }
    else if (_stricmp(function, "trim") == 0) {
        int compacted = DoTrim();
        snprintf(output, outputSize,
                 "OK:trim|heaps_compacted=%d|count=%d",
                 compacted, g_trimCount);
    }
    else if (_stricmp(function, "gc") == 0) {
        int compacted = DoTrim();
        bool ok = DoFlush();
        snprintf(output, outputSize,
                 "%s:gc|heaps=%d|flush_count=%d",
                 ok ? "OK" : "PARTIAL", compacted, g_flushCount);
    }
    else if (_stricmp(function, "shrink") == 0) {
        SIZE_T before = GetCurrentWorkingSet();
        bool ok = DoShrink();
        SIZE_T after = GetCurrentWorkingSet();
        char bStr[32], aStr[32];
        FormatBytes(before, bStr, sizeof(bStr));
        FormatBytes(after, aStr, sizeof(aStr));
        snprintf(output, outputSize,
                 "%s:shrink|before=%s|after=%s",
                 ok ? "OK" : "FAIL", bStr, aStr);
    }
    else if (_stricmp(function, "fullflush") == 0) {
        DoFullFlush(output, outputSize);
    }
    else if (_stricmp(function, "standby") == 0) {
        bool ok = DoStandbyPurge();
        snprintf(output, outputSize,
                 "%s:standby_purge",
                 ok ? "OK" : "FAIL:need_admin");
    }
    else if (_stricmp(function, "stats") == 0) {
        SIZE_T ws = GetCurrentWorkingSet();
        SIZE_T pb = GetCurrentPrivateBytes();
        DWORD  pf = GetPageFaultCount();
        DWORD  uptime = (GetTickCount() - g_initTime) / 1000;

        char wsStr[32], pbStr[32], freedStr[32];
        FormatBytes(ws, wsStr, sizeof(wsStr));
        FormatBytes(pb, pbStr, sizeof(pbStr));
        FormatBytes(g_totalFreed, freedStr, sizeof(freedStr));

        snprintf(output, outputSize,
                 "OK:stats|ws=%s|private=%s|pagefaults=%lu|"
                 "flushes=%d|trims=%d|fullflushes=%d|"
                 "total_freed=%s|uptime=%lus",
                 wsStr, pbStr, pf,
                 g_flushCount, g_trimCount, g_fullFlushCount,
                 freedStr, uptime);
    }
    else if (_stricmp(function, "pagefaults") == 0) {
        DWORD pf = GetPageFaultCount();
        snprintf(output, outputSize, "%lu", pf);
    }
    else if (_stricmp(function, "version") == 0) {
        snprintf(output, outputSize, "%s v%s", DLL_NAME, DLL_VERSION);
    }
    else if (_stricmp(function, "help") == 0) {
        snprintf(output, outputSize,
                 "Commands: flush,trim,gc,shrink,fullflush,"
                 "standby,stats,pagefaults,version,help");
    }
    else {
        snprintf(output, outputSize, "ERR:unknown_command:%s", function);
    }
}

__declspec(dllexport) void __stdcall RVExtensionVersion(
    char* output,
    int   outputSize)
{
    if (output && outputSize > 0)
        snprintf(output, outputSize, "%s v%s", DLL_NAME, DLL_VERSION);
}

__declspec(dllexport) int __stdcall RVExtensionArgs(
    char*        output,
    int          outputSize,
    const char*  function,
    const char** argv,
    int          argc)
{
    RVExtension(output, outputSize, function);
    return 0;
}

}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_initTime = GetTickCount();
            g_lastWorkingSet = GetCurrentWorkingSet();
            EnablePrivilege(L"SeDebugPrivilege");
            EnablePrivilege(L"SeProfileSingleProcessPrivilege");
            EnablePrivilege(L"SeIncreaseQuotaPrivilege");
            DisableThreadLibraryCalls(hModule);
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
