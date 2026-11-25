#include <windows.h>
#include <intrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <tlhelp32.h>
#include "ddraw.h"
#include "debug.h"
#include "dd.h"
#include "ddsurface.h"
#include "hook.h"
#include "mouse.h"
#include "utils.h"
#include "dllmain.h"
#include "config.h"
#include "versionhelpers.h"
#include "delay_imports.h"


/*
    The following code is licensed under the MIT license:
    DetourEnumerateModules
    Copyright (c) Microsoft Corporation
    https://github.com/microsoft/Detours
*/
#define MM_ALLOCATION_GRANULARITY 0x10000

HMODULE WINAPI util_enumerate_modules(_In_opt_ HMODULE hModuleLast)
{
    PBYTE pbLast = (PBYTE)hModuleLast + MM_ALLOCATION_GRANULARITY;

    MEMORY_BASIC_INFORMATION mbi;
    ZeroMemory(&mbi, sizeof(mbi));

    // Find the next memory region that contains a mapped PE image.
    //
    for (;; pbLast = (PBYTE)mbi.BaseAddress + mbi.RegionSize) {
        if (VirtualQuery(pbLast, &mbi, sizeof(mbi)) <= 0) {
            break;
        }

        // Skip uncommitted regions and guard pages.
        //
        if ((mbi.State != MEM_COMMIT) ||
            ((mbi.Protect & 0xff) == PAGE_NOACCESS) ||
            (mbi.Protect & PAGE_GUARD)) {
            continue;
        }

        __try {
            PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)pbLast;
            if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE ||
                (DWORD)pDosHeader->e_lfanew > mbi.RegionSize ||
                (DWORD)pDosHeader->e_lfanew < sizeof(*pDosHeader)) {
                continue;
            }

            PIMAGE_NT_HEADERS pNtHeader = (PIMAGE_NT_HEADERS)((PBYTE)pDosHeader +
                pDosHeader->e_lfanew);
            if (pNtHeader->Signature != IMAGE_NT_SIGNATURE) {
                continue;
            }

            return (HMODULE)pDosHeader;
        }
#if defined(_MSC_VER)
#pragma prefast(suppress:28940, "A bad pointer means this probably isn't a PE header.")
#endif
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ?
            EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            continue;
        }
    }
    return NULL;
}

void util_set_process_affinity()
{ 
#if (_WIN32_WINNT >= _WIN32_WINNT_WINXP)
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return;

    THREADENTRY32 entry = { 0 };
    entry.dwSize = sizeof(THREADENTRY32);

    if (!Thread32First(snap, &entry))
    {
        CloseHandle(snap);
        return;
    }

    do
    {
        if (entry.th32OwnerProcessID == GetCurrentProcessId())
        {
            util_set_thread_affinity(entry.th32ThreadID);
        }
    } while (Thread32Next(snap, &entry));

    CloseHandle(snap);
#endif
}

void util_set_thread_affinity(DWORD tid)
{
#if (_WIN32_WINNT >= _WIN32_WINNT_WINXP)
    HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION | THREAD_SET_INFORMATION, FALSE, tid);
    if (thread)
    {
        void* start = NULL;
        NTSTATUS status = STATUS_PENDING;
            
        if (delay_NtQueryInformationThread)
        {
            status = 
                delay_NtQueryInformationThread(thread, ThreadQuerySetWin32StartAddress, &start, sizeof(start), NULL);
        }

        if (status == STATUS_SUCCESS && start && delay_GetModuleHandleExA)
        {
            char game_exe_path[MAX_PATH] = { 0 };
            char game_dir[MAX_PATH] = { 0 };

            if (GetModuleFileNameA(NULL, game_exe_path, sizeof(game_exe_path)))
            {
                _splitpath(game_exe_path, NULL, game_dir, NULL, NULL);

                char mod_path[MAX_PATH] = { 0 };
                char mod_dir[MAX_PATH] = { 0 };
                char mod_filename[MAX_PATH] = { 0 };
                HMODULE mod = NULL;

                if (delay_GetModuleHandleExA(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, start, &mod))
                {
                    if (GetModuleFileNameA(mod, mod_path, sizeof(mod_path)))
                    {
                        _splitpath(mod_path, NULL, mod_dir, mod_filename, NULL);

                        if (_strnicmp(game_dir, mod_dir, strlen(game_dir)) == 0) // _strcmpi(mod_filename, "WINMM") == 0
                        {
                            SetThreadAffinityMask(thread, 1);
                        }
                    }
                }
            }
        }
        else
        {
            SetThreadAffinityMask(thread, 1);
        }

        CloseHandle(thread);
    }
