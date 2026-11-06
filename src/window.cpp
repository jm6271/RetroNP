#include "app.h"
#include <shellapi.h>
#include <windowsx.h>

namespace retronp {

// Attach the controller during WM_NCCREATE, before any other window messages.
// GWLP_USERDATA borrows App; the entry point owns it for the message-loop lifetime.
LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        app->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app)
        return DefWindowProcW(hwnd, msg, w, l);
    auto point = [&]() {
        return POINT{MulDiv(GET_X_LPARAM(l), 96, app->dpi), MulDiv(GET_Y_LPARAM(l), 96, app->dpi)};
    };
    switch (msg) {
    case WM_CREATE:
        app->dpi = static_cast<int>(GetDpiForWindow(hwnd));
        app->fonts();
        SetTimer(hwnd, 1, 500, nullptr);
        DragAcceptFiles(hwnd, TRUE);
        return 0;
    case WM_SIZE: {
        RECT r{};
        GetClientRect(hwnd, &r);
        app->width = MulDiv(r.right, 96, app->dpi);
        app->height = MulDiv(r.bottom, 96, app->dpi);
        app->layout();
        app->invalidate();
        return 0;
    }
    case WM_DPICHANGED: {
        app->dpi = HIWORD(w);
        auto r = reinterpret_cast<RECT*>(l);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        app->fonts();
        app->reveal();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto mm = reinterpret_cast<MINMAXINFO*>(l);
        mm->ptMinTrackSize = {MulDiv(440, app->dpi, 96), MulDiv(280, app->dpi, 96)};
        HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(monitor, &mi);
        mm->ptMaxPosition = {mi.rcWork.left - mi.rcMonitor.left, mi.rcWork.top - mi.rcMonitor.top};
        mm->ptMaxSize = {mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top};
        return 0;
    }
    case WM_NCCALCSIZE:
        if (w)
            return 0;
        break;
    // The whole window, including its frame, is owned by the custom renderer.
    // DefWindowProc must not repaint native non-client chrome over our canvas.
    case WM_NCPAINT:
        return 0;
    case WM_NCACTIVATE:
        return TRUE;
    case WM_NCHITTEST: {
        POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ScreenToClient(hwnd, &p);
        RECT r{};
        GetClientRect(hwnd, &r);
        int b = MulDiv(4, app->dpi, 96);
        if (!IsZoomed(hwnd) && app->dialog == Dialog::None) {
            bool left = p.x < b, right = p.x >= r.right - b, top = p.y < b, bottom = p.y >= r.bottom - b;
            if (top && left)
                return HTTOPLEFT;
            if (top && right)
                return HTTOPRIGHT;
            if (bottom && left)
                return HTBOTTOMLEFT;
            if (bottom && right)
                return HTBOTTOMRIGHT;
            if (left)
                return HTLEFT;
            if (right)
                return HTRIGHT;
            if (top)
                return HTTOP;
            if (bottom)
                return HTBOTTOM;
        }
        return HTCLIENT;
    }
    case WM_PAINT:
        app->paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_KEYDOWN:
        app->key(w);
        return 0;
    case WM_CHAR:
        app->character(static_cast<wchar_t>(w));
        return 0;
    case WM_SYSKEYDOWN:
        if (w == VK_F4) {
            if (app->dialog != Dialog::None)
                app->cancelDialog();
            else
                app->request(Pending::Exit);
            return 0;
        }
        if (app->dialog == Dialog::None) {
            int index = w == 'F' ? 0 : w == 'E' ? 1 : w == 'S' ? 2 : w == 'H' ? 3 : -1;
            if (index >= 0) {
                app->activeMenu = index;
                app->hover = -1;
                app->invalidate();
                return 0;
            }
        }
        break;
    case WM_SYSCHAR:
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        auto p = point();
        app->mouseDown(p.x, p.y, msg == WM_LBUTTONDBLCLK);
        return 0;
    }
    case WM_MOUSEMOVE: {
        auto p = point();
        app->mouseMove(p.x, p.y);
        return 0;
    }
    case WM_LBUTTONUP: {
        auto p = point();
        app->mouseUp(p.x, p.y);
        return 0;
    }
    case WM_CAPTURECHANGED:
        app->selecting = false;
        app->inputSelecting = false;
        app->scrollDrag = 0;
        app->titlePress = -1;
        return 0;
    case WM_MOUSEWHEEL:
        if (app->dialog == Dialog::None) {
            int amount = GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * 3;
            if (GET_KEYSTATE_WPARAM(w) & MK_SHIFT)
                app->left = std::clamp(app->left - amount, 0, app->horizontal.max);
            else
                app->top = std::clamp(app->top - amount, 0, app->vertical.max);
            app->invalidate();
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) {
            POINT p{};
            GetCursorPos(&p);
            ScreenToClient(hwnd, &p);
            p.x = MulDiv(p.x, 96, app->dpi);
            p.y = MulDiv(p.y, 96, app->dpi);
            bool edit = app->dialog == Dialog::None
                            ? app->activeMenu < 0 && App::inside(app->editorRect(), p.x, p.y)
                            : app->dialogFocus == 0 && App::inside(app->inputRect(), p.x, p.y);
            SetCursor(LoadCursorW(nullptr, edit ? IDC_IBEAM : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_SETFOCUS:
        app->focused = true;
        app->blink = true;
        app->invalidate();
        return 0;
    case WM_KILLFOCUS:
        app->focused = false;
        app->activeMenu = -1;
        app->invalidate();
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE) {
            app->activeMenu = -1;
            app->invalidate();
        }
        break;
    case WM_TIMER:
        if (w == 1)
            app->blinkCaret();
        return 0;
    case WM_DROPFILES: {
        auto drop = reinterpret_cast<HDROP>(w);
        UINT n = DragQueryFileW(drop, 0, nullptr, 0);
        std::wstring p(n + 1, L'\0');
        DragQueryFileW(drop, 0, p.data(), n + 1);
        p.resize(n);
        DragFinish(drop);
        if (app->dialog == Dialog::None) {
            app->droppedPath = p;
            app->request(Pending::Drop);
        }
        return 0;
    }
    case WM_CLOSE:
        if (app->dialog != Dialog::None)
            app->cancelDialog();
        else
            app->request(Pending::Exit);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

} // namespace retronp
