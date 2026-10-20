#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <stdio.h>
#include <detours.h>

static decltype(&CreateProcessW) RealCreateProcessW = CreateProcessW;
static decltype(&CreateProcessA) RealCreateProcessA = CreateProcessA;
static decltype(&CreateWindowExW) RealCreateWindowExW = CreateWindowExW;
static decltype(&ShowWindow) RealShowWindow = ShowWindow;
static decltype(&ShowWindowAsync) RealShowWindowAsync = ShowWindowAsync;
static decltype(&SetWindowPos) RealSetWindowPos = SetWindowPos;
static decltype(&Shell_NotifyIconW) RealNotifyIcon = Shell_NotifyIconW;
static decltype(&ShellExecuteExW) RealShellExecuteExW = ShellExecuteExW;
static decltype(&ShellExecuteW) RealShellExecuteW = ShellExecuteW;
static char dllPath[MAX_PATH];
static wchar_t logPath[MAX_PATH];
// Where the tray logo comes from: LINE.exe in production, a test icon in TrayIconTest.
static wchar_t logoSource[MAX_PATH];
static bool startup = true;
static bool launcher = false;
static HWND closing = nullptr;
static bool closePosted = false;
static const UINT_PTR CloseTimer = 0x4C54; // "LT"
static HICON lineIcon = nullptr;
static bool iconLoaded = false;

static void Log(const char* event, HWND window = nullptr, LONG value = 0) {
    wchar_t cls[128] = {};
    if (window) GetClassNameW(window, cls, 128);
    char data[512];
    int count = sprintf_s(data, "%llu pid=%lu %s hwnd=%p class=%ls value=%ld\r\n",
        GetTickCount64(), GetCurrentProcessId(), event, window, cls, value);
    HANDLE file = CreateFileW(logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(file, data, count, &written, nullptr);
        CloseHandle(file);
    }
}

static bool IsQtTop(HWND window) {
    if (!window || (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD)) return false;
    wchar_t cls[128] = {};
    GetClassNameW(window, cls, 128);
    return wcsncmp(cls, L"Qt", 2) == 0 && wcsstr(cls, L"QWindow");
}