#endif
}

void util_pull_messages()
{
    if (g_config.fix_not_responding &&
        g_ddraw.hwnd &&
        g_ddraw.last_msg_pull_tick + 1000 < timeGetTime() &&
        GetCurrentThreadId() == g_ddraw.gui_thread_id &&
        !IsWine())
    {
        /* workaround for "Not Responding" window problem */
        //g_ddraw.last_msg_pull_tick = timeGetTime();
        MSG msg;
        if (real_PeekMessageA(&msg, g_ddraw.hwnd, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
}

DWORD util_get_timestamp(HMODULE mod)
{
    if (!mod || mod == INVALID_HANDLE_VALUE)
        return 0;

    PIMAGE_DOS_HEADER dos_header = (PIMAGE_DOS_HEADER)mod;
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;

    PIMAGE_NT_HEADERS nt_headers = (PIMAGE_NT_HEADERS)((DWORD)dos_header + (DWORD)dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE)
        return 0;

    return nt_headers->FileHeader.TimeDateStamp;
}

FARPROC util_get_iat_proc(HMODULE mod, char* module_name, char* function_name)
{
    if (!mod || mod == INVALID_HANDLE_VALUE)
        return NULL;

    __try
    {
        PIMAGE_DOS_HEADER dos_header = (PIMAGE_DOS_HEADER)mod;
        if (dos_header->e_magic != IMAGE_DOS_SIGNATURE)
            return NULL;

        PIMAGE_NT_HEADERS nt_headers = (PIMAGE_NT_HEADERS)((DWORD)mod + (DWORD)dos_header->e_lfanew);
        if (nt_headers->Signature != IMAGE_NT_SIGNATURE)
            return NULL;

        DWORD import_desc_rva = nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
        if (!import_desc_rva)
            return NULL;

        PIMAGE_IMPORT_DESCRIPTOR import_desc = (PIMAGE_IMPORT_DESCRIPTOR)((DWORD)mod + import_desc_rva);

        while (import_desc->FirstThunk)
        {
            if (!import_desc->OriginalFirstThunk || !import_desc->Name)
            {
                import_desc++;
                continue;
            }

            char* imp_module_name = (char*)((DWORD)mod + import_desc->Name);

            if (_stricmp(imp_module_name, module_name) == 0)
            {
                PIMAGE_THUNK_DATA first_thunk = (void*)((DWORD)mod + import_desc->FirstThunk);
                PIMAGE_THUNK_DATA o_first_thunk = (void*)((DWORD)mod + import_desc->OriginalFirstThunk);

                while (first_thunk->u1.Function)
                {
                    if (!o_first_thunk->u1.AddressOfData)
                    {
                        first_thunk++;
                        o_first_thunk++;
                        continue;
                    }

                    PIMAGE_IMPORT_BY_NAME import = (void*)((DWORD)mod + o_first_thunk->u1.AddressOfData);

                    if ((o_first_thunk->u1.Ordinal & IMAGE_ORDINAL_FLAG) == 0)
                    {
                        if (strcmp((const char*)import->Name, function_name) == 0 && first_thunk->u1.Function)
                        {
                            return (FARPROC)first_thunk->u1.Function;
                        }
                    }

                    first_thunk++;
                    o_first_thunk++;
                }
            }

            import_desc++;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return NULL;
}

BOOL util_caller_is_ddraw_wrapper(void* return_address)
{
    if (!delay_GetModuleHandleExA)
        return FALSE;

    void* directDrawCreate = (void*)util_get_iat_proc(GetModuleHandleA(NULL), "ddraw.dll", "DirectDrawCreate");
    void* directDrawCreateEx = (void*)util_get_iat_proc(GetModuleHandleA(NULL), "ddraw.dll", "DirectDrawCreateEx");

    TRACE("%s directDrawCreate = %p, directDrawCreateEx = %p\n", __FUNCTION__, directDrawCreate, directDrawCreateEx);

    HMODULE mod = NULL;
    DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;

    HMODULE D3dHook_dll = GetModuleHandleA("D3dHook.dll");
    if (D3dHook_dll)
    {
        if ((delay_GetModuleHandleExA(flags, return_address, &mod) && mod == D3dHook_dll) ||
            (delay_GetModuleHandleExA(flags, directDrawCreate, &mod) && mod == D3dHook_dll) ||
            (delay_GetModuleHandleExA(flags, directDrawCreateEx, &mod) && mod == D3dHook_dll))
        {
            MessageBoxA(
                NULL,
                "Error: You cannot combine DirectCRT with other DirectDraw wrappers. \n\n"
                    "Please disable D3DWindower and then try to start the game again.",
                "Conflicting DirectDraw wrapper detected - DirectCRT",
                MB_OK | MB_TOPMOST);

            return TRUE;
        }
    }

    HMODULE wndmode_dll = GetModuleHandleA("wndmode.dll");
    if (wndmode_dll)
    {
        if ((delay_GetModuleHandleExA(flags, return_address, &mod) && mod == wndmode_dll) ||
            (delay_GetModuleHandleExA(flags, directDrawCreate, &mod) && mod == wndmode_dll) ||
            (delay_GetModuleHandleExA(flags, directDrawCreateEx, &mod) && mod == wndmode_dll))
        {
            MessageBoxA(
                NULL,
                "Error: You cannot combine DirectCRT with other DirectDraw wrappers. \n\n"
                    "Please remove/disable wndmode.dll and then try to start the game again.",
                "Conflicting DirectDraw wrapper detected - DirectCRT",
                MB_OK | MB_TOPMOST);

            return TRUE;
        }
    }

    HMODULE windmode_dll = GetModuleHandleA("windmode.dll");
    if (windmode_dll)
    {
        if ((delay_GetModuleHandleExA(flags, return_address, &mod) && mod == windmode_dll) ||
            (delay_GetModuleHandleExA(flags, directDrawCreate, &mod) && mod == windmode_dll) ||
            (delay_GetModuleHandleExA(flags, directDrawCreateEx, &mod) && mod == windmode_dll))
        {
            MessageBoxA(
                NULL,
                "Error: You cannot combine DirectCRT with other DirectDraw wrappers. \n\n"
                    "Please remove/disable windmode.dll and then try to start the game again.",
                "Conflicting DirectDraw wrapper detected - DirectCRT",
                MB_OK | MB_TOPMOST);

            return TRUE;
        }
    }

    HMODULE dxwnd_dll = GetModuleHandleA("dxwnd.dll");
    if (dxwnd_dll)
    {
        if ((delay_GetModuleHandleExA(flags, return_address, &mod) && mod == dxwnd_dll) ||
            (delay_GetModuleHandleExA(flags, directDrawCreate, &mod) && mod == dxwnd_dll) ||
            (delay_GetModuleHandleExA(flags, directDrawCreateEx, &mod) && mod == dxwnd_dll))
        {
            MessageBoxA(
                NULL,
                "Error: You cannot combine DirectCRT with other DirectDraw wrappers. \n\n"
                    "Please disable DxWnd and then try to start the game again.",
                "Conflicting DirectDraw wrapper detected - DirectCRT",
                MB_OK | MB_TOPMOST);

            return TRUE;
        }
    }

    HMODULE age_dll = GetModuleHandleA("age.dll");
    if (age_dll)
    {
        if ((delay_GetModuleHandleExA(flags, return_address, &mod) && mod == age_dll) ||
            (delay_GetModuleHandleExA(flags, directDrawCreate, &mod) && mod == age_dll) ||
            (delay_GetModuleHandleExA(flags, directDrawCreateEx, &mod) && mod == age_dll))
        {
            HKEY hkey;
            LONG status =
                RegOpenKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\Voobly\\Voobly\\game\\13", 0L, KEY_READ, &hkey);

            if (status == ERROR_SUCCESS)
            {
                char windowed[256] = { 0 };
                DWORD size = sizeof(windowed);
                RegQueryValueExA(hkey, "windowmode", NULL, NULL, (PVOID)&windowed, &size);
                RegCloseKey(hkey);

                if (tolower(windowed[0]) == 't')
                {
                    MessageBoxA(
                        NULL,
                        "Error: You cannot combine DirectCRT with other DirectDraw wrappers. \n\n"
                            "Please disable the other wrapper by clicking in the game room on the very top "
                            "on 'Game', now select 'DirectX' and disable 'Start in Window-Mode'.",
                        "Conflicting DirectDraw wrapper detected - DirectCRT",
                        MB_OK | MB_TOPMOST);

                    return TRUE;
                }
            }
        }
    }

    return FALSE;
}

BOOL util_is_minimized(HWND hwnd)
{
    RECT rc = { 0 };

    return IsIconic(hwnd) || (real_GetClientRect(hwnd, &rc) && (rc.right - rc.left == 0 || rc.bottom - rc.top == 0));
}

BOOL util_in_foreground()
{
    DWORD process_id = 0;

    return GetWindowThreadProcessId(real_GetForegroundWindow(), &process_id) && process_id == GetCurrentProcessId();
}

BOOL util_is_avx_supported()
{
    const DWORD XMM_STATE_BIT = 1 << 1;
    const DWORD YMM_STATE_BIT = 1 << 2;
    const DWORD OS_AVX_BITS = XMM_STATE_BIT | YMM_STATE_BIT;

    const DWORD AVX_BIT = 1 << 28;
    const DWORD OSXSAVE_BIT = 1 << 27;
    const DWORD XSAVE_BIT = 1 << 26;
    const DWORD CPU_AVX_BITS = AVX_BIT | OSXSAVE_BIT | XSAVE_BIT;

    BOOL result = FALSE;

    __try
    {
        int info[4] = { 0 };
        __cpuid(info, 0);

        if (info[0] >= 1)
        {
            __cpuid(info, 1);

            if ((info[2] & CPU_AVX_BITS) == CPU_AVX_BITS)
            {
                unsigned int xcr0 = 0;

#ifdef _MSC_VER
                xcr0 = (unsigned int)_xgetbv(_XCR_XFEATURE_ENABLED_MASK);
#elif __AVX__
                __asm__("xgetbv" : "=a" (xcr0) : "c" (0) : "%edx");
#endif

                result = (xcr0 & OS_AVX_BITS) == OS_AVX_BITS;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return result;
}

void util_limit_game_ticks()
{
    if (GetCurrentThreadId() != g_ddraw.gui_thread_id)
        return;

    /*
    static void (WINAPI * getSystemTimePreciseAsFileTime)(LPFILETIME);

    if (!getSystemTimePreciseAsFileTime)
    {
        getSystemTimePreciseAsFileTime = GetProcAddress(LoadLibraryA("Kernel32.dll"), "GetSystemTimePreciseAsFileTime");

        //if (!getSystemTimePreciseAsFileTime)
        //    getSystemTimePreciseAsFileTime = GetSystemTimeAsFileTime;
    }


    if (1)
    {
        FILETIME ft = { 0 };
        getSystemTimePreciseAsFileTime(&ft);

        if (CompareFileTime((FILETIME*)&g_ddraw.ticks_limiter.due_time, &ft) == -1)
        {
            memcpy(&g_ddraw.ticks_limiter.due_time, &ft, sizeof(LARGE_INTEGER));
        }
        else
        {
            while (TRUE)
            {
                getSystemTimePreciseAsFileTime(&ft);
                if (CompareFileTime((FILETIME*)&g_ddraw.ticks_limiter.due_time, &ft) <= 0)
                    break;
            }
        }

        g_ddraw.ticks_limiter.due_time.QuadPart += g_ddraw.ticks_limiter.tick_length_ns;
    }
    else */
    if (g_ddraw.ticks_limiter.htimer)
    {
        FILETIME ft = { 0 };
        GetSystemTimeAsFileTime(&ft);

        if (CompareFileTime((FILETIME*)&g_ddraw.ticks_limiter.due_time, &ft) == -1)
        {
            memcpy(&g_ddraw.ticks_limiter.due_time, &ft, sizeof(LARGE_INTEGER));
        }
        else
        {
            WaitForSingleObject(g_ddraw.ticks_limiter.htimer, g_ddraw.ticks_limiter.tick_length * 2);
        }

        g_ddraw.ticks_limiter.due_time.QuadPart += g_ddraw.ticks_limiter.tick_length_ns;
        SetWaitableTimer(g_ddraw.ticks_limiter.htimer, &g_ddraw.ticks_limiter.due_time, 0, NULL, NULL, FALSE);
    }
    else
    {
        static DWORD next_game_tick;

        if (!next_game_tick)
        {
            next_game_tick = timeGetTime();
            return;
        }

        next_game_tick += g_ddraw.ticks_limiter.tick_length;
        DWORD tick_count = timeGetTime();

        int sleep_time = next_game_tick - tick_count;

        if (sleep_time <= 0 || sleep_time > g_ddraw.ticks_limiter.tick_length)
        {
            next_game_tick = tick_count;
        }
        else
        {
            Sleep(sleep_time);
        }
    }
}

void util_update_bnet_pos(int new_x, int new_y)
{
    static int old_x = -32000;
    static int old_y = -32000;

    if (old_x == -32000 || old_y == -32000 || !g_ddraw.bnet_active)
    {
        old_x = new_x;
        old_y = new_y;
        return;
    }

    POINT pt = { 0, 0 };
    real_ClientToScreen(g_ddraw.hwnd, &pt);

    RECT mainrc;
    SetRect(&mainrc, pt.x, pt.y, pt.x + g_ddraw.width, pt.y + g_ddraw.height);

    int adj_y = 0;
    int adj_x = 0;

    HWND hwnd = FindWindowEx(HWND_DESKTOP, NULL, "SDlgDialog", NULL);

    while (hwnd != NULL)
    {
        RECT rc;
        real_GetWindowRect(hwnd, &rc);

        OffsetRect(&rc, new_x - old_x, new_y - old_y);

        real_SetWindowPos(
            hwnd,
            0,
            rc.left,
            rc.top,
            0,
            0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

        if (rc.bottom - rc.top <= g_ddraw.height)
        {
            if (rc.bottom > mainrc.bottom && abs(mainrc.bottom - rc.bottom) > abs(adj_y))
            {
                adj_y = mainrc.bottom - rc.bottom;
            }
            else if (rc.top < mainrc.top && abs(mainrc.top - rc.top) > abs(adj_y))
            {
                adj_y = mainrc.top - rc.top;
            }
        }

        if (rc.right - rc.left <= g_ddraw.width)
        {
            if (rc.right > mainrc.right && abs(mainrc.right - rc.right) > abs(adj_x))
            {
                adj_x = mainrc.right - rc.right;
            }
            else if (rc.left < mainrc.left && abs(mainrc.left - rc.left) > abs(adj_x))
            {
                adj_x = mainrc.left - rc.left;
            }
        }

        hwnd = FindWindowEx(HWND_DESKTOP, hwnd, "SDlgDialog", NULL);
    }

    if (adj_x || adj_y)
    {
        HWND hwnd = FindWindowEx(HWND_DESKTOP, NULL, "SDlgDialog", NULL);

        while (hwnd != NULL)
        {
            RECT rc;
            real_GetWindowRect(hwnd, &rc);

            OffsetRect(&rc, adj_x, adj_y);

            real_SetWindowPos(
                hwnd,
                0,
                rc.left,
                rc.top,
                0,
                0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

            hwnd = FindWindowEx(HWND_DESKTOP, hwnd, "SDlgDialog", NULL);
        }
    }

    old_x = new_x;
    old_y = new_y;
}

BOOL util_get_lowest_resolution(
    float ratio,
    SIZE* out_res,
    DWORD min_width,
    DWORD min_height,
    DWORD max_width,
    DWORD max_height)
{
    BOOL result = FALSE;
    int org_ratio = (int)((ratio + 0.005f) * 100);
    SIZE lowest = { .cx = max_width + 1, .cy = max_height + 1 };
    DWORD i = 0;
    DEVMODE m;
    memset(&m, 0, sizeof(DEVMODE));
    m.dmSize = sizeof(DEVMODE);

    while (real_EnumDisplaySettingsA(NULL, i, &m))
    {
        if (m.dmPelsWidth >= min_width &&
            m.dmPelsHeight >= min_height &&
            m.dmPelsWidth <= max_width &&
            m.dmPelsHeight <= max_height &&
            m.dmPelsWidth <= lowest.cx &&
            m.dmPelsHeight <= lowest.cy)
        {
            int res_ratio = (int)((((float)m.dmPelsWidth / m.dmPelsHeight) + 0.005f) * 100);

            if (abs(res_ratio - org_ratio) <= 5)
            {
                result = TRUE;
                out_res->cx = lowest.cx = m.dmPelsWidth;
                out_res->cy = lowest.cy = m.dmPelsHeight;
            }
        }

        memset(&m, 0, sizeof(DEVMODE));
        m.dmSize = sizeof(DEVMODE);
        i++;
    }

    return result;
}

/* Maximize fills the work area, where the picture is placed as in fullscreen;
 * the second time the window goes back to window_size */
void util_toggle_maximize()
{
    if (!g_config.window_resizable || !g_config.windowed || g_config.fullscreen || !g_ddraw.width)
        return;

    /* Do not allow DirectCRT's maximize while macOS maximize is active */
    if (IsMacOS() && !g_config.window_rect.left && !g_config.window_rect.top)
        return;

    RECT client_rc;
    RECT work_rc;

    LONG style = real_GetWindowLongA(g_ddraw.hwnd, GWL_STYLE);
    LONG exstyle = real_GetWindowLongA(g_ddraw.hwnd, GWL_EXSTYLE);
    BOOL got_menu = GetMenu(g_ddraw.hwnd) != NULL;

    if (!real_GetClientRect(g_ddraw.hwnd, &client_rc) || !SystemParametersInfo(SPI_GETWORKAREA, 0, &work_rc, 0))
        return;

    /* The client area of a window that fills the work area */
    RECT max_rc = work_rc;
    util_unadjust_window_rect(&max_rc, style, got_menu, exstyle);

    int width = max_rc.right - max_rc.left;
    int height = max_rc.bottom - max_rc.top;

    if (client_rc.right == width && client_rc.bottom == height)
    {
        g_config.window_rect.left = -32000;
        g_config.window_rect.top = -32000;
        g_config.window_rect.right = 0;
        g_config.window_rect.bottom = 0;

        util_set_window_rect(0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }
    else
    {
        util_set_window_rect(max_rc.left, max_rc.top, width, height, 0);
    }
}

/* Alt+Enter: from a window to the fullscreen that 'display' gives, and back */
void util_toggle_fullscreen()
{
    /* Disable ALT+ENTER on battle.net */
    if (g_ddraw.bnet_active || !g_ddraw.width)
        return;

    /* Do not allow ALT+ENTER while macOS maximize is active */
    if (IsMacOS() && !g_config.window_rect.left && !g_config.window_rect.top)
        return;

    mouse_unlock();

    if (g_config.windowed && !g_config.fullscreen)
    {
        DISPLAY_KIND kind = ds_display_fullscreen_kind(g_config.display);

        g_config.windowed = FALSE;
        g_config.fullscreen = kind == DISPLAY_KIND_DESKTOP;

        dd_SetDisplayMode(0, 0, 0, SDM_LEAVE_WINDOWED);
        util_update_bnet_pos(0, 0);

        mouse_lock();
    }
    else
    {
        /* Borderless (the fallback when a mode switch failed) has no mode to
           restore */
        BOOL was_borderless = g_config.windowed;

        g_config.windowed = TRUE;
        g_config.fullscreen = FALSE;

        if (!was_borderless)
        {
            if (g_ddraw.render.thread)
            {
                EnterCriticalSection(&g_ddraw.cs);
                g_ddraw.render.run = FALSE;
                ReleaseSemaphore(g_ddraw.render.sem, 1, NULL);
                LeaveCriticalSection(&g_ddraw.cs);

                WaitForSingleObject(g_ddraw.render.thread, INFINITE);
                g_ddraw.render.thread = NULL;
            }

            ChangeDisplaySettings(NULL, g_ddraw.bnet_active ? CDS_FULLSCREEN : 0);
        }

        dd_SetDisplayMode(0, 0, 0, SDM_LEAVE_FULLSCREEN);
    }
}

BOOL util_unadjust_window_rect(LPRECT prc, DWORD dwStyle, BOOL fMenu, DWORD dwExStyle)
{
    RECT rc;
    SetRectEmpty(&rc);

    BOOL fRc = AdjustWindowRectEx(&rc, dwStyle, fMenu, dwExStyle);

    if (fRc)
    {
        prc->left -= rc.left;
        prc->top -= rc.top;
        prc->right -= rc.right;
        prc->bottom -= rc.bottom;
    }

    return fRc;
}

void util_set_window_rect(int x, int y, int width, int height, UINT flags)
{
    if (g_config.windowed)
    {
        if (g_ddraw.render.thread)
        {
            EnterCriticalSection(&g_ddraw.cs);
            g_ddraw.render.run = FALSE;
            ReleaseSemaphore(g_ddraw.render.sem, 1, NULL);
            LeaveCriticalSection(&g_ddraw.cs);

            WaitForSingleObject(g_ddraw.render.thread, INFINITE);
            g_ddraw.render.thread = NULL;
        }

        if ((flags & SWP_NOMOVE) == 0)
        {
            g_config.window_rect.left = x;
            g_config.window_rect.top = y;
        }

        if ((flags & SWP_NOSIZE) == 0)
        {
            g_config.window_rect.bottom = height;
            g_config.window_rect.right = width;
        }

        dd_SetDisplayMode(0, 0, 0, 0);
    }
}

BOOL CALLBACK util_enum_thread_wnd_proc(HWND hwnd, LPARAM lParam)
{
    RECT size = { 0 };
    real_GetClientRect(hwnd, &size);

    LONG sytle = real_GetWindowLongA(hwnd, GWL_STYLE);

    if (!g_ddraw.hwnd && !(sytle & WS_DISABLED) && size.right > 0 && size.bottom > 0)
        g_ddraw.hwnd = hwnd;

#ifdef _DEBUG
    char class[MAX_PATH] = { 0 };
    GetClassNameA(hwnd, class, sizeof(class) - 1);

    char title[MAX_PATH] = { 0 };
    GetWindowTextA(hwnd, title, sizeof(title) - 1);

    RECT pos = { 0 };
    real_GetWindowRect(hwnd, &pos);

    LONG exsytle = real_GetWindowLongA(hwnd, GWL_EXSTYLE);

    TRACE(
        "%s(class=%s, title=%s, X=%d, Y=%d, nWidth=%d, nHeight=%d)\n",
        __FUNCTION__, class, title, pos.left, pos.top, size.right, size.bottom);

    dbg_dump_wnd_styles(sytle, exsytle);
#endif

    return TRUE;
}

BOOL CALLBACK util_enum_child_proc(HWND hwnd, LPARAM lparam)
{
    IDirectDrawSurfaceImpl* this = (IDirectDrawSurfaceImpl*)lparam;

    RECT size;
    RECT pos;

    if (real_GetClientRect(hwnd, &size) && real_GetWindowRect(hwnd, &pos))
    {
        char class_name[MAX_PATH] = { 0 };
        GetClassNameA(hwnd, class_name, sizeof(class_name) - 1);

        LONG exstyle = real_GetWindowLongA(hwnd, GWL_EXSTYLE);
        HWND parent = GetParent(hwnd);

#ifdef _DEBUG_X
        LONG style = real_GetWindowLongA(hwnd, GWL_STYLE);

        TRACE("util_enum_child_proc class=%s, hwnd=%p, width=%u, height=%u, left=%d, top=%d, parent=%p, style=%08X\n", 
            class_name, hwnd, size.right, size.bottom, pos.left, pos.top, parent, style);

        dbg_dump_wnd_styles(style, exstyle);
#endif

        if (parent != g_ddraw.hwnd || size.right <= 1 || size.bottom <= 1 || _strcmpi(class_name, "Edit") == 0)
            return TRUE;

        if (g_config.fixchilds == FIX_CHILDS_DETECT_HIDE ||
            g_config.fixchilds == FIX_CHILDS_DETECT_HIDE_NOSCALE ||
            strcmp(class_name, "Cc2EditClassTh") == 0 ||
            strcmp(class_name, "msctls_statusbar32") == 0 ||
            strcmp(class_name, "VideoRenderer") == 0 ||
            strcmp(class_name, "MCIQTZ_Window") == 0 ||
            strcmp(class_name, "MCIAVI") == 0 ||
            strcmp(class_name, "AVIWnd32") == 0 ||
            strcmp(class_name, "MCIWndClass") == 0 ||
            strcmp(class_name, "AVI Window") == 0)
        {
            if (g_config.fixchilds == FIX_CHILDS_DETECT_HIDE_NOSCALE)
            {
                g_ddraw.got_child_windows = g_ddraw.child_window_exists = TRUE;
            }

            if (!(exstyle & WS_EX_TRANSPARENT))
            {
                real_SetWindowLongA(hwnd, GWL_EXSTYLE, exstyle | WS_EX_TRANSPARENT);

                real_SetWindowPos(
                    hwnd,
                    0,
                    0,
                    0,
                    0,
                    0,
                    SWP_ASYNCWINDOWPOS | SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER
                );
            }
        }
        else if (!(exstyle & WS_EX_TRANSPARENT))
        {
            g_ddraw.got_child_windows = g_ddraw.child_window_exists = TRUE;

            if (g_config.fixchilds == FIX_CHILDS_DETECT_PAINT)
            {
                HDC dst_dc = GetDC(hwnd);
                HDC src_dc;

                dds_GetDC(this, &src_dc);

                real_MapWindowPoints(HWND_DESKTOP, g_ddraw.hwnd, (LPPOINT)&pos, 2);

                real_BitBlt(dst_dc, 0, 0, size.right, size.bottom, src_dc, pos.left, pos.top, SRCCOPY);

                ReleaseDC(hwnd, dst_dc);
            }
        }

        if (strcmp(class_name, "TMediaPlayer") == 0)
            return TRUE;
    }

#ifdef _DEBUG_X
    return TRUE;
#else
    return FALSE;
#endif
}

char* util_load_text_resource(const char* name)
{
    HRSRC res = FindResourceA(g_ddraw_module, name, MAKEINTRESOURCEA(10) /* RT_RCDATA */);
    HGLOBAL mem = res ? LoadResource(g_ddraw_module, res) : NULL;
    DWORD size = res ? SizeofResource(g_ddraw_module, res) : 0;
    const void* data = mem ? LockResource(mem) : NULL;

    if (!data || !size)
        return NULL;

    char* text = malloc(size + 1);

    if (text)
    {
        memcpy(text, data, size);
        text[size] = '\0';
    }

    return text;
}
