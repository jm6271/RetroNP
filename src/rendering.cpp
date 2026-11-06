#include "app.h"
#include <filesystem>

namespace retronp {

void App::fonts() {
    // These Windows bitmap faces need not be registered on modern Windows.
    // Load privately so "MS Sans Serif" cannot silently become Arial.
    if (privateFonts.empty()) {
        wchar_t windows[MAX_PATH]{};
        GetWindowsDirectoryW(windows, MAX_PATH);
        for (auto file : {L"vgafix.fon", L"vgasys.fon", L"sserife.fon"}) {
            auto fontPath = (std::filesystem::path(windows) / L"Fonts" / file).wstring();
            if (AddFontResourceExW(fontPath.c_str(), FR_PRIVATE, nullptr))
                privateFonts.push_back(fontPath);
        }
    }
    if (uiFont)
        DeleteObject(uiFont);
    if (titleFont)
        DeleteObject(titleFont);
    if (textFont)
        DeleteObject(textFont);
    uiFont = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_RASTER_PRECIS,
                         CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH, L"MS Sans Serif");
    titleFont = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_RASTER_PRECIS,
                            CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH, L"System");
    textFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_RASTER_PRECIS,
                           CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Fixedsys");
    HDC dc = CreateCompatibleDC(nullptr);
    auto old = SelectObject(dc, textFont);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    characterWidth = tm.tmAveCharWidth;
    lineHeight = std::max(16L, tm.tmHeight);
    SelectObject(dc, old);
    DeleteDC(dc);
}

void App::fill(HDC dc, RECT r, COLORREF color) {
    HBRUSH b = CreateSolidBrush(color);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

void App::line(HDC dc, int x1, int y1, int x2, int y2, COLORREF color) {
    HPEN p = CreatePen(PS_SOLID, 1, color);
    auto old = SelectObject(dc, p);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, old);
    DeleteObject(p);
}

void App::bevel(HDC dc, RECT r, bool down) {
    fill(dc, r, face);
    line(dc, r.left, r.bottom - 1, r.left, r.top, down ? dark : light);
    line(dc, r.left, r.top, r.right - 1, r.top, down ? dark : light);
    line(dc, r.right - 1, r.top, r.right - 1, r.bottom, down ? light : dark);
    line(dc, r.right - 1, r.bottom - 1, r.left, r.bottom - 1, down ? light : dark);
    InflateRect(&r, -1, -1);
    line(dc, r.left, r.bottom - 1, r.left, r.top, down ? shadow : face);
    line(dc, r.left, r.top, r.right - 1, r.top, down ? shadow : face);
    line(dc, r.right - 1, r.top, r.right - 1, r.bottom, down ? face : shadow);
    line(dc, r.right - 1, r.bottom - 1, r.left, r.bottom - 1, down ? face : shadow);
}

void App::text(HDC dc, const std::wstring& s, RECT r, COLORREF color, UINT flags) const {
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &r, flags);
}

