// Checks the tray icon composition with synthetic inputs (no LINE artwork):
// a colourful square, a grey square and a colourful square with an orange badge.
// Usage: TrayIconTest.exe <icon file used as the logo> <output folder>
#define LINETRAY_COMPOSE_TEST
#include "../native/Hook.cpp"

static HICON Source(int kind) {
    const int size = 32;
    Pixels pixels;
    pixels.width = pixels.height = size;
    pixels.data = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size * size * 4));
    for (int y = 2; y < 30; y++) for (int x = 2; x < 30; x++) {
        BYTE* p = pixels.data + (y * size + x) * 4;
        bool bubble = x > 7 && x < 24 && y > 9 && y < 21;
        BYTE base = kind == 1 ? 0xBD : 0x00;
        p[0] = bubble ? 0xFF : (kind == 1 ? base : 0x30);
        p[1] = bubble ? 0xFF : (kind == 1 ? base : 0xC0);
        p[2] = bubble ? 0xFF : (kind == 1 ? base : 0x10);
        p[3] = 0xFF;
    }
    if (kind == 2) {
        for (int y = 6; y < 22; y++) for (int x = 18; x < 31; x++) {
            BYTE* p = pixels.data + (y * size + x) * 4;
            bool letter = (x == 22 || x == 27 || x - 22 == (y - 9) * 5 / 9) && y > 8 && y < 19;
            p[0] = letter ? 0xFF : 0x3C; p[1] = letter ? 0xFF : 0x5F; p[2] = letter ? 0xFF : 0xF5; p[3] = 0xFF;
        }
    }
    HICON icon = MakeIcon(pixels);
    FreePixels(pixels);
    return icon;
}

// Optional: further arguments are 32-bit top-down BMPs saved by the tray dump, for checking
// the composition against real LINE icons locally. Those images are not part of the repository.
static void ComposeFiles(int argc, wchar_t** argv) {
    for (int i = 3; i < argc; i++) {
        HANDLE file = CreateFileW(argv[i], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE) continue;
        BITMAPFILEHEADER head; BITMAPINFOHEADER info; DWORD read;
        ReadFile(file, &head, sizeof(head), &read, nullptr);
        ReadFile(file, &info, sizeof(info), &read, nullptr);
        Pixels pixels;
        pixels.width = info.biWidth; pixels.height = info.biHeight < 0 ? -info.biHeight : info.biHeight;
        DWORD bytes = pixels.width * pixels.height * 4;
        pixels.data = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), 0, bytes));
        ReadFile(file, pixels.data, bytes, &read, nullptr);
        CloseHandle(file);
        HICON source = MakeIcon(pixels);
        LONG state = -1;
        HICON composed = ComposeTrayIcon(source, &state);
        wprintf(L"%s: state=%ld\n", argv[i], state);
        if (composed) DestroyIcon(composed);
        DestroyIcon(source);
        FreePixels(pixels);
    }
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return 2;
    wcscpy_s(logoSource, argv[1]);
    swprintf_s(logPath, L"%s\\diagnostic.log", argv[2]);
    dumpTray = true;
    if (argc > 3) { ComposeFiles(argc, argv); return 0; }
    const LONG expected[] = { 0, 1, 2 };
    const wchar_t* names[] = { L"online", L"connecting", L"unread" };
    int failures = 0;
    for (int kind = 0; kind < 3; kind++) {
        HICON source = Source(kind);
        LONG state = -1;
        HICON composed = ComposeTrayIcon(source, &state);
        Pixels result;
        bool read = composed && ReadIcon(composed, result);
        bool sized = read && result.width == 32 && result.height == 32;
        // The badge corner must carry the orange badge only in the unread case.
        bool orange = sized && IsBadge(result.data + (7 * 32 + 19) * 4);
        bool ok = composed && sized && state == expected[kind] && orange == (kind == 2);
        wprintf(L"%s: state=%ld orange=%d %s\n", names[kind], state, orange ? 1 : 0, ok ? L"ok" : L"FAIL");
        if (!ok) failures++;
        FreePixels(result);
        if (composed) DestroyIcon(composed);
        DestroyIcon(source);
    }
    wprintf(failures ? L"FAIL\n" : L"PASS: online, connecting (grey) and unread (badge) tray icons.\n");
    return failures ? 1 : 0;
}
