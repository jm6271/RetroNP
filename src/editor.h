#pragma once
#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <string>
#include <vector>

struct Snapshot {
    std::wstring text;
    size_t caret, anchor;
};
struct Row {
    // Half-open offsets [start, end), excluding the physical newline. Wrapped
    // rows can meet at the same offset without a newline between them.
    size_t start, end;
};

// The document is independent of Win32 and stores normalized LF line endings.
// Positions are offsets into the wide string (UTF-16 code units on Windows),
// not screen columns. Use previous()/next() to avoid splitting surrogate pairs.
class Editor {
  public:
    // Keep the saved text rather than a dirty flag so undo can return to clean.
    std::wstring text, saved;
    // The caret is the moving selection endpoint; anchor is the fixed endpoint.
    size_t caret = 0, anchor = 0;
    std::vector<Snapshot> undoStack, redoStack;
    // Selection bounds are ordered regardless of the direction of a drag.
    bool dirty() const;
    size_t lo() const;
    size_t hi() const;
    bool selected() const;
    std::wstring selection() const;
    static std::wstring normalize(const std::wstring& s);
    void reset(const std::wstring& s = L"");
    size_t previous(size_t p) const;
    size_t next(size_t p) const;
    // Each replacement is one undo step. A new edit discards redo history.
    void checkpoint();
    void replace(const std::wstring& value);
    void backspace();
    void del();
    bool undo();
    bool redo();
    void move(size_t p, bool extend);
    size_t word(size_t p, int direction) const;
    size_t lineStart(size_t p) const;
    size_t lineEnd(size_t p) const;
    // Always returns at least one visual row, even for an empty document.
    // Width is measured in monospace columns; tabs use eight-column stops.
    std::vector<Row> rows(int width, bool wrap) const;
    // Searches after the selection, wrapping once to the document beginning.
    bool find(const std::wstring& needle, bool matchCase);

  private:
    bool restore(std::vector<Snapshot>& from, std::vector<Snapshot>& to);
};
