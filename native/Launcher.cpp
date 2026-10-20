#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <detours.h>
#include <stdio.h>

static void Log(const char* event, DWORD value = 0) {
    wchar_t path[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH)) return;
    wcscat_s(path, L"\\LineTrayStartup\\diagnostic.log");
    SYSTEMTIME time;
    GetSystemTime(&time);
    char data[256];
    int count = sprintf_s(data, "%04u-%02u-%02uT%02u:%02u:%02uZ tick=%llu pid=%lu %s value=%lu\r\n",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
        GetTickCount64(), GetCurrentProcessId(), event, value);
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(file, data, count, &written, nullptr);
        CloseHandle(file);
    }
}

// LINE keeps the hook DLLs loaded while it runs. Inject copies from the data folder so
// the installed files stay replaceable by updates. Each build gets its own folder.
static bool PrepareHook(char* hook, size_t size) {
    wchar_t source[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, source, MAX_PATH);
    wchar_t* tail = wcsrchr(source, L'\\');
    if (!tail) return false;
    wcscpy_s(tail + 1, MAX_PATH - (tail + 1 - source), L"LineTrayHook32.dll");
    WIN32_FILE_ATTRIBUTE_DATA info = {};
    if (!GetFileAttributesExW(source, GetFileExInfoStandard, &info)) return false;
    wchar_t folder[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", folder, MAX_PATH)) return false;
    wcscat_s(folder, L"\\LineTrayStartup");
    CreateDirectoryW(folder, nullptr);
    wcscat_s(folder, L"\\hook");
    CreateDirectoryW(folder, nullptr);
    wchar_t name[32];
    swprintf_s(name, L"\\%08lx%08lx", info.ftLastWriteTime.dwHighDateTime, info.ftLastWriteTime.dwLowDateTime);
    wcscat_s(folder, name);
    CreateDirectoryW(folder, nullptr);
    static const wchar_t* files[] = { L"LineTrayHook32.dll", L"LineTrayHook64.dll" };
    for (const wchar_t* file : files) {
        wchar_t from[MAX_PATH], to[MAX_PATH];
        wcscpy_s(from, source);
        wcscpy_s(wcsrchr(from, L'\\') + 1, MAX_PATH - (wcsrchr(from, L'\\') + 1 - from), file);
        swprintf_s(to, L"%s\\%s", folder, file);
        // An existing copy may be loaded by a running LINE; it is the same build.
        if (GetFileAttributesW(to) == INVALID_FILE_ATTRIBUTES && !CopyFileW(from, to, TRUE)) return false;
    }
    wchar_t target[MAX_PATH];
    swprintf_s(target, L"%s\\LineTrayHook32.dll", folder);
    return WideCharToMultiByte(CP_ACP, 0, target, -1, hook, static_cast<int>(size), nullptr, nullptr) != 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    Log("helper-start");
    wchar_t launcher[MAX_PATH] = {}, command[MAX_PATH + 32] = {};
    GetEnvironmentVariableW(L"LOCALAPPDATA", launcher, MAX_PATH);
    wcscat_s(launcher, L"\\LINE\\bin\\LineLauncher.exe");
    swprintf_s(command, L"\"%s\" --booting", launcher);
    wchar_t directory[MAX_PATH];
    wcscpy_s(directory, launcher);
    *wcsrchr(directory, L'\\') = L'\0';
    char hook[MAX_PATH] = {};
    if (!PrepareHook(hook, MAX_PATH)) {
        DWORD error = GetLastError();
        Log("hook-copy-failed", error);
        wchar_t message[256];
        swprintf_s(message, L"LINEを通知領域で起動するための準備に失敗しました (Windows error %lu)。", error);
        MessageBoxW(nullptr, message, L"LINEを最小化して起動", MB_OK | MB_ICONERROR);
        return 1;
    }
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (!DetourCreateProcessWithDllExW(launcher, command, nullptr, nullptr, FALSE,
        CREATE_DEFAULT_ERROR_MODE, nullptr, directory, &si, &pi, hook, nullptr)) {
        DWORD error = GetLastError();
        Log("launcher-create-failed", error);
        wchar_t message[256];
        swprintf_s(message, L"LINEを通知領域で起動できませんでした (Windows error %lu)。", error);
        MessageBoxW(nullptr, message, L"LINEを最小化して起動", MB_OK | MB_ICONERROR);
        return 1;
    }
    Log("launcher-created", pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
