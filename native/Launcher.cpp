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
    GetModuleFileNameA(nullptr, hook, MAX_PATH);
    char* tail = strrchr(hook, '\\');
    if (!tail) return 1;
    strcpy_s(tail + 1, MAX_PATH - (tail + 1 - hook), "LineTrayHook32.dll");
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
        MessageBoxW(nullptr, message, L"Windows版LINEを通知領域で起動", MB_OK | MB_ICONERROR);
        return 1;
    }
    Log("launcher-created", pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
