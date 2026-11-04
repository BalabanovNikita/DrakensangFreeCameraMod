/* dinput8.dll proxy: entry point, real dinput8 forwarding, IAT hooks. */
#include "loader.h"

int _fltused = 0;

static HMODULE g_realDinput;
typedef HRESULT (WINAPI *PFN_DirectInput8Create)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
static PFN_DirectInput8Create real_DirectInput8Create;
static int g_isGame;

/* Locate the IAT slot of an imported function in `mod`. */
void **iat_slot(HMODULE mod, const char *dll, const char *func)
{
    BYTE *base = (BYTE *)mod;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *d;
    if (!dir.VirtualAddress) return NULL;
    for (d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; d++) {
        IMAGE_THUNK_DATA *oft, *ft;
        if (_stricmp((char *)base + d->Name, dll) != 0) continue;
        if (!d->OriginalFirstThunk) continue;
        oft = (IMAGE_THUNK_DATA *)(base + d->OriginalFirstThunk);
        ft = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
        for (; oft->u1.AddressOfData; oft++, ft++) {
            IMAGE_IMPORT_BY_NAME *ibn;
            if (IMAGE_SNAP_BY_ORDINAL(oft->u1.Ordinal)) continue;
            ibn = (IMAGE_IMPORT_BY_NAME *)(base + oft->u1.AddressOfData);
            if (strcmp((char *)ibn->Name, func) == 0) return (void **)&ft->u1.Function;
        }
    }
    return NULL;
}

/* Replace an imported function pointer in `mod`'s IAT. Returns the original or NULL. */
void *iat_hook(HMODULE mod, const char *dll, const char *func, void *repl)
{
    void **slot = iat_slot(mod, dll, func);
    void *orig;
    if (!slot) return NULL;
    orig = *slot;
    patch_ptr(slot, repl);
    return orig;
}

/* ---- GetCommandLineA: append extra engine arguments from the ini ------- */
static LPSTR (WINAPI *real_GetCommandLineA)(void);
static char g_cmdline[4096];

static LPSTR WINAPI hk_GetCommandLineA(void)
{
    if (!g_cmdline[0]) {
        snprintf(g_cmdline, sizeof g_cmdline, "%s %s", real_GetCommandLineA(), g_cfg.extra_args);
        mlog("command line: %s", g_cmdline);
    }
    return g_cmdline;
}

static void install_import_hooks(void)
{
    HMODULE exe = GetModuleHandleW(NULL);
    void **slot = iat_slot(exe, "d3d9.dll", "Direct3DCreate9");

    if (slot) {
        void *orig = *slot;
        patch_ptr(slot, d3d_hook_create(orig));
        mlog("IAT: Direct3DCreate9 hooked (orig %p)", orig);
    } else {
        mlog("IAT: Direct3DCreate9 NOT found");
    }
    input_install_message_filter(exe);

    if (g_cfg.extra_args[0]) {
        real_GetCommandLineA = iat_hook(exe, "kernel32.dll", "GetCommandLineA", (void *)hk_GetCommandLineA);
        mlog("IAT: GetCommandLineA %s (extra args: '%s')",
             real_GetCommandLineA ? "hooked" : "NOT found", g_cfg.extra_args);
    }
}

/* ---- crash reporting: log fatal exceptions with module + offset and code addresses on the stack */
static int stack_readable(const void *p, DWORD size)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof mbi)) return 0;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    return (const BYTE *)mbi.BaseAddress + mbi.RegionSize >= (const BYTE *)p + size;
}

static void describe_addr(DWORD a, char *out, int outsz)
{
    HMODULE hm;
    char path[MAX_PATH], *name;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)a, &hm) && GetModuleFileNameA(hm, path, MAX_PATH)) {
        name = strrchr(path, '\\');
        snprintf(out, outsz, "%08lx (%s+%lx)", a, name ? name + 1 : path, a - (DWORD)hm);
    } else {
        snprintf(out, outsz, "%08lx", a);
    }
}

