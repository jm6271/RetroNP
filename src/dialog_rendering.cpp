#include "app.h"
#include <filesystem>

namespace retronp {

RECT App::dialogRect() const {
    int w = std::min(560, width - 24), h = (dialog == Dialog::Confirm   ? 152
                                            : dialog == Dialog::About   ? 214
                                            : dialog == Dialog::Message ? 190
                                                                        : 168);
    return {(width - w) / 2, (height - h) / 2, (width + w) / 2, (height + h) / 2};
}

RECT App::inputRect() const {
    auto r = dialogRect();
    return {r.left + 16, r.top + 68, r.right - 16, r.top + 93};
}

std::vector<std::wstring> App::dialogButtons() const {
    if (dialog == Dialog::Confirm)
        return {L"&Yes", L"&No", L"Cancel"};
    if (dialog == Dialog::Message || dialog == Dialog::About)
        return {L"OK"};
    return {dialog == Dialog::Find ? L"Find &Next" : dialog == Dialog::Open ? L"&Open" : L"&Save", L"Cancel"};
}

RECT App::dialogButton(int i) const {
    auto r = dialogRect();
    int count = static_cast<int>(dialogButtons().size());
    int x = r.right - 16 - (count - i) * 88;
    return {x, r.bottom - 39, x + 80, r.bottom - 15};
}

void App::drawDialog(HDC dc) {
    if (dialog == Dialog::None)
        return;
    if (showOverwrite) {
        drawOverwrite(dc);
        return;
    }
    RECT r = dialogRect();
    bevel(dc, r);
    std::wstring dialogTitle = dialog == Dialog::Open      ? L"Open"
                               : dialog == Dialog::Save    ? L"Save As"
                               : dialog == Dialog::Find    ? L"Find"
                               : dialog == Dialog::Confirm ? L"RetroNP"
                               : dialog == Dialog::About   ? L"About RetroNP"
                                                           : messageTitle;
    title(dc, {r.left + 3, r.top + 3, r.right - 3, r.top + 25}, dialogTitle, true);
    SelectObject(dc, uiFont);
    bool field = dialog == Dialog::Open || dialog == Dialog::Save || dialog == Dialog::Find;
    if (field) {
        text(dc,
             dialog == Dialog::Find ? L"Fi&nd what:"
                                    : L"File &name (full path or relative to the working folder):",
             {r.left + 16, r.top + 36, r.right - 16, r.top + 61});
        RECT box = inputRect();
        bevel(dc, box, true);
        InflateRect(&box, -3, -3);
        fill(dc, box, light);
        int old = SaveDC(dc);
        IntersectClipRect(dc, box.left, box.top, box.right, box.bottom);
        SelectObject(dc, textFont);
        int visible = std::max(1, static_cast<int>((box.right - box.left) / characterWidth));
        int offset = std::max(0, static_cast<int>(input.caret) - visible + 1);
        for (size_t p = static_cast<size_t>(offset); p < input.text.size(); ++p) {
            RECT c = {box.left + (static_cast<int>(p) - offset) * characterWidth, box.top,
                      box.left + (static_cast<int>(p) - offset + 1) * characterWidth, box.bottom};
            bool sel = p >= input.lo() && p < input.hi();
            SetBkColor(dc, sel ? blue : light);
            SetTextColor(dc, sel ? light : dark);
            ExtTextOutW(dc, c.left, c.top, ETO_OPAQUE | ETO_CLIPPED, &c, input.text.c_str() + p, 1, nullptr);
        }
        RestoreDC(dc, old);
        SelectObject(dc, uiFont);
        text(dc,
             dialog == Dialog::Find ? L"Search ignores case and wraps to the beginning."
                                    : L"Tip: you can also drag a text file onto RetroNP.",
             {r.left + 16, r.top + 96, r.right - 16, r.top + 116}, shadow);
    } else if (dialog == Dialog::Confirm) {
        std::wstring name = path.empty() ? L"Untitled" : std::filesystem::path(path).filename().wstring();
        text(dc, L"Save changes to \"" + name + L"\"?",
             {r.left + 17, r.top + 40, r.right - 17, r.bottom - 47}, dark,
             DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    } else if (dialog == Dialog::About) {
        icon(dc, r.left + 20, r.top + 46);
        SelectObject(dc, titleFont);
        text(dc, L"RetroNP", {r.left + 49, r.top + 38, r.right - 16, r.top + 66});
        SelectObject(dc, uiFont);
        text(dc,
             L"A little piece of 1995, written in C++.\n\nEvery control is drawn by hand with Win32 GDI.\nNo "
             L"UI frameworks. Just pixels and plain text.\n\nVersion 1.0",
             {r.left + 20, r.top + 76, r.right - 20, r.bottom - 46}, dark, DT_LEFT | DT_WORDBREAK);
    } else
        text(dc, message, {r.left + 17, r.top + 40, r.right - 17, r.bottom - 47}, dark,
             DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    auto buttons = dialogButtons();
    for (size_t i = 0; i < buttons.size(); ++i)
        button(dc, dialogButton(static_cast<int>(i)), buttons[i], false,
               dialogFocus == static_cast<int>(i) + 1);
}

void App::drawOverwrite(HDC dc) {
    RECT r = dialogRect();
    bevel(dc, r);
    title(dc, {r.left + 3, r.top + 3, r.right - 3, r.top + 25}, L"Confirm Save As", true);
    SelectObject(dc, uiFont);
    text(dc, message, {r.left + 17, r.top + 40, r.right - 17, r.bottom - 47}, dark,
         DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    button(dc, {r.right - 192, r.bottom - 39, r.right - 112, r.bottom - 15}, L"&Yes", false,
           dialogFocus == 1);
    button(dc, {r.right - 104, r.bottom - 39, r.right - 24, r.bottom - 15}, L"&No", false, dialogFocus == 2);
}

} // namespace retronp
