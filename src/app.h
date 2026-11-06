#pragma once
#include "editor.h"
#include "graphics.h"
#include <algorithm>
#include <string>
#include <vector>
#include <windows.h>

namespace retronp {

enum Command {
    New = 1,
    Open,
    Save,
    SaveAs,
    Exit,
    Undo,
    Redo,
    Cut,
    Copy,
    Paste,
    Delete,
    SelectAll,
    TimeDate,
    Wrap,
    Find,
    FindNext,
    About
};
enum class Dialog {
    None,
    Open,
    Save,
    Find,
    Confirm,
    Message,
    About
};
enum class Pending {
    None,
    New,
    Open,
    Exit,
    Drop
};
struct Item {
    std::wstring label, shortcut;
    int command;
};
struct Menu {
    std::wstring label;
    int x, width;
    std::vector<Item> items;
};
struct Scroll {
    RECT track{}, thumb{}, before{}, after{};
    int max = 0, pageSize = 1; // Extent in rows or columns, depending on orientation.
};

// Coordinates and font metrics are logical pixels at 96 DPI.
// The window adapter converts input; the presenter scales the final image.
struct App {
    App();
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // App is the shared controller for one window. The window adapter and
    // integration tests access this state; Editor and file_io have no App dependency.
    HWND hwnd = nullptr; // Borrowed handle; Win32 owns the window lifetime.

    // Document and its on-disk identity. input is a separate editor used only
    // by the active dialog, so dialog edits never enter document undo history.
    Editor editor, input;
    std::wstring path;
    bool utf16 = false;
    bool utf8bom = false;
    bool wrap = false;
    std::wstring windowTitle;

    // Viewport and layout cache. top/left are scroll offsets in visual rows
    // and columns; document offsets remain UTF-16 code-unit positions.
    int width = 960, height = 640, dpi = 96;
    int characterWidth = 8, lineHeight = 16;
    int top = 0, left = 0;
    int preferredColumn = -1; // Preserved across repeated Up/Down movements.
    std::vector<Row> rows;
    Scroll vertical, horizontal;

    // Input routing: a dialog takes priority over a menu, then the document.
    bool focused = true;
    bool blink = true;
    bool eatCharacter = false; // Suppress WM_CHAR queued after a dialog keydown.
    bool selecting = false, inputSelecting = false;
    int activeMenu = -1, hover = -1, titlePress = -1; // -1 means no active item.
    int scrollDrag = 0;                               // 0: none, 1: vertical thumb, 2: horizontal thumb.
    int dragOffset = 0;
    std::vector<Menu> menus;

    // pending survives confirmation and Save As until saving/discard completes.
    Dialog dialog = Dialog::None;
    Pending pending = Pending::None;
    int dialogFocus = 0; // Field: 0; buttons: 1 through button count.
    std::wstring droppedPath, query, message, messageTitle, overwritePath;
    bool showOverwrite = false;

    // GDI resources belong to this controller. The logical canvas is cached
    // without the caret; frame composes its scaled pixels and the blinking caret.
    HFONT uiFont = nullptr, titleFont = nullptr, textFont = nullptr;
    std::vector<std::wstring> privateFonts;
    HDC canvas = nullptr;
    HBITMAP canvasBitmap = nullptr;
    HGDIOBJ originalBitmap = nullptr;
    int canvasWidth = 0, canvasHeight = 0;
    bool canvasDirty = true;
    FrameBuffer frame;

    // app.cpp: viewport geometry, document-to-screen mapping, and lifetime.
    RECT editorRect() const;
    int columns() const;
    int visibleRows() const;
    int columnOf(size_t start, size_t end) const;
    int rowOf(size_t p) const;
    int maxColumns() const;
    size_t positionAtColumn(int row, int column) const;
    void layout();
    void reveal();
    void edited();
    void updateTitle();
    // rendering.cpp: bitmap fonts, drawing primitives, editor, and menus.
    void fonts();
    static void fill(HDC dc, RECT r, COLORREF color);
    static void line(HDC dc, int x1, int y1, int x2, int y2, COLORREF color);
    static void bevel(HDC dc, RECT r, bool down = false);
    void text(HDC dc, const std::wstring& s, RECT r, COLORREF color = dark,
              UINT flags = DT_LEFT | DT_SINGLELINE | DT_VCENTER) const;
    static bool inside(RECT r, int x, int y);
    RECT titleButton(int i) const;
    void icon(HDC dc, int x, int y);
    void title(HDC dc, RECT r, const std::wstring& name, bool active);
    void button(HDC dc, RECT r, const std::wstring& label, bool pressed = false, bool focus = false);
    void drawTitleButtons(HDC dc);
    Scroll makeScroll(RECT r, bool vert, int pos, int max, int visible, int total);
    void drawScroll(HDC dc, Scroll& s, bool vert);
    void drawEditor(HDC dc);
    RECT menuRect() const;
    bool enabled(int command) const;
    void drawMenu(HDC dc);
    // dialog_rendering.cpp: modal geometry and drawing.
    RECT dialogRect() const;
    RECT inputRect() const;
    std::vector<std::wstring> dialogButtons() const;
    RECT dialogButton(int i) const;
    void drawDialog(HDC dc);
    void drawOverwrite(HDC dc);
    // presenter.cpp: cached images, scaling, caret composition, and WM_PAINT.
    void drawCanvas(HDC dc);
    void invalidate();
    void invalidate(RECT logical);
    RECT caretBounds() const;
    RECT deviceRect(RECT logical, RECT client) const;
    void blinkCaret();
    void releaseCanvas();
    bool present(HDC target, RECT client);
    void paint();
    // dialogs.cpp: modal state transitions and deferred-action decisions.
    void cancelDialog();
    void acceptDialog(int button);
    void overwriteAnswer(bool yes);
    void show(Dialog kind, const std::wstring& value = L"");
    void info(const std::wstring& body, const std::wstring& title = L"RetroNP");
    // document_commands.cpp: application actions and clipboard integration.
    void load(const std::wstring& filename);
    bool write(const std::wstring& filename);
    void continuePending();
    void request(Pending action);
    void save(bool as = false);
    void copy(Editor& e);
    void paste(Editor& e, bool single = false);
    void findNext();
    void execute(int command);
    // keyboard.cpp: key routing and text insertion.
    void keyEditor(Editor& e, WPARAM key, bool ctrl, bool shift, bool single = false);
    void key(WPARAM vk);
    void character(wchar_t c);
    // mouse.cpp: hit testing, capture, selection, and scrolling.
    int menuHit(int x, int y) const;
    size_t hitText(int x, int y) const;
    size_t hitInput(int x) const;
    bool scrollClick(Scroll& s, bool vert, int x, int y);
    void mouseDown(int x, int y, bool doubleClick = false);
    void mouseMove(int x, int y);
    void mouseUp(int x, int y);
};

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);

} // namespace retronp
