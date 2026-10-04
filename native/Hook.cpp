#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
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
static bool startup = true;
static bool launcher = false;
static HWND closing = nullptr;
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

static LRESULT CALLBACK MainProc(HWND window, UINT message, WPARAM wp, LPARAM lp,
    UINT_PTR, DWORD_PTR) {
    LRESULT result = DefSubclassProc(window, message, wp, lp);
    if (message == WM_CLOSE || message == WM_NCDESTROY) {
        startup = false;
        closing = nullptr;
        RemoveWindowSubclass(window, MainProc, 1);
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
        // Close through Qt's usual close-to-tray handling before its first native show.
        if (!SetWindowSubclass(window, MainProc, 1, 0)) {
            Log("subclass-failed", window, GetLastError());
            startup = false;
            return false;
        }
        closing = window;
        if (!PostMessageW(window, WM_CLOSE, 0, 0)) {
            RemoveWindowSubclass(window, MainProc, 1);
            startup = false;
            closing = nullptr;
            return false;
        }
        Log("startup-show-suppressed", window);
    }
    return window == closing;
}

static BOOL WINAPI HookShowWindow(HWND window, int command) {
    if (command != SW_HIDE && startup) {
        Log("show-request", window, command);
        if (Suppress(window)) return IsWindowVisible(window);
    }
    return RealShowWindow(window, command);
}

static BOOL WINAPI HookShowWindowAsync(HWND window, int command) {
    if (command != SW_HIDE && Suppress(window)) return TRUE;
    return RealShowWindowAsync(window, command);
}

static BOOL WINAPI HookSetWindowPos(HWND window, HWND after, int x, int y, int cx, int cy, UINT flags) {
    if ((flags & SWP_SHOWWINDOW) && startup) {
        Log("position-show-request", window, flags);
        if (Suppress(window)) flags &= ~SWP_SHOWWINDOW;
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
    if (visible) Suppress(window);
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
            Log("tray-user-activation", window, event);
        }
    }
    return DefSubclassProc(window, message, wp, lp);
}

static BOOL WINAPI HookNotifyIcon(DWORD operation, PNOTIFYICONDATAW original) {
    if (original && operation == NIM_ADD && (original->uFlags & NIF_MESSAGE)) {
        SetWindowSubclass(original->hWnd, TrayProc, 2, original->uCallbackMessage);
    }
    if (original && (operation == NIM_ADD || operation == NIM_MODIFY) && (original->uFlags & NIF_ICON)) {
        if (!iconLoaded) {
            iconLoaded = true;
            wchar_t executable[MAX_PATH];
            GetModuleFileNameW(nullptr, executable, MAX_PATH);
            ExtractIconExW(executable, 0, nullptr, &lineIcon, 1);
        }
        if (lineIcon) {
            NOTIFYICONDATAW changed = {};
            memcpy(&changed, original, min(original->cbSize, static_cast<DWORD>(sizeof(changed))));
            changed.hIcon = lineIcon;
            Log("tray-icon", original->hWnd, operation);
            return RealNotifyIcon(operation, &changed);
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
    LPCWSTR name = wcsrchr(executable, L'\\');
    bool isLine = name && _wcsicmp(name + 1, L"LINE.exe") == 0;
    launcher = name && _wcsicmp(name + 1, L"LineLauncher.exe") == 0;
    bool isUpdater = name && _wcsicmp(name + 1, L"LineUpdater.exe") == 0;
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