static bool IsMain(HWND window) {
    if (!IsQtTop(window) || (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) return false;
    wchar_t title[64] = {};
    GetWindowTextW(window, title, 64);
    return wcscmp(title, L"LINE") == 0;
}

// The logged-in main window is shown once, cloaked by DWM so nothing appears on screen,
// because LINE only starts counting unread messages and showing new-message notifications
// (and the "N" on its tray icon) after its main window has loaded. It is closed through
// LINE's usual close-to-tray handling as soon as LINE marks unread messages on its tray icon,
// or after five seconds when there are none.
static void Cloak(HWND window, BOOL on) {
    DwmSetWindowAttribute(window, DWMWA_CLOAK, &on, sizeof(on));
}

static void PostClose(HWND window, UINT reason) {
    if (closePosted) return;
    closePosted = true;
    KillTimer(window, CloseTimer);
    PostMessageW(window, WM_CLOSE, 0, 0);
    Log("startup-close-posted", window, reason);
}

static LRESULT CALLBACK MainProc(HWND window, UINT message, WPARAM wp, LPARAM lp,
    UINT_PTR, DWORD_PTR) {
    // The timer is ours and is not passed to LINE.
    if (message == WM_TIMER && wp == CloseTimer) { PostClose(window, message); return 0; }
    LRESULT result = DefSubclassProc(window, message, wp, lp);
    if (message == WM_CLOSE || message == WM_NCDESTROY) {
        startup = false;
        closing = nullptr;
        KillTimer(window, CloseTimer);
        RemoveWindowSubclass(window, MainProc, 1);
        // LINE hides the window when it closes to the tray; only then is it uncloaked.
        if (message == WM_CLOSE && IsWindowVisible(window)) RealShowWindow(window, SW_HIDE);
        Cloak(window, FALSE);
        Log("startup-close-complete", window, IsWindowVisible(window));
    }
    return result;
}

static bool Suppress(HWND window) {
    if (startup && launcher && window && !(GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD)) {
        wchar_t cls[128] = {};
        GetClassNameW(window, cls, 128);
        if (wcscmp(cls, L"SPLASH") == 0) {
            Log("launcher-splash-suppressed", window);
            return true;
        }
    }
    if (!startup || !IsQtTop(window)) return false;
    if (!IsMain(window)) {
        // The splash has no LINE title. Let initialization continue without showing it.
        Log("startup-splash-suppressed", window);
        return true;
    }
    if (!closing) {
        LONG style = static_cast<LONG>(GetWindowLongPtrW(window, GWL_STYLE));
        if (!(style & WS_MAXIMIZEBOX)) {
            // LINE's login screen shares the main window's class and title but cannot be
            // maximized. Show it so a logged-out start does not silently sit in the tray.
            Log("startup-login-shown", window, style);
            startup = false;
            return false;
        }
        if (!SetWindowSubclass(window, MainProc, 1, 0)) {
            Log("subclass-failed", window, GetLastError());
            startup = false;
            return false;
        }
        closing = window;
        Cloak(window, TRUE);
        SetTimer(window, CloseTimer, 5000, nullptr);
        Log("startup-main-cloaked", window, style);
    }
    // The cloaked main window is allowed to show, without taking the focus.
    return false;
}

static bool IsCloakedMain(HWND window) { return closing && window == closing; }

static BOOL WINAPI HookShowWindow(HWND window, int command) {
    if (command != SW_HIDE && startup) {
        Log("show-request", window, command);
        if (Suppress(window)) return IsWindowVisible(window);
        if (IsCloakedMain(window)) command = SW_SHOWNA;
    }
    return RealShowWindow(window, command);
}

static BOOL WINAPI HookShowWindowAsync(HWND window, int command) {
    if (command != SW_HIDE && Suppress(window)) return TRUE;
    if (command != SW_HIDE && IsCloakedMain(window)) command = SW_SHOWNA;
    return RealShowWindowAsync(window, command);
}

static BOOL WINAPI HookSetWindowPos(HWND window, HWND after, int x, int y, int cx, int cy, UINT flags) {
    if ((flags & SWP_SHOWWINDOW) && startup) {
        Log("position-show-request", window, flags);
        if (Suppress(window)) flags &= ~SWP_SHOWWINDOW;
        else if (IsCloakedMain(window)) flags |= SWP_NOACTIVATE;
    }
    return RealSetWindowPos(window, after, x, y, cx, cy, flags);
}

static HWND WINAPI HookCreateWindowExW(DWORD exStyle, LPCWSTR cls, LPCWSTR title, DWORD style,
    int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param) {
    bool candidate = startup && !(style & WS_CHILD) && HIWORD(cls) &&
        ((launcher && wcscmp(cls, L"SPLASH") == 0) ||
        (!launcher && wcsncmp(cls, L"Qt", 2) == 0 && wcsstr(cls, L"QWindow")));
    bool visible = candidate && (style & WS_VISIBLE);
    HWND window = RealCreateWindowExW(exStyle, cls, title, visible ? style & ~WS_VISIBLE : style,
        x, y, width, height, parent, menu, instance, param);
    if (startup && window && !(style & WS_CHILD)) Log("create-top-window", window, style);
    if (visible && !Suppress(window)) RealShowWindow(window, IsCloakedMain(window) ? SW_SHOWNA : SW_SHOWNORMAL);
    return window;
}

static LRESULT CALLBACK TrayProc(HWND window, UINT message, WPARAM wp, LPARAM lp,
    UINT_PTR, DWORD_PTR callbackMessage) {
    if (message == callbackMessage) {
        WORD event = LOWORD(lp);
        if (event == WM_LBUTTONUP || event == WM_LBUTTONDBLCLK || event == WM_RBUTTONUP ||
            event == NIN_SELECT || event == NIN_KEYSELECT || event == WM_CONTEXTMENU) {
            // Explicit tray interaction always restores normal user control, even if login failed.
            startup = false;
            if (closing) { closePosted = true; KillTimer(closing, CloseTimer); Cloak(closing, FALSE); }
            Log("tray-user-activation", window, event);
        }
    }
    return DefSubclassProc(window, message, wp, lp);
}

// Tray icon: the tool shows the logo embedded in LINE.exe instead of LINE's own tray artwork,
// but keeps the states LINE draws into its icon. LINE's icon is grey while connecting and
// gets an orange "N" badge when there are unread messages; both are read from its pixels.
struct Pixels { int width = 0, height = 0; BYTE* data = nullptr; };

static void FreePixels(Pixels& pixels) {
    if (pixels.data) HeapFree(GetProcessHeap(), 0, pixels.data);
    pixels = Pixels();
}

// Top-down BGRA with straight alpha, as stored in 32-bit icons.
static bool ReadIcon(HICON icon, Pixels& out) {
    ICONINFO info = {};
    if (!icon || !GetIconInfo(icon, &info)) return false;
    BITMAP bm = {};
    bool ok = false;
    if (info.hbmColor && GetObjectW(info.hbmColor, sizeof(bm), &bm) && bm.bmBitsPixel == 32 &&
        bm.bmWidth > 0 && bm.bmWidth <= 256 && bm.bmHeight > 0 && bm.bmHeight <= 256) {
        BITMAPINFOHEADER header = { sizeof(header), bm.bmWidth, -bm.bmHeight, 1, 32, BI_RGB };
        out.width = bm.bmWidth; out.height = bm.bmHeight;
        out.data = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), 0, bm.bmWidth * bm.bmHeight * 4));
        HDC dc = GetDC(nullptr);
        ok = out.data && GetDIBits(dc, info.hbmColor, 0, bm.bmHeight, out.data, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS);
        ReleaseDC(nullptr, dc);
        if (!ok) FreePixels(out);
    }
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    return ok;
}

