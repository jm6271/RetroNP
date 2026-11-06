#include "app.h"

namespace retronp {

void App::keyEditor(Editor& e, WPARAM key, bool ctrl, bool shift, bool single) {
    // Shared editing shortcuts work in both the document and path/search field.
    // The caller handles vertical navigation, which needs visual row layout.
    if (ctrl) {
        if (key == 'A') {
            e.anchor = 0;
            e.caret = e.text.size();
        } else if (key == 'C')
            copy(e);
        else if (key == 'X') {
            copy(e);
            if (e.selected())
                e.replace(L"");
        } else if (key == 'V')
            paste(e, single);
        else if (key == 'Z') {
            if (shift)
                e.redo();
            else
                e.undo();
        } else if (key == 'Y')
            e.redo();
        else if (key == VK_LEFT)
            e.move(e.word(e.caret, -1), shift);
        else if (key == VK_RIGHT)
            e.move(e.word(e.caret, 1), shift);
        else if (key == VK_HOME)
            e.move(0, shift);
        else if (key == VK_END)
            e.move(e.text.size(), shift);
        else if (key == VK_BACK) {
            if (!e.selected())
                e.anchor = e.word(e.caret, -1);
            e.backspace();
        } else if (key == VK_DELETE) {
            if (!e.selected())
                e.anchor = e.word(e.caret, 1);
            e.del();
        }
    } else {
        if (key == VK_LEFT)
            e.move(e.selected() && !shift ? e.lo() : e.previous(e.caret), shift);
        else if (key == VK_RIGHT)
            e.move(e.selected() && !shift ? e.hi() : e.next(e.caret), shift);
        else if (key == VK_BACK)
            e.backspace();
        else if (key == VK_DELETE)
            e.del();
        else if (key == VK_HOME)
            e.move(e.lineStart(e.caret), shift);
        else if (key == VK_END)
            e.move(e.lineEnd(e.caret), shift);
    }
}

void App::key(WPARAM vk) {
    // Route input to the modal dialog first, then menus, then the document.
    // Some keydowns close a dialog before Windows delivers their WM_CHAR;
    // eatCharacter prevents that queued character from editing the document.
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (dialog != Dialog::None) {
        if (vk == VK_RETURN || ((dialog == Dialog::Confirm || showOverwrite) && (vk == 'Y' || vk == 'N')))
            eatCharacter = true;
        if (showOverwrite) {
            if (vk == 'Y')
                overwriteAnswer(true);
            else if (vk == 'N' || vk == VK_ESCAPE)
                overwriteAnswer(false);
            else if (vk == VK_TAB || vk == VK_LEFT || vk == VK_RIGHT) {
                dialogFocus = dialogFocus == 1 ? 2 : 1;
                invalidate();
            } else if (vk == VK_RETURN)
                overwriteAnswer(dialogFocus == 1);
            return;
        }
        bool field = dialog == Dialog::Open || dialog == Dialog::Save || dialog == Dialog::Find;
        if (vk == VK_ESCAPE)
            cancelDialog();
        else if (vk == VK_RETURN)
            acceptDialog(std::max(0, dialogFocus - 1));
        else if (vk == VK_TAB) {
            int count = static_cast<int>(dialogButtons().size()) + (field ? 1 : 0), base = field ? 0 : 1;
            dialogFocus = base + (dialogFocus - base + (shift ? count - 1 : 1)) % count;
        } else if (dialog == Dialog::Confirm && (vk == 'Y' || vk == 'N'))
            acceptDialog(vk == 'Y' ? 0 : 1);
        else if (dialogFocus == 0 && field)
            keyEditor(input, vk, ctrl, shift, true);
        else if (vk == VK_LEFT || vk == VK_RIGHT) {
            int count = static_cast<int>(dialogButtons().size());
            dialogFocus = 1 + (dialogFocus - 1 + (vk == VK_LEFT ? count - 1 : 1)) % count;
        }
        blink = true;
        invalidate();
        return;
    }
    if (activeMenu >= 0) {
        if (vk == VK_RETURN || (vk >= 32 && vk <= 126))
            eatCharacter = true;
        if (vk == VK_ESCAPE) {
            activeMenu = -1;
            ReleaseCapture();
        } else if (vk == VK_LEFT || vk == VK_RIGHT) {
            activeMenu = (activeMenu + (vk == VK_LEFT ? 3 : 1)) % 4;
            hover = -1;
        } else if (vk == VK_DOWN || vk == VK_UP) {
            auto& items = menus[activeMenu].items;
            int n = static_cast<int>(items.size());
            do {
                hover = (hover + (vk == VK_DOWN ? 1 : n - 1) + n) % n;
            } while (!items[hover].command);
        } else if (vk == VK_RETURN && hover >= 0) {
            int c = menus[activeMenu].items[hover].command;
            if (enabled(c))
                execute(c);
        } else
            for (const auto& i : menus[activeMenu].items) {
                auto p = i.label.find(L'&');
                if (p != std::wstring::npos && towupper(i.label[p + 1]) == static_cast<wint_t>(vk)) {
                    if (enabled(i.command))
                        execute(i.command);
                    break;
                }
            }
        invalidate();
        return;
    }
    if (ctrl) {
        if (vk == 'N') {
            execute(New);
            return;
        }
        if (vk == 'O') {
            execute(Open);
            return;
        }
        if (vk == 'S') {
            execute(shift ? SaveAs : Save);
            return;
        }
        if (vk == 'F') {
            execute(Find);
            return;
        }
    }
    if (vk == VK_F3) {
        execute(FindNext);
        return;
    }
    if (vk == VK_F5) {
        execute(TimeDate);
        return;
    }
    if (vk == VK_F10) {
        activeMenu = 0;
        hover = -1;
        invalidate();
        return;
    }
    // Printable keys, Enter, and Tab are edited by WM_CHAR. Do not paint an
    // extra unchanged frame on WM_KEYDOWN (or on modifier-only key presses).
    bool navigation = vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN || vk == VK_HOME ||
                      vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT;
    bool editKey = vk == VK_BACK || vk == VK_DELETE ||
                   (ctrl && (vk == 'A' || vk == 'C' || vk == 'X' || vk == 'V' || vk == 'Z' || vk == 'Y'));
    if (!navigation && !editKey)
        return;
    layout();
    if (!ctrl && (vk == VK_UP || vk == VK_DOWN || vk == VK_PRIOR || vk == VK_NEXT)) {
        int row = rowOf(editor.caret);
        if (preferredColumn < 0)
            preferredColumn = columnOf(rows[row].start, editor.caret);
        int amount = vk == VK_UP ? -1 : vk == VK_DOWN ? 1 : vk == VK_PRIOR ? -visibleRows() : visibleRows();
        editor.move(positionAtColumn(row + amount, preferredColumn), shift);
        reveal();
        return;
    }
    if (!ctrl && (vk == VK_HOME || vk == VK_END)) {
        auto r = rows[rowOf(editor.caret)];
        editor.move(vk == VK_HOME ? r.start : r.end, shift);
    } else
        keyEditor(editor, vk, ctrl, shift);
    edited();
}

void App::character(wchar_t c) {
    // Text insertion happens here rather than on WM_KEYDOWN, preserving the
    // keyboard layout's translated character and avoiding duplicate inserts.
    if (eatCharacter) {
        eatCharacter = false;
        return;
    }
    if (GetKeyState(VK_CONTROL) & 0x8000 || activeMenu >= 0)
        return;
    if (dialog != Dialog::None) {
        if (showOverwrite)
            return;
        if (dialogFocus == 0 &&
            (dialog == Dialog::Open || dialog == Dialog::Save || dialog == Dialog::Find) && c >= 32) {
            input.replace(std::wstring(1, c));
            blink = true;
            invalidate();
        }
    } else if (c >= 32 || c == L'\r' || c == L'\t') {
        editor.replace(c == L'\r' ? L"\n" : std::wstring(1, c));
        edited();
    }
}

} // namespace retronp
