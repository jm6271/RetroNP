#include "app.h"

namespace retronp {

int App::menuHit(int x, int y) const {
    if (activeMenu < 0)
        return -1;
    RECT r = menuRect();
    if (!inside(r, x, y))
        return -1;
    int yy = r.top + 3;
    auto& items = menus[activeMenu].items;
    for (size_t i = 0; i < items.size(); ++i) {
        int h = items[i].command ? 19 : 9;
        if (y >= yy && y < yy + h)
            return items[i].command ? static_cast<int>(i) : -1;
        yy += h;
    }
    return -1;
}

size_t App::hitText(int x, int y) const {
    auto r = editorRect();
    return positionAtColumn(top + (y - r.top - 2) / lineHeight,
                            left + std::max<int>(0, (x - r.left - 3 + characterWidth / 2) / characterWidth));
}

size_t App::hitInput(int x) const {
    auto r = inputRect();
    int visible = std::max(1, static_cast<int>((r.right - r.left - 6) / characterWidth));
    int offset = std::max(0, static_cast<int>(input.caret) - visible + 1);
    return static_cast<size_t>(
        std::clamp<int>(offset + (x - r.left - 3 + characterWidth / 2) / characterWidth, 0,
                        static_cast<int>(input.text.size())));
}

bool App::scrollClick(Scroll& s, bool vert, int x, int y) {
    // Arrows move by one unit, the trough by a page, and the thumb captures
    // the pointer so dragging continues outside the scrollbar rectangle.
    int& pos = vert ? top : left;
    if (inside(s.before, x, y)) {
        pos = std::max(0, pos - 1);
    } else if (inside(s.after, x, y)) {
        pos = std::min(s.max, pos + 1);
    } else if (inside(s.track, x, y)) {
        if (s.max && inside(s.thumb, x, y)) {
            scrollDrag = vert ? 1 : 2;
            dragOffset = vert ? y - s.thumb.top : x - s.thumb.left;
            SetCapture(hwnd);
        } else {
            bool before = vert ? y < s.thumb.top : x < s.thumb.left;
            pos = std::clamp(pos + (before ? -s.pageSize : s.pageSize), 0, s.max);
        }
    } else
        return false;
    invalidate();
    return true;
}

void App::mouseDown(int x, int y, bool doubleClick) {
    // Hit testing follows the same front-to-back order as painting: modal
    // dialogs, popup menus, title controls, scrollbars, then document text.
    SetFocus(hwnd);
    if (dialog != Dialog::None) {
        if (showOverwrite) {
            auto r = dialogRect();
            RECT yes = {r.right - 192, r.bottom - 39, r.right - 112, r.bottom - 15},
                 no = {r.right - 104, r.bottom - 39, r.right - 24, r.bottom - 15};
            if (inside(yes, x, y))
                overwriteAnswer(true);
            else if (inside(no, x, y))
                overwriteAnswer(false);
            return;
        }
        auto buttons = dialogButtons();
        for (size_t i = 0; i < buttons.size(); ++i)
            if (inside(dialogButton(static_cast<int>(i)), x, y)) {
                acceptDialog(static_cast<int>(i));
                return;
            }
        if ((dialog == Dialog::Open || dialog == Dialog::Save || dialog == Dialog::Find) &&
            inside(inputRect(), x, y)) {
            dialogFocus = 0;
            input.move(hitInput(x), (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            inputSelecting = true;
            SetCapture(hwnd);
            if (doubleClick) {
                input.anchor = 0;
                input.caret = input.text.size();
            }
        }
        invalidate();
        return;
    }
    if (activeMenu >= 0 && inside(menuRect(), x, y)) {
        int i = menuHit(x, y);
        if (i >= 0 && enabled(menus[activeMenu].items[i].command))
            execute(menus[activeMenu].items[i].command);
        return;
    }
    for (size_t i = 0; i < menus.size(); ++i) {
        auto& m = menus[i];
        if (inside({m.x, 24, m.x + m.width, 43}, x, y)) {
            activeMenu = activeMenu == static_cast<int>(i) ? -1 : static_cast<int>(i);
            hover = -1;
            invalidate();
            return;
        }
    }
    if (activeMenu >= 0) {
        activeMenu = -1;
        invalidate();
        return;
    }
    for (int i = 0; i < 3; ++i)
        if (inside(titleButton(i), x, y)) {
            titlePress = i;
            SetCapture(hwnd);
            invalidate();
            return;
        }
    if (y >= 4 && y < 23 && x > 4 && x < width - 60) {
        if (doubleClick)
            ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
        else {
            ReleaseCapture();
            SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        return;
    }
    if (scrollClick(vertical, true, x, y) || scrollClick(horizontal, false, x, y))
        return;
    if (inside(editorRect(), x, y)) {
        editor.move(hitText(x, y), (GetKeyState(VK_SHIFT) & 0x8000) != 0);
        preferredColumn = -1;
        selecting = true;
        SetCapture(hwnd);
        if (doubleClick && !editor.text.empty()) {
            size_t p = std::min(editor.caret, editor.text.size() - 1);
            bool word = iswalnum(editor.text[p]) || editor.text[p] == L'_';
            size_t a = p, b = editor.next(p);
            if (word) {
                while (a &&
                       (iswalnum(editor.text[editor.previous(a)]) || editor.text[editor.previous(a)] == L'_'))
                    a = editor.previous(a);
                while (b < editor.text.size() && (iswalnum(editor.text[b]) || editor.text[b] == L'_'))
                    b = editor.next(b);
            }
            editor.anchor = a;
            editor.caret = b;
        }
        reveal();
    }
}

void App::mouseMove(int x, int y) {
    // A captured interaction takes priority over hover. Selection extends
    // outside the editor and scrolls one row/column per move at its edges.
    if (inputSelecting) {
        input.caret = hitInput(x);
        invalidate();
        return;
    }
    if (scrollDrag) {
        Scroll& s = scrollDrag == 1 ? vertical : horizontal;
        bool vert = scrollDrag == 1;
        int length = vert ? s.track.bottom - s.track.top - (s.thumb.bottom - s.thumb.top)
                          : s.track.right - s.track.left - (s.thumb.right - s.thumb.left);
        int at = (vert ? y - s.track.top : x - s.track.left) - dragOffset;
        int value = length > 0 ? std::clamp(at * s.max / length, 0, s.max) : 0;
        (vert ? top : left) = value;
        invalidate();
        return;
    }
    if (selecting) {
        auto r = editorRect();
        if (y < r.top)
            top = std::max(0, top - 1);
        else if (y >= r.bottom)
            top = std::min(vertical.max, top + 1);
        if (!wrap) {
            if (x < r.left)
                left = std::max(0, left - 1);
            else if (x >= r.right)
                left = std::min(horizontal.max, left + 1);
        }
        editor.caret = hitText(x, y);
        blink = true;
        invalidate();
        return;
    }
    if (activeMenu >= 0) {
        int previousMenu = activeMenu, previousHover = hover;
        for (size_t i = 0; i < menus.size(); ++i)
            if (inside({menus[i].x, 24, menus[i].x + menus[i].width, 43}, x, y))
                activeMenu = static_cast<int>(i);
        hover = menuHit(x, y);
        if (activeMenu != previousMenu)
            invalidate();
        else if (hover != previousHover)
            invalidate(menuRect());
    }
}

void App::mouseUp(int x, int y) {
    int pressed = titlePress;
    selecting = false;
    inputSelecting = false;
    scrollDrag = 0;
    ReleaseCapture();
    titlePress = -1;
    if (pressed >= 0 && inside(titleButton(pressed), x, y)) {
        if (pressed == 0)
            ShowWindow(hwnd, SW_MINIMIZE);
        else if (pressed == 1)
            ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
        else
            request(Pending::Exit);
    }
    invalidate();
}

} // namespace retronp
