#include "app.h"
#include <filesystem>

namespace retronp {

// Menu geometry is part of the fixed 96-DPI pixel design. A zero command
// represents a separator; ampersands mark keyboard mnemonics.
App::App()
    : menus{{L"&File",
             6,
             32,
             {{L"&New", L"Ctrl+N", New},
              {L"&Open...", L"Ctrl+O", Open},
              {L"&Save", L"Ctrl+S", Save},
              {L"Save &As...", L"Ctrl+Shift+S", SaveAs},
              {L"", L"", 0},
              {L"E&xit", L"Alt+F4", Exit}}},
            {L"&Edit",
             38,
             34,
             {{L"&Undo", L"Ctrl+Z", Undo},
              {L"&Redo", L"Ctrl+Y", Redo},
              {L"", L"", 0},
              {L"Cu&t", L"Ctrl+X", Cut},
              {L"&Copy", L"Ctrl+C", Copy},
              {L"&Paste", L"Ctrl+V", Paste},
              {L"De&lete", L"Del", Delete},
              {L"", L"", 0},
              {L"Select &All", L"Ctrl+A", SelectAll},
              {L"Time/&Date", L"F5", TimeDate},
              {L"", L"", 0},
              {L"&Word Wrap", L"", Wrap}}},
            {L"&Search", 72, 50, {{L"&Find...", L"Ctrl+F", Find}, {L"Find &Next", L"F3", FindNext}}},
            {L"&Help", 122, 36, {{L"&About RetroNP", L"", About}}}} {}

RECT App::editorRect() const {
    return {6, 46, width - 24, height - 24};
}

int App::columns() const {
    auto r = editorRect();
    return std::max<int>(1, (r.right - r.left - 6) / characterWidth);
}

int App::visibleRows() const {
    auto r = editorRect();
    return std::max<int>(1, (r.bottom - r.top - 4) / lineHeight);
}

int App::columnOf(size_t start, size_t end) const {
    int n = 0;
    for (size_t p = start; p < end; p = editor.next(p))
        n += editor.text[p] == L'\t' ? 8 - n % 8 : 1;
    return n;
}

int App::rowOf(size_t p) const {
    // At a soft-wrap boundary the caret belongs to the following visual row.
    // At a physical newline it stays at the end of the preceding row.
    for (size_t i = 0; i < rows.size(); ++i)
        if (p < rows[i].end || (p == rows[i].end && (i + 1 == rows.size() || rows[i + 1].start != p)))
            return static_cast<int>(i);
    return static_cast<int>(rows.size()) - 1;
}

int App::maxColumns() const {
    int n = 0;
    for (auto r : rows)
        n = std::max(n, columnOf(r.start, r.end));
    return n;
}

size_t App::positionAtColumn(int row, int column) const {
    // Choose the nearest side of a character cell. A tab spans several cells,
    // but still maps to a single document position.
    auto r = rows[std::clamp(row, 0, static_cast<int>(rows.size()) - 1)];
    int c = 0;
    for (size_t p = r.start; p < r.end; p = editor.next(p)) {
        int n = editor.text[p] == L'\t' ? 8 - c % 8 : 1;
        if (column < c + (n + 1) / 2)
            return p;
        c += n;
    }
    return r.end;
}

void App::layout() {
    // Reflow first, then clamp scrolling to the newly computed content size.
    rows = editor.rows(columns(), wrap);
    top = std::clamp(top, 0, std::max(0, static_cast<int>(rows.size()) - visibleRows()));
    left = wrap ? 0 : std::clamp(left, 0, std::max(0, maxColumns() - columns() + 1));
}

void App::reveal() {
    layout();
    int row = rowOf(editor.caret), c = columnOf(rows[row].start, editor.caret);
    if (row < top)
        top = row;
    if (row >= top + visibleRows())
        top = row - visibleRows() + 1;
    if (!wrap) {
        if (c < left)
            left = c;
        if (c >= left + columns())
            left = c - columns() + 1;
    }
    blink = true;
    invalidate();
}

void App::edited() {
    // Vertical movement keeps a preferred column; editing starts a new sequence.
    preferredColumn = -1;
    reveal();
    updateTitle();
}

void App::updateTitle() {
    auto name = path.empty() ? L"Untitled" : std::filesystem::path(path).filename().wstring();
    auto title = name + (editor.dirty() ? L" *" : L"") + L" - RetroNP";
    if (title != windowTitle) {
        SetWindowTextW(hwnd, title.c_str());
        windowTitle = title;
    }
}

App::~App() {
    releaseCanvas();
    if (uiFont)
        DeleteObject(uiFont);
    if (titleFont)
        DeleteObject(titleFont);
    if (textFont)
        DeleteObject(textFont);
    for (auto& file : privateFonts)
        RemoveFontResourceExW(file.c_str(), FR_PRIVATE, nullptr);
}

} // namespace retronp
