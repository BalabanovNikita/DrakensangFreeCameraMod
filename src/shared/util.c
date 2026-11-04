#include "../common.h"

WCHAR g_gameDir[MAX_PATH];
WCHAR g_modDir[MAX_PATH];

double now_seconds(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER t;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)freq.QuadPart;
}

void patch_ptr(void **where, void *value)
{
    DWORD old;
    VirtualProtect(where, sizeof(void *), PAGE_EXECUTE_READWRITE, &old);
    *where = value;
    VirtualProtect(where, sizeof(void *), old, &old);
}