bool App::inside(RECT r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

RECT App::titleButton(int i) const {
    int x = width - 22 - (2 - i) * 16 - (i < 2 ? 2 : 0);
    return {x, 6, x + 16, 20};
}

void App::icon(HDC dc, int x, int y) {
    for (int row = 0; row < 16; ++row)
        for (int column = 0; column < 16; ++column) {
            char p = notepadPixels[row][column];
            if (p != ' ')
                fill(dc, {x + column, y + row, x + column + 1, y + row + 1},
                     p == 'W'   ? light
                     : p == 'G' ? shadow
                                : dark);
        }
}

void App::title(HDC dc, RECT r, const std::wstring& name, bool active) {
    fill(dc, r, active ? blue : shadow);
    SelectObject(dc, titleFont);
    r.left += 5;
    text(dc, name, r, light, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void App::button(HDC dc, RECT r, const std::wstring& label, bool pressed, bool focus) {
    bevel(dc, r, pressed);
    if (pressed)
        OffsetRect(&r, 1, 1);
    SelectObject(dc, uiFont);
    text(dc, label, r, dark, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (focus) {
        InflateRect(&r, -4, -4);
        DrawFocusRect(dc, &r);
    }
}

void App::drawTitleButtons(HDC dc) {
    for (int i = 0; i < 3; ++i) {
        RECT b = titleButton(i);
        bool pressed = titlePress == i;
        bevel(dc, b, pressed);
        if (pressed)
            OffsetRect(&b, 1, 1);
        int x = b.left + 3, y = b.top + 3;
        if (i == 0)
            fill(dc, {x + 1, y + 6, x + 7, y + 8}, dark);
        else if (i == 1) {
            auto box = [&](int xx, int yy, int w, int h) {
                fill(dc, {xx, yy, xx + w, yy + h}, dark);
                fill(dc, {xx + 1, yy + 2, xx + w - 1, yy + h - 1}, face);
            };
            if (IsZoomed(hwnd)) {
                box(x + 2, y - 1, 7, 7);
                box(x, y + 1, 7, 7);
            } else
                box(x, y - 1, 9, 9);
        } else {
            constexpr BYTE glyph[] = {0xC3, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0xC3};
            for (int yy = 0; yy < 7; ++yy)
                for (int xx = 0; xx < 8; ++xx)
                    if (glyph[yy] & (0x80 >> xx))
                        fill(dc, {x + xx + 1, y + yy, x + xx + 2, y + yy + 1}, dark);
        }
    }
}

Scroll App::makeScroll(RECT r, bool vert, int pos, int max, int visible, int total) {
    Scroll s;
    s.max = max;
    s.pageSize = visible;
    s.before = r;
    s.after = r;
    if (vert) {
        s.before.bottom = r.top + 16;
        s.after.top = r.bottom - 16;
        r.top += 16;
        r.bottom -= 16;
    } else {
        s.before.right = r.left + 16;
        s.after.left = r.right - 16;
        r.left += 16;
        r.right -= 16;
    }
    s.track = r;
    int length = vert ? r.bottom - r.top : r.right - r.left;
    int thumb = max ? std::max(16, length * visible / std::max(1, total)) : length;
    thumb = std::min(length, thumb);
    int offset = max ? (length - thumb) * pos / max : 0;
    s.thumb = r;
    if (vert) {
        s.thumb.top += offset;
        s.thumb.bottom = s.thumb.top + thumb;
    } else {
        s.thumb.left += offset;
        s.thumb.right = s.thumb.left + thumb;
    }
    return s;
}

void App::drawScroll(HDC dc, Scroll& s, bool vert) {
    fill(dc, s.track, light);
    // Checkerboard scrollbar trough, as on Windows 95.
    const WORD bits[8] = {0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA};
    HBITMAP pattern = CreateBitmap(8, 8, 1, 1, bits);
    HBRUSH brush = CreatePatternBrush(pattern);
    SetTextColor(dc, face);
    SetBkColor(dc, light);
    FillRect(dc, &s.track, brush);
    DeleteObject(brush);
    DeleteObject(pattern);
    if (s.max > 0)
        bevel(dc, s.thumb);
    for (int i = 0; i < 2; ++i) {
        RECT r = i ? s.after : s.before;
        bevel(dc, r);
        int x = (r.left + r.right) / 2, y = (r.top + r.bottom) / 2;
        COLORREF c = s.max > 0 ? dark : shadow;
        for (int k = 0; k < 4; ++k) {
            if (vert) {
                int yy = y + (i ? 2 - k : k - 2);
                line(dc, x - k, yy, x + k + 1, yy, c);
            } else {
                int xx = x + (i ? 2 - k : k - 2);
                line(dc, xx, y - k, xx, y + k + 1, c);
            }
        }
    }
}

void App::drawEditor(HDC dc) {
    RECT r = editorRect();
    RECT border = {r.left - 2, r.top - 2, r.right + 18, r.bottom + 18};
    bevel(dc, border, true);
    fill(dc, r, light);
    int clip = SaveDC(dc);
    IntersectClipRect(dc, r.left, r.top, r.right, r.bottom);
    SelectObject(dc, textFont);
    SetBkMode(dc, OPAQUE);
    for (int row = top; row < static_cast<int>(rows.size()) && row <= top + visibleRows(); ++row) {
        auto segment = rows[row];
        int x = r.left + 3 - left * characterWidth, y = r.top + 2 + (row - top) * lineHeight, c = 0;
        for (size_t p = segment.start; p < segment.end;) {
            size_t next = editor.next(p);
            int n = editor.text[p] == L'\t' ? 8 - c % 8 : 1;
            RECT cell = {x + c * characterWidth, y, x + (c + n) * characterWidth, y + lineHeight};
            bool selected = p >= editor.lo() && p < editor.hi();
            if (cell.right >= r.left && cell.left < r.right) {
                SetTextColor(dc, selected ? light : dark);
                SetBkColor(dc, selected ? blue : light);
                if (editor.text[p] == L'\t')
                    ExtTextOutW(dc, cell.left, y, ETO_OPAQUE, &cell, L"", 0, nullptr);
                else
                    ExtTextOutW(dc, cell.left, y, ETO_OPAQUE | ETO_CLIPPED, &cell, editor.text.c_str() + p,
                                static_cast<UINT>(next - p), nullptr);
            }
            c += n;
            p = next;
        }
        if (segment.end < editor.text.size() && editor.text[segment.end] == L'\n' &&
            segment.end >= editor.lo() && segment.end < editor.hi())
            fill(dc, {x + c * characterWidth, y, x + (c + 1) * characterWidth, y + lineHeight}, blue);
    }
    RestoreDC(dc, clip);
    vertical = makeScroll({r.right, r.top, r.right + 16, r.bottom}, true, top,
                          std::max(0, static_cast<int>(rows.size()) - visibleRows()), visibleRows(),
                          static_cast<int>(rows.size()));
    horizontal =
        makeScroll({r.left, r.bottom, r.right, r.bottom + 16}, false, left,
                   wrap ? 0 : std::max(0, maxColumns() - columns() + 1), columns(), maxColumns() + 1);
    drawScroll(dc, vertical, true);
    drawScroll(dc, horizontal, false);
    fill(dc, {r.right, r.bottom, r.right + 16, r.bottom + 16}, face);
    for (int k = 0; k < 3; ++k) {
        int t = k * 4;
        line(dc, r.right + 3 + t, r.bottom + 14, r.right + 14, r.bottom + 3 + t, light);
        line(dc, r.right + 4 + t, r.bottom + 14, r.right + 14, r.bottom + 4 + t, shadow);
    }
}

RECT App::menuRect() const {
    const auto& m = menus[activeMenu];
    int h = 6;
    for (auto& i : m.items)
        h += i.command ? 19 : 9;
    return {m.x, 43, m.x + 232, 43 + h};
}

bool App::enabled(int command) const {
    if (command == Undo)
        return !editor.undoStack.empty();
    if (command == Redo)
        return !editor.redoStack.empty();
    if (command == Copy || command == Cut || command == Delete)
        return editor.selected();
    if (command == Paste)
        return IsClipboardFormatAvailable(CF_UNICODETEXT) != 0;
    return true;
}

void App::drawMenu(HDC dc) {
    if (activeMenu < 0)
        return;
    RECT r = menuRect();
    bevel(dc, r);
    SelectObject(dc, uiFont);
    int y = r.top + 3, index = 0;
    for (const auto& i : menus[activeMenu].items) {
        if (!i.command) {
            line(dc, r.left + 3, y + 3, r.right - 3, y + 3, shadow);
            line(dc, r.left + 3, y + 4, r.right - 3, y + 4, light);
            y += 9;
            ++index;
            continue;
        }
        RECT item = {r.left + 3, y, r.right - 3, y + 19};
        bool hot = hover == index;
        bool on = enabled(i.command);
        if (hot)
            fill(dc, item, blue);
        COLORREF color = hot ? light : (on ? dark : shadow);
        RECT label = item;
        label.left += 23;
        RECT key = item;
        key.right -= 12;
        if (!on && !hot) {
            RECT embossedLabel = label, embossedKey = key;
            OffsetRect(&embossedLabel, 1, 1);
            OffsetRect(&embossedKey, 1, 1);
            text(dc, i.label, embossedLabel, light);
            text(dc, i.shortcut, embossedKey, light, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        }
        text(dc, i.label, label, on ? color : shadow);
        text(dc, i.shortcut, key, on ? color : shadow, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        if (i.command == Wrap && wrap) {
            int x = item.left + 7;
            line(dc, x, y + 12, x + 3, y + 15, color);
            line(dc, x + 3, y + 15, x + 9, y + 7, color);
        }
        y += 19;
        ++index;
    }
}

} // namespace retronp
