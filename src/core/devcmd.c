/* Development channel: commands written to _mod\cmd.txt are executed on the render thread,
 * results are appended to _mod\cmd_out.txt. Enabled with [dev] command_file=1.
 * "after <ticks> <command>" runs a command that many frames later (for timed comparisons). */
#include "core.h"

#define MAX_LATER 32
static struct { unsigned at; char line[200]; } g_later[MAX_LATER];
static int g_nlater;
static unsigned g_tick;

static void run_line(HANDLE f, const char *line)
{
    static char result[8192], msg[8400];
    DWORD written;
    int len;
    unsigned n;
    char rest[200];
    if (sscanf(line, "after %u %199[^\r\n]", &n, rest) == 2) {
        if (g_nlater < MAX_LATER) {
            g_later[g_nlater].at = g_tick + n;
            strcpy(g_later[g_nlater++].line, rest);
        }
        return;
    }
    mod_command(line, result, sizeof result);
    mlog("cmd '%s' -> %s", line, result);
    len = snprintf(msg, sizeof msg, "> %s\n%s\n", line, result);
    if (f != INVALID_HANDLE_VALUE) WriteFile(f, msg, len, &written, NULL);
}

static HANDLE open_out(void)
{
    WCHAR outPath[MAX_PATH];
    wcscpy(outPath, g_modDir);
    wcscat(outPath, L"cmd_out.txt");
    return CreateFileW(outPath, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

static void run_later(void)
{
    int i = 0;
    HANDLE f;
    if (!g_nlater) return;
    f = INVALID_HANDLE_VALUE;
    while (i < g_nlater) {
        if ((int)(g_tick - g_later[i].at) >= 0) {
            char line[200];
            strcpy(line, g_later[i].line);
            g_later[i] = g_later[--g_nlater];
            if (f == INVALID_HANDLE_VALUE) f = open_out();
            run_line(f, line);
        } else {
            i++;
        }
    }
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
}

void devcmd_poll(void)
{
    static double next;
    static char buf[8192];
    WCHAR inPath[MAX_PATH];
    HANDLE f;
    DWORD n = 0;
    char *line, *save = NULL;
    double now = now_seconds();

    g_tick++;
    run_later();
    if (now < next) return;
    next = now + 0.25;

    wcscpy(inPath, g_modDir);
    wcscat(inPath, L"cmd.txt");
    f = CreateFileW(inPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    ReadFile(f, buf, sizeof buf - 1, &n, NULL);
    CloseHandle(f);
    DeleteFileW(inPath);
    buf[n] = 0;
    if (!n) return;

    f = open_out();
    for (line = strtok_s(buf, "\r\n", &save); line; line = strtok_s(NULL, "\r\n", &save)) {
        while (*line == ' ') line++;
        if (!*line || *line == '#') continue;
        run_line(f, line);
    }
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
}