static HICON MakeIcon(const Pixels& pixels) {
    BITMAPINFOHEADER header = { sizeof(header), pixels.width, -pixels.height, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color) return nullptr;
    memcpy(bits, pixels.data, pixels.width * pixels.height * 4);
    HBITMAP mask = CreateBitmap(pixels.width, pixels.height, 1, 1, nullptr);
    ICONINFO info = { TRUE, 0, 0, mask, color };
    HICON icon = mask ? CreateIconIndirect(&info) : nullptr;
    DeleteObject(color);
    if (mask) DeleteObject(mask);
    return icon;
}

// Investigation aid: with a tray-dump.on file next to the log, each icon LINE registers and
// each icon the tool registers instead is saved as a BMP.
static bool dumpTray = false;
static void Dump(const Pixels& pixels, const wchar_t* kind) {
    static int count = 0;
    if (!dumpTray || !pixels.data || count >= 200) return;
    wchar_t path[MAX_PATH];
    wcscpy_s(path, logPath);
    swprintf_s(wcsrchr(path, L'\\') + 1, 64, L"tray-%03d-%s.bmp", ++count, kind);
    DWORD bytes = pixels.width * pixels.height * 4;
    BITMAPINFOHEADER header = { sizeof(header), pixels.width, -pixels.height, 1, 32, BI_RGB };
    BITMAPFILEHEADER file = { 0x4D42, sizeof(file) + sizeof(header) + bytes, 0, 0, sizeof(file) + sizeof(header) };
    HANDLE out = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(out, &file, sizeof(file), &written, nullptr);
    WriteFile(out, &header, sizeof(header), &written, nullptr);
    WriteFile(out, pixels.data, bytes, &written, nullptr);
    CloseHandle(out);
}

static bool IsBadge(const BYTE* p) {
    int b = p[0], g = p[1], r = p[2];
    return p[3] > 40 && r > 190 && r - g > 60 && r - b > 90 && g > 30;
}

