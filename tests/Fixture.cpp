#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#if !defined(TEST_LAUNCHER) && !defined(TEST_UPDATER) && !defined(TEST_APP_MANAGER)
static int closes = 0;
static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CLOSE) { ++closes; ShowWindow(h, SW_HIDE); return 0; }
    return DefWindowProcW(h, m, w, l);
}
#endif
static int StartChild(const wchar_t* relative, const wchar_t* arguments, int api) {
    wchar_t file[MAX_PATH];
    GetModuleFileNameW(nullptr, file, MAX_PATH);
    wcscpy_s(wcsrchr(file, L'\\') + 1, MAX_PATH - (wcsrchr(file, L'\\') + 1 - file), relative);
    HANDLE process = nullptr;
    if (api == 0) {
        SHELLEXECUTEINFOW info = {sizeof(info)};
        info.fMask = SEE_MASK_NOCLOSEPROCESS;
        info.lpFile = file; info.lpParameters = arguments; info.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&info)) return 20;
        process = info.hProcess;
    } else {
        wchar_t command[MAX_PATH + 128];
        swprintf_s(command, L"\"%s\" %s", file, arguments);
        PROCESS_INFORMATION information = {};
        BOOL created;
        if (api == 1) {
            STARTUPINFOW startup = {sizeof(startup)};
            created = CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &information);
        } else {
            char ansi[(MAX_PATH + 128) * 4];
            if (!WideCharToMultiByte(CP_ACP, 0, command, -1, ansi, sizeof(ansi), nullptr, nullptr)) return 23;
            STARTUPINFOA startup = {sizeof(startup)};
            created = CreateProcessA(nullptr, ansi, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &information);
        }
        if (!created) return 24;
        CloseHandle(information.hThread);
        process = information.hProcess;
    }
    if (WaitForSingleObject(process, 15000) != WAIT_OBJECT_0) { CloseHandle(process); return 25; }
    DWORD code = 21; GetExitCodeProcess(process, &code); CloseHandle(process);
    return static_cast<int>(code);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR arguments, int) {
#ifdef TEST_LAUNCHER
    (void)arguments;
    WNDCLASSW cls = {};
    cls.hInstance = instance; cls.lpfnWndProc = DefWindowProcW; cls.lpszClassName = L"SPLASH";
    RegisterClassW(&cls);
    HWND splash = CreateWindowExW(0, cls.lpszClassName, L"Connecting to LINE", WS_POPUP | WS_VISIBLE,
        0, 0, 200, 100, nullptr, nullptr, instance, nullptr);
    ShowWindow(splash, SW_SHOWNORMAL);
    ShowWindow(splash, SW_SHOWNORMAL);
    ShowWindowAsync(splash, SW_SHOWNORMAL);
    SetWindowPos(splash, nullptr, 0, 0, 200, 100, SWP_SHOWWINDOW | SWP_NOZORDER);
    bool splashHidden = !IsWindowVisible(splash);
    HWND other = CreateWindowExW(0, L"STATIC", L"fixture unrelated", WS_POPUP,
        0, 0, 20, 20, nullptr, nullptr, instance, nullptr);
    ShowWindow(other, SW_SHOWNORMAL);
    ShowWindow(other, SW_SHOWNORMAL);
    bool otherVisible = IsWindowVisible(other);
    DestroyWindow(other);
    wchar_t update[2], eventName[128], file[MAX_PATH];
    bool chain = GetEnvironmentVariableW(L"LINE_TRAY_FIXTURE_UPDATE", update, 2) != 0;
    int code = StartChild(L"current\\LINE.exe", chain ? L"--fixture-update" : L"run --booting", 0);
    DestroyWindow(splash);
    if (!splashHidden || !otherVisible) code = 26;
    GetEnvironmentVariableW(L"LOCALAPPDATA", file, MAX_PATH);
    wcscat_s(file, L"\\fixture-chain-result.txt");
    FILE* result = nullptr; _wfopen_s(&result, file, L"w");
    if (result) { fprintf(result, "exit=%d\n", code); fclose(result); }
    if (GetEnvironmentVariableW(L"LINE_TRAY_FIXTURE_DONE", eventName, 128)) {
        HANDLE done = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName);
        if (done) { SetEvent(done); CloseHandle(done); }
    }
    return code;
#elif defined(TEST_APP_MANAGER)
    (void)instance; (void)arguments;
    return StartChild(L"LineUpdater.exe", L"--fixture-relay", 1);
#elif defined(TEST_UPDATER)
    (void)instance; (void)arguments;
    return StartChild(L"LINE.exe", L"run --updated fixture silent", 2);
#else
    if (wcsstr(arguments, L"--fixture-update")) return StartChild(L"LineAppMgr.exe", L"--fixture-relay", 0);
    WNDCLASSW cls = {};
    cls.hInstance = instance; cls.lpfnWndProc = Proc; cls.lpszClassName = L"Qt663QWindowIcon";
    RegisterClassW(&cls);
    HWND splash = CreateWindowExW(WS_EX_TOOLWINDOW, cls.lpszClassName, L"", WS_POPUP | WS_VISIBLE,
        0, 0, 200, 100, nullptr, nullptr, instance, nullptr);
    ShowWindow(splash, SW_SHOWNORMAL);
    SetWindowPos(splash, nullptr, 0, 0, 200, 100, SWP_SHOWWINDOW | SWP_NOZORDER);
    bool splashHidden = !IsWindowVisible(splash);
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"LINE", WS_OVERLAPPEDWINDOW,
        0, 0, 400, 300, nullptr, nullptr, instance, nullptr);
    ShowWindow(window, SW_SHOWNORMAL);
    bool initiallyHidden = !IsWindowVisible(window);
    MSG message;
    // Drain the fixture's queued WM_CLOSE. The production helper has no message loop.
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
    bool closeHidden = !IsWindowVisible(window) && closes == 1;
    ShowWindow(window, SW_SHOWNORMAL);
    bool reopened = IsWindowVisible(window);
    ShowWindow(window, SW_HIDE);
    wchar_t file[MAX_PATH]; GetEnvironmentVariableW(L"LOCALAPPDATA", file, MAX_PATH);
    wcscat_s(file, L"\\fixture-result.txt");
    FILE* result = nullptr; _wfopen_s(&result, file, L"w");
    if (result) { fprintf(result, "splashHidden=%d initiallyHidden=%d closeHidden=%d reopened=%d\n", splashHidden, initiallyHidden, closeHidden, reopened); fclose(result); }
    DestroyWindow(splash);
    DestroyWindow(window);
    return splashHidden && initiallyHidden && closeHidden && reopened ? 0 : 22;
#endif
}
