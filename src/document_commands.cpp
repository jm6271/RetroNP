#include "app.h"
#include "file_io.h"
#include <cstring>

namespace retronp {

void App::load(const std::wstring& filename) {
    try {
        // Commit only after decoding and validation succeed. Failed opens must
        // leave the current document, path, encoding, and undo history intact.
        const auto document = readDocument(filename);
        editor.reset(document.text);
        path = document.path.wstring();
        utf16 = document.encoding.utf16;
        utf8bom = document.encoding.utf8bom;
        top = left = 0;
        updateTitle();
        reveal();
    } catch (const FileError& error) {
        info(error.explanation, L"Open");
    } catch (...) {
        info(L"Could not read the file. Check the path, file permissions, and text encoding.", L"Open");
    }
}

bool App::write(const std::wstring& filename) {
    try {
        auto resolved = writeDocument(filename, editor.text, {utf16, utf8bom});
        // A failed write never changes the saved snapshot or current path.
        path = resolved.wstring();
        editor.saved = editor.text;
        updateTitle();
        return true;
    } catch (const FileError& error) {
        info(error.explanation, L"Save");
    } catch (...) {
        info(L"Could not save the file. Check the file name and folder permissions.", L"Save");
    }
    return false;
}

void App::continuePending() {
    // Consume the deferred action before executing it. Open/Save dialogs may
    // start another interaction, and must not accidentally repeat this action.
    Pending action = pending;
    pending = Pending::None;
    if (action == Pending::Exit)
        DestroyWindow(hwnd);
    else if (action == Pending::New) {
        editor.reset();
        path.clear();
        utf16 = utf8bom = false;
        top = left = 0;
        updateTitle();
        reveal();
    } else if (action == Pending::Open)
        show(Dialog::Open);
    else if (action == Pending::Drop)
        load(droppedPath);
}

void App::request(Pending action) {
    // New/Open/Drop/Exit all share the same unsaved-changes workflow.
    // The action resumes only after a successful save or an explicit discard.
    pending = action;
    if (editor.dirty()) {
        show(Dialog::Confirm);
        dialogFocus = 1;
    } else
        continuePending();
}

void App::save(bool as) {
    if (path.empty() || as) {
        show(Dialog::Save, path.empty() ? L"Untitled.txt" : path);
        return;
    }
    if (write(path))
        continuePending();
    else
        pending = Pending::None;
}

void App::copy(Editor& e) {
    // Clipboard text uses Windows CRLF; document storage uses LF.
    if (!e.selected() || !OpenClipboard(hwnd))
        return;
    EmptyClipboard();
    std::wstring s;
    for (auto c : e.selection()) {
        if (c == L'\n')
            s += L'\r';
        s += c;
    }
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
    if (memory) {
        void* data = GlobalLock(memory);
        if (data) {
            memcpy(data, s.c_str(), (s.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(memory);
            // On success the clipboard owns memory. Free it only on failure.
            if (!SetClipboardData(CF_UNICODETEXT, memory))
                GlobalFree(memory);
        } else
            GlobalFree(memory);
    }
    CloseClipboard();
}

void App::paste(Editor& e, bool single) {
    if (!OpenClipboard(hwnd))
        return;
    HANDLE memory = GetClipboardData(CF_UNICODETEXT);
    if (memory) {
        const wchar_t* data = static_cast<const wchar_t*>(GlobalLock(memory));
        if (data) {
            std::wstring s = data;
            GlobalUnlock(memory);
            if (single)
                for (auto& c : s)
                    if (c == L'\r' || c == L'\n' || c == L'\t')
                        c = L' ';
            e.replace(s);
        }
    }
    CloseClipboard();
}

void App::findNext() {
    if (query.empty())
        show(Dialog::Find);
    else if (!editor.find(query, false))
        info(L"Cannot find \"" + query + L"\".", L"Find");
    else
        reveal();
}

void App::execute(int command) {
    activeMenu = -1;
    hover = -1;
    ReleaseCapture();
    invalidate();
    switch (command) {
    case New:
        request(Pending::New);
        break;
    case Open:
        request(Pending::Open);
        break;
    case Save:
        save();
        break;
    case SaveAs:
        save(true);
        break;
    case Exit:
        request(Pending::Exit);
        break;
    case Undo:
        editor.undo();
        edited();
        break;
    case Redo:
        editor.redo();
        edited();
        break;
    case Copy:
        copy(editor);
        break;
    case Cut:
        copy(editor);
        editor.replace(L"");
        edited();
        break;
    case Paste:
        paste(editor);
        edited();
        break;
    case Delete:
        editor.del();
        edited();
        break;
    case SelectAll:
        editor.anchor = 0;
        editor.caret = editor.text.size();
        reveal();
        break;
    case Wrap:
        wrap = !wrap;
        left = 0;
        preferredColumn = -1;
        reveal();
        break;
    case Find:
        show(Dialog::Find, editor.selected() ? editor.selection() : query);
        break;
    case FindNext:
        findNext();
        break;
    case About:
        show(Dialog::About);
        dialogFocus = 1;
        break;
    case TimeDate: {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        wchar_t time[80]{}, date[80]{};
        GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &now, nullptr, time, 80);
        GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &now, nullptr, date, 80);
        editor.replace(std::wstring(time) + L" " + date);
        edited();
        break;
    }
    }
}

} // namespace retronp