// Builds the logo icon at the size of LINE's icon with LINE's state applied.
// state: 1 = connecting (grey), 2 = unread badge.
static HICON ComposeTrayIcon(HICON original, LONG* state) {
    Pixels source, logo;
    HICON result = nullptr;
    *state = 0;
    if (!ReadIcon(original, source)) return nullptr;
    int w = source.width, h = source.height;
    HICON logoIcon = nullptr;
    UINT id = 0;
    if (PrivateExtractIconsW(logoSource, 0, w, h, &logoIcon, &id, 1, 0) == 1 && logoIcon) {
        if (ReadIcon(logoIcon, logo) && (logo.width != w || logo.height != h)) FreePixels(logo);
        DestroyIcon(logoIcon);
    }
    if (logo.data) {
        int opaque = 0, colourful = 0, badge = 0, left = w, top = h, right = -1, bottom = -1;
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            const BYTE* p = source.data + (y * w + x) * 4;
            if (p[3] < 40) continue;
            opaque++;
            int high = max(p[0], max(p[1], p[2])), low = min(p[0], min(p[1], p[2]));
            if (high - low > 40) colourful++;
            if (IsBadge(p)) { badge++; left = min(left, x); top = min(top, y); right = max(right, x); bottom = max(bottom, y); }
        }
        if (opaque > 0 && colourful * 20 < opaque) {
            *state |= 1;
            for (int i = 0; i < w * h; i++) {
                BYTE* p = logo.data + i * 4;
                int lum = (p[2] * 299 + p[1] * 587 + p[0] * 114) / 1000;
                // LINE's own grey icon is light (about #BEBEBE behind a white bubble).
                p[0] = p[1] = p[2] = static_cast<BYTE>(96 + lum * 5 / 8);
            }
        }
        if (badge >= 4) {
            *state |= 2;
            // Copy LINE's badge: its orange pixels and the white letter inside them.
            for (int y = max(0, top - 1); y <= min(h - 1, bottom + 1); y++)
                for (int x = max(0, left - 1); x <= min(w - 1, right + 1); x++) {
                    const BYTE* p = source.data + (y * w + x) * 4;
                    bool inside = x > left && x < right && y > top && y < bottom;
                    bool white = p[0] > 200 && p[1] > 200 && p[2] > 200;
                    bool green = p[1] > p[2] + 25 && p[1] > p[0] + 25;
                    if (p[3] > 40 && !green && (IsBadge(p) || (inside && white) || p[2] > p[1] + 30))
                        memcpy(logo.data + (y * w + x) * 4, p, 4);
                }
        }
        result = MakeIcon(logo);
        Dump(source, L"line");
        Dump(logo, L"tool");
    }
    FreePixels(source);
    FreePixels(logo);
    return result;
}

static BOOL WINAPI HookNotifyIcon(DWORD operation, PNOTIFYICONDATAW original) {
    if (original && operation == NIM_ADD && (original->uFlags & NIF_MESSAGE)) {
        SetWindowSubclass(original->hWnd, TrayProc, 2, original->uCallbackMessage);
    }
    if (original && (operation == NIM_ADD || operation == NIM_MODIFY) && (original->uFlags & NIF_ICON)) {
        static LONG lastState = -1;
        LONG state = 0;
        HICON composed = ComposeTrayIcon(original->hIcon, &state);
        if (!composed && !iconLoaded) {
            iconLoaded = true;
            ExtractIconExW(logoSource, 0, nullptr, &lineIcon, 1);
        }
        HICON icon = composed ? composed : lineIcon;
        if (icon) {
            NOTIFYICONDATAW changed = {};
            memcpy(&changed, original, min(original->cbSize, static_cast<DWORD>(sizeof(changed))));
            changed.hIcon = icon;
            if (state != lastState) { Log("tray-state", original->hWnd, composed ? state : -1); lastState = state; }
            // Unread messages are counted: the cloaked main window has done its job.
            if (closing && (state & 2)) PostClose(closing, 2);
            BOOL result = RealNotifyIcon(operation, &changed);
            // The shell keeps its own copy of the icon.
            if (composed) DestroyIcon(composed);
            return result;
        }
    }
    return RealNotifyIcon(operation, original);
}

