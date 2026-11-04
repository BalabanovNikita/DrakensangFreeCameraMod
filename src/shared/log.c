/* Plain-text log at <game>\_mod\mod.log, shared by loader and core (append-only writes). */
#include "../common.h"

static HANDLE g_log = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_logLock;
static int g_lockInit;

void log_open(int truncate)
{
    WCHAR path[MAX_PATH];
    if (!g_lockInit) {
        InitializeCriticalSection(&g_logLock);
        g_lockInit = 1;
    }
    if (!g_cfg.dev_log || g_log != INVALID_HANDLE_VALUE) return;
    CreateDirectoryW(g_modDir, NULL);
    wcscpy(path, g_modDir);
    wcscat(path, L"mod.log");
    g_log = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        truncate ? CREATE_ALWAYS : OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

void log_close(void)
{
    if (g_log != INVALID_HANDLE_VALUE) CloseHandle(g_log);
    g_log = INVALID_HANDLE_VALUE;
}

void mlog(const char *fmt, ...)
{
    static char buf[2048];
    static double t0;
    va_list ap;
    int n;
    DWORD written;
    if (g_log == INVALID_HANDLE_VALUE) return;
    EnterCriticalSection(&g_logLock);
    if (!t0) t0 = now_seconds();
    n = snprintf(buf, sizeof buf, "[%9.3f] ", now_seconds() - t0);
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof buf - n - 2, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof buf - 2) n = sizeof buf - 2;
    buf[n++] = '\n';
    WriteFile(g_log, buf, n, &written, NULL);
    LeaveCriticalSection(&g_logLock);
}