static LONG CALLBACK crash_handler(EXCEPTION_POINTERS *ep)
{
    static int count;
    DWORD code = ep->ExceptionRecord->ExceptionCode, *sp;
    CONTEXT *c = ep->ContextRecord;
    char where[300];
    int i, shown = 0;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_PRIV_INSTRUCTION)
        return EXCEPTION_CONTINUE_SEARCH;
    if (count++ >= 8) return EXCEPTION_CONTINUE_SEARCH;
    describe_addr((DWORD)ep->ExceptionRecord->ExceptionAddress, where, sizeof where);
    mlog("EXCEPTION %08lx at %s, info %lu %08lx", code, where,
         (unsigned long)ep->ExceptionRecord->ExceptionInformation[0],
         (unsigned long)ep->ExceptionRecord->ExceptionInformation[1]);
    mlog("  eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx", c->Eax, c->Ebx,
         c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    sp = (DWORD *)c->Esp;
    for (i = 0; i < 256 && shown < 16; i++) {
        DWORD v;
        if (((DWORD)(sp + i) & 0xFFF) == 0 && !stack_readable(sp + i, 4)) break;
        if (i == 0 && !stack_readable(sp, 4)) break;
        v = sp[i];
        if ((v >= 0x401000 && v < 0xA88000) || (v >= 0x10000000 && v < 0x7FFF0000)) {
            describe_addr(v, where, sizeof where);
            if (strchr(where, '(')) {
                mlog("  stack[%d] %s", i, where);
                shown++;
            }
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* ---- hang watchdog: if no frame is presented for a while, log where the main thread is */
static DWORD g_mainTid;

static void report_main_thread(void)
{
    HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, g_mainTid);
    CONTEXT c;
    char where[300];
    static DWORD stack[256];
    int i, n = 0, shown = 0;
    if (!t) return;
    memset(&c, 0, sizeof c);
    c.ContextFlags = CONTEXT_FULL;
    SuspendThread(t);
    if (GetThreadContext(t, &c)) {
        for (n = 0; n < 256 && stack_readable((DWORD *)c.Esp + n, 4); n++) stack[n] = ((DWORD *)c.Esp)[n];
    }
    ResumeThread(t);
    CloseHandle(t);
    describe_addr(c.Eip, where, sizeof where);
    mlog("WATCHDOG: no frame for 8s; main thread at %s esp=%08lx iconic=%d foreground=%d", where, c.Esp,
         g_hwnd ? IsIconic(g_hwnd) : -1, GetForegroundWindow() == g_hwnd);
    for (i = 0; i < n && shown < 20; i++) {
        DWORD v = stack[i];
        if ((v >= 0x401000 && v < 0xA88000) || (v >= 0x10000000 && v < 0x7FFF0000)) {
            describe_addr(v, where, sizeof where);
            if (strchr(where, '(')) {
                mlog("  stack[%d] %s", i, where);
                shown++;
            }
        }
    }
}

static DWORD WINAPI watchdog_thread(void *param)
{
    LONG last = -1;
    int stuck = 0;
    (void)param;
    for (;;) {
        LONG cur;
        Sleep(1000);
        cur = g_presentCount;
        if (cur == 0) continue;   /* rendering not started yet */
        if (cur != last) {
            if (stuck >= 8) mlog("WATCHDOG: frames resumed after %ds", stuck);
            stuck = 0;
        } else if (++stuck == 8) {
            report_main_thread();
        }
        last = cur;
    }
}

static int load_real_dinput(void)
{
    WCHAR path[MAX_PATH];
    if (real_DirectInput8Create) return 1;
    GetSystemDirectoryW(path, MAX_PATH);
    wcscat(path, L"\\dinput8.dll");
    g_realDinput = LoadLibraryW(path);
    if (g_realDinput)
        real_DirectInput8Create = (PFN_DirectInput8Create)GetProcAddress(g_realDinput, "DirectInput8Create");
    mlog("real dinput8: %p, DirectInput8Create %p", g_realDinput, real_DirectInput8Create);
    return real_DirectInput8Create != NULL;
}

HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE inst, DWORD ver, REFIID riid, LPVOID *out, LPUNKNOWN outer)
{
    HRESULT hr;
    if (!load_real_dinput()) return E_FAIL;
    if (g_isGame) core_ensure_loaded();   /* game code is decrypted by now */
    hr = real_DirectInput8Create(inst, ver, riid, out, outer);
    mlog("DirectInput8Create(ver %x) -> %08lx", ver, hr);
    if (g_isGame && SUCCEEDED(hr) && out && *out) input_wrap_dinput(*out);
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        WCHAR exe[MAX_PATH], *slash;
        DisableThreadLibraryCalls(inst);
        now_seconds();

        GetModuleFileNameW(NULL, exe, MAX_PATH);
        slash = wcsrchr(exe, L'\\');
        g_isGame = slash && (_wcsicmp(slash + 1, L"drakensang.exe") == 0 ||     /* Drakensang, The River of Time */
                             _wcsicmp(slash + 1, L"drakensanga1.exe") == 0);     /* River of Time add-on */
        if (slash) slash[1] = 0;
        wcscpy(g_gameDir, exe);
        wcscpy(g_modDir, g_gameDir);
        wcscat(g_modDir, L"_mod\\");

        config_load();
        if (!g_isGame) return TRUE;   /* plain proxy for any other host */
        {
            WCHAR cur[MAX_PATH], prev[MAX_PATH];
            wcscpy(cur, g_modDir);
            wcscat(cur, L"mod.log");
            wcscpy(prev, g_modDir);
            wcscat(prev, L"mod.prev.log");
            MoveFileExW(cur, prev, MOVEFILE_REPLACE_EXISTING);
        }
        log_open(1);
        mlog("Drakensang mod loader " MOD_VERSION " loaded");
        AddVectoredExceptionHandler(0, crash_handler);
        g_mainTid = GetCurrentThreadId();
        CloseHandle(CreateThread(NULL, 0, watchdog_thread, NULL, 0, NULL));
        install_import_hooks();
    }
    return TRUE;
}
