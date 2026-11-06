#include "app.h"
#include <filesystem>

namespace retronp {

void App::drawCanvas(HDC dc) {
    // Draw back-to-front into the logical image; modal dialogs cover menus.
    bevel(dc, {0, 0, width, height});
    line(dc, 2, 2, width - 2, 2, face);
    auto name = path.empty() ? L"Untitled" : std::filesystem::path(path).filename().wstring();
    title(dc, {4, 4, width - 4, 23}, L"", focused && dialog == Dialog::None);
    icon(dc, 6, 5);
    SelectObject(dc, titleFont);
    text(dc, name + (editor.dirty() ? L" *" : L"") + L" - RetroNP", {25, 4, width - 61, 23}, light,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    drawTitleButtons(dc);
    SelectObject(dc, uiFont);
    for (size_t i = 0; i < menus.size(); ++i) {
        auto& m = menus[i];
        RECT r = {m.x, 24, m.x + m.width, 43};
        bool active = activeMenu == static_cast<int>(i);
        if (active)
            fill(dc, r, blue);
        text(dc, m.label, r, active ? light : dark, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    layout();
    drawEditor(dc);
    drawMenu(dc);
    drawDialog(dc);
}

void App::invalidate() {
    // Content changes invalidate the cached logical image. Caret blinking
    // deliberately bypasses this path and only recomposes the final frame.
    canvasDirty = true;
    if (hwnd)
        InvalidateRect(hwnd, nullptr, FALSE);
}

void App::invalidate(RECT logical) {
    canvasDirty = true;
    if (hwnd) {
        RECT client{};
        GetClientRect(hwnd, &client);
        RECT rect = deviceRect(logical, client);
        InvalidateRect(hwnd, &rect, FALSE);
    }
}

RECT App::caretBounds() const {
    RECT caret{}, clip{};
    if (!focused || activeMenu >= 0 || showOverwrite)
        return caret;
    if (dialog == Dialog::None) {
        if (rows.empty())
            return caret;
        clip = editorRect();
        int row = rowOf(editor.caret);
        int x = clip.left + 3 + (columnOf(rows[row].start, editor.caret) - left) * characterWidth,
            y = clip.top + 2 + (row - top) * lineHeight;
        caret = {x, y, x + 1, y + lineHeight};
    } else if (dialogFocus == 0 &&
               (dialog == Dialog::Open || dialog == Dialog::Save || dialog == Dialog::Find)) {
        clip = inputRect();
        InflateRect(&clip, -3, -3);
        int visible = std::max<int>(1, (clip.right - clip.left) / characterWidth);
        int offset = std::max(0, static_cast<int>(input.caret) - visible + 1);
        int x = clip.left + (static_cast<int>(input.caret) - offset) * characterWidth;
        caret = {x, clip.top, x + 1, clip.top + lineHeight};
    }
    RECT result{};
    IntersectRect(&result, &caret, &clip);
    return result;
}

RECT App::deviceRect(RECT logical, RECT client) const {
    // Round outward to cover every destination pixel of the logical caret.
    int w = std::max(1, width), h = std::max(1, height);
    return {logical.left * client.right / w, logical.top * client.bottom / h,
            (logical.right * client.right + w - 1) / w, (logical.bottom * client.bottom + h - 1) / h};
}

void App::blinkCaret() {
    RECT caret = caretBounds();
    if (IsRectEmpty(&caret))
        return;
    blink = !blink;
    RECT client{};
    GetClientRect(hwnd, &client);
    caret = deviceRect(caret, client);
    // No document reflow, resource allocation, or full-window invalidation.
    InvalidateRect(hwnd, &caret, FALSE);
}

void App::releaseCanvas() {
    if (canvas && originalBitmap)
        SelectObject(canvas, originalBitmap);
    if (canvasBitmap)
        DeleteObject(canvasBitmap);
    if (canvas)
        DeleteDC(canvas);
    canvas = nullptr;
    canvasBitmap = nullptr;
    originalBitmap = nullptr;
    canvasWidth = canvasHeight = 0;
}

bool App::present(HDC target, RECT client) {
    // Two surfaces serve different lifetimes: canvas caches logical content,
    // while frame holds the device-sized image plus the current caret state.
    if (width <= 0 || height <= 0 || client.right <= 0 || client.bottom <= 0)
        return false;
    if (!canvas || canvasWidth != width || canvasHeight != height) {
        releaseCanvas();
        canvas = CreateCompatibleDC(target);
        if (!canvas)
            return false;
        canvasBitmap = pixelBitmap(target, width, height);
        if (!canvasBitmap) {
            releaseCanvas();
            return false;
        }
        originalBitmap = SelectObject(canvas, canvasBitmap);
        canvasWidth = width;
        canvasHeight = height;
        canvasDirty = true;
    }
    if (canvasDirty) {
        int savedState = SaveDC(canvas);
        drawCanvas(canvas);
        RestoreDC(canvas, savedState);
        canvasDirty = false;
    }
    // Complete scaling and caret composition offscreen. The window DC sees
    // exactly one finished, device-sized image, never separate drawing steps.
    if (!frame.ensure(target, client.right, client.bottom))
        return false;
    SetStretchBltMode(frame.dc, COLORONCOLOR);
    StretchBlt(frame.dc, 0, 0, client.right, client.bottom, canvas, 0, 0, width, height, SRCCOPY);
    RECT caret = caretBounds();
    if (blink && !IsRectEmpty(&caret))
        fill(frame.dc, deviceRect(caret, client), dark);
    GdiFlush();
    // BeginPaint's clip preserves partial updates (caret/menu highlights).
    BitBlt(target, 0, 0, client.right, client.bottom, frame.dc, 0, 0, SRCCOPY);
    GdiFlush();
    return true;
}

void App::paint() {
    PAINTSTRUCT ps{};
    HDC target = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);
    present(target, client);
    EndPaint(hwnd, &ps);
}

} // namespace retronp
