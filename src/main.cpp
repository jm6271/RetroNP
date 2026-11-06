#include "app.h"
#include <shellapi.h>

using namespace retronp;

// Startup owns the controller; all document and UI behavior lives in the library.
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HICON appIcon = createNotepadIcon();
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"RetroNP.Canvas";
    wc.hIcon = appIcon;
    wc.hIconSm = appIcon;
    if (!RegisterClassExW(&wc))
        return 1;
    App app;
    int scale = static_cast<int>(GetDpiForSystem());
    HWND hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, L"Untitled - RetroNP",
                                WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU,
                                CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(960, scale, 96), MulDiv(660, scale, 96),
                                nullptr, nullptr, instance, &app);
    if (!hwnd)
        return 1;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 1 && std::wstring(argv[1]) == L"--demo") {
        app.editor.reset(L"#include <iostream>\n\nint main()\n{\n    std::cout << \"Hello World!\\n\";\n    "
                         L"return 0;\n}\n");
        app.path = L"hello.cpp";
        app.updateTitle();
    } else if (argc > 1)
        app.load(argv[1]);
    if (argv)
        LocalFree(argv);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    MSG msg{};
    int result;
    while ((result = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (appIcon)
        DestroyIcon(appIcon);
    return result < 0 ? 1 : static_cast<int>(msg.wParam);
}