static bool IsLineChild(LPCWSTR app, LPCWSTR command) {
    wchar_t path[MAX_PATH] = {};
    if (app) wcsncpy_s(path, app, _TRUNCATE);
    else if (command) {
        LPCWSTR begin = command;
        bool quoted = *begin == L'"';
        if (quoted) ++begin;
        LPCWSTR end = wcschr(begin, quoted ? L'"' : L' ');
        size_t length = end ? static_cast<size_t>(end - begin) : wcslen(begin);
        if (length >= MAX_PATH) return false;
        wcsncpy_s(path, begin, length);
    }
    LPCWSTR name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return _wcsicmp(name, L"LINE.exe") == 0 || _wcsicmp(name, L"LineLauncher.exe") == 0 ||
        _wcsicmp(name, L"LineUpdater.exe") == 0 || _wcsicmp(name, L"LineAppMgr.exe") == 0;
}

static bool IsLineChildA(LPCSTR app, LPCSTR command) {
    char path[MAX_PATH] = {};
    if (app) strncpy_s(path, app, _TRUNCATE);
    else if (command) {
        LPCSTR begin = command;
        bool quoted = *begin == '"';
        if (quoted) ++begin;
        LPCSTR end = strchr(begin, quoted ? '"' : ' ');
        size_t length = end ? static_cast<size_t>(end - begin) : strlen(begin);
        if (length >= MAX_PATH) return false;
        strncpy_s(path, begin, length);
    }
    wchar_t wide[MAX_PATH] = {};
    return MultiByteToWideChar(CP_ACP, 0, path, -1, wide, MAX_PATH) && IsLineChild(wide, nullptr);
}

static BOOL WINAPI HookCreateProcessA(LPCSTR app, LPSTR command, LPSECURITY_ATTRIBUTES processAttr,
    LPSECURITY_ATTRIBUTES threadAttr, BOOL inherit, DWORD flags, LPVOID environment,
    LPCSTR directory, LPSTARTUPINFOA startupInfo, LPPROCESS_INFORMATION information) {
    if (!IsLineChildA(app, command)) return RealCreateProcessA(app, command, processAttr, threadAttr,
        inherit, flags, environment, directory, startupInfo, information);
    PROCESS_INFORMATION local = {};
    BOOL result = DetourCreateProcessWithDllExA(app, command, processAttr, threadAttr, inherit,
        flags, environment, directory, startupInfo, information ? information : &local,
        dllPath, RealCreateProcessA);
    DWORD error = GetLastError();
    Log(result ? "ansi-child-injected" : "ansi-child-injection-failed", nullptr, result ? 0 : error);
    if (result && !information) { CloseHandle(local.hProcess); CloseHandle(local.hThread); }
    SetLastError(error);
    return result;
}

static BOOL WINAPI HookCreateProcessW(LPCWSTR app, LPWSTR command, LPSECURITY_ATTRIBUTES processAttr,
    LPSECURITY_ATTRIBUTES threadAttr, BOOL inherit, DWORD flags, LPVOID environment,
    LPCWSTR directory, LPSTARTUPINFOW startupInfo, LPPROCESS_INFORMATION information) {
    if (!IsLineChild(app, command)) return RealCreateProcessW(app, command, processAttr, threadAttr,
        inherit, flags, environment, directory, startupInfo, information);
    PROCESS_INFORMATION local = {};
    BOOL result = DetourCreateProcessWithDllExW(app, command, processAttr, threadAttr, inherit,
        flags, environment, directory, startupInfo, information ? information : &local,
        dllPath, RealCreateProcessW);
    DWORD error = GetLastError();
    Log(result ? "child-injected" : "child-injection-failed", nullptr, result ? 0 : error);
    if (result && !information) { CloseHandle(local.hProcess); CloseHandle(local.hThread); }
    SetLastError(error);
    return result;
}

static BOOL WINAPI HookShellExecuteExW(SHELLEXECUTEINFOW* info) {
    if (!info || !IsLineChild(info->lpFile, nullptr) ||
        (info->lpVerb && _wcsicmp(info->lpVerb, L"open") != 0)) return RealShellExecuteExW(info);
    wchar_t command[32768];
    if (swprintf_s(command, L"\"%s\" %s", info->lpFile, info->lpParameters ? info->lpParameters : L"") < 0) {
        SetLastError(ERROR_BAD_ARGUMENTS);
        return FALSE;
    }
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = static_cast<WORD>(info->nShow);
    PROCESS_INFORMATION pi = {};
    BOOL result = DetourCreateProcessWithDllExW(info->lpFile, command, nullptr, nullptr, FALSE,
        CREATE_DEFAULT_ERROR_MODE, nullptr, info->lpDirectory, &si, &pi, dllPath, RealCreateProcessW);
    DWORD error = GetLastError();
    Log(result ? "shell-child-injected" : "shell-child-injection-failed", nullptr, result ? 0 : error);
    if (result) {
        CloseHandle(pi.hThread);
        if (info->fMask & SEE_MASK_NOCLOSEPROCESS) info->hProcess = pi.hProcess;
        else CloseHandle(pi.hProcess);
        info->hInstApp = reinterpret_cast<HINSTANCE>(33);
    }
    else info->hInstApp = reinterpret_cast<HINSTANCE>(SE_ERR_ACCESSDENIED);
    SetLastError(error);
    return result;
}

static HINSTANCE WINAPI HookShellExecuteW(HWND window, LPCWSTR verb, LPCWSTR file,
    LPCWSTR parameters, LPCWSTR directory, int show) {
    if (!IsLineChild(file, nullptr) || (verb && _wcsicmp(verb, L"open") != 0))
        return RealShellExecuteW(window, verb, file, parameters, directory, show);
    SHELLEXECUTEINFOW info = { sizeof(info) };
    info.hwnd = window;
    info.lpVerb = verb;
    info.lpFile = file;
    info.lpParameters = parameters;
    info.lpDirectory = directory;
    info.nShow = show;
    HookShellExecuteExW(&info);
    return info.hInstApp;
}

#ifndef LINETRAY_COMPOSE_TEST
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (DetourIsHelperProcess()) return TRUE;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DetourRestoreAfterWith();
    DisableThreadLibraryCalls(instance);
    GetModuleFileNameA(instance, dllPath, MAX_PATH);
    GetEnvironmentVariableW(L"LOCALAPPDATA", logPath, MAX_PATH);
    wcscat_s(logPath, L"\\LineTrayStartup\\diagnostic.log");
    wchar_t executable[MAX_PATH];
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    wcscpy_s(logoSource, executable);
    LPCWSTR name = wcsrchr(executable, L'\\');
    bool isLine = name && _wcsicmp(name + 1, L"LINE.exe") == 0;
    launcher = name && _wcsicmp(name + 1, L"LineLauncher.exe") == 0;
    bool isUpdater = name && _wcsicmp(name + 1, L"LineUpdater.exe") == 0;
    wchar_t marker[MAX_PATH];
    wcscpy_s(marker, logPath);
    wcscpy_s(wcsrchr(marker, L'\\') + 1, 32, L"tray-dump.on");
    dumpTray = GetFileAttributesW(marker) != INVALID_FILE_ATTRIBUTES;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    if (isLine || launcher) {
        DetourAttach(&(PVOID&)RealCreateWindowExW, HookCreateWindowExW);
        DetourAttach(&(PVOID&)RealShowWindow, HookShowWindow);
        DetourAttach(&(PVOID&)RealShowWindowAsync, HookShowWindowAsync);
        DetourAttach(&(PVOID&)RealSetWindowPos, HookSetWindowPos);
        if (isLine) DetourAttach(&(PVOID&)RealNotifyIcon, HookNotifyIcon);
    }
    // LINE can replace itself through LineUpdater during startup. Propagate the
    // hook from LINE and the updater too, so the updated process remains covered.
    DetourAttach(&(PVOID&)RealCreateProcessW, HookCreateProcessW);
    DetourAttach(&(PVOID&)RealCreateProcessA, HookCreateProcessA);
    DetourAttach(&(PVOID&)RealShellExecuteExW, HookShellExecuteExW);
    DetourAttach(&(PVOID&)RealShellExecuteW, HookShellExecuteW);
    LONG error = DetourTransactionCommit();
    Log(isLine ? "line-hook-attached" : isUpdater ? "updater-hook-attached" : "launcher-hook-attached", nullptr, error);
    return error == NO_ERROR;
}
#endif
