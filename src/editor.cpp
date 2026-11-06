#include "editor.h"
#include <utility>

namespace {
constexpr size_t MaxUndoSnapshots = 200;
}

bool Editor::dirty() const {
    return text != saved;
}

size_t Editor::lo() const {
    return std::min(caret, anchor);
}

size_t Editor::hi() const {
    return std::max(caret, anchor);
}

bool Editor::selected() const {
    return caret != anchor;
}

std::wstring Editor::selection() const {
    return text.substr(lo(), hi() - lo());
}

std::wstring Editor::normalize(const std::wstring& s) {
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\r') {
            out += L'\n';
            if (i + 1 < s.size() && s[i + 1] == L'\n')
                ++i;
        } else if (s[i] != 0)
            out += s[i];
    }
    return out;
}

void Editor::reset(const std::wstring& s) {
    text = normalize(s);
    saved = text;
    caret = anchor = 0;
    undoStack.clear();
    redoStack.clear();
}

size_t Editor::previous(size_t p) const {
    if (!p)
        return 0;
    --p;
    if (p && text[p] >= 0xDC00 && text[p] <= 0xDFFF && text[p - 1] >= 0xD800 && text[p - 1] <= 0xDBFF)
        --p;
    return p;
}

size_t Editor::next(size_t p) const {
    if (p >= text.size())
        return text.size();
    if (text[p] >= 0xD800 && text[p] <= 0xDBFF && p + 1 < text.size() && text[p + 1] >= 0xDC00 &&
        text[p + 1] <= 0xDFFF)
        ++p;
    return p + 1;
}

void Editor::checkpoint() {
    undoStack.push_back({text, caret, anchor});
    if (undoStack.size() > MaxUndoSnapshots)
        undoStack.erase(undoStack.begin());
    redoStack.clear();
}

void Editor::replace(const std::wstring& value) {
    auto s = normalize(value);
    if (!selected() && s.empty())
        return;
    checkpoint();
    size_t start = lo();
    text.replace(start, hi() - start, s);
    caret = anchor = start + s.size();
}

void Editor::backspace() {
    if (!selected() && caret)
        anchor = previous(caret);
    if (selected())
        replace(L"");
}

void Editor::del() {
    if (!selected())
        anchor = next(caret);
    if (selected())
        replace(L"");
}

bool Editor::undo() {
    return restore(undoStack, redoStack);
}

bool Editor::redo() {
    return restore(redoStack, undoStack);
}

void Editor::move(size_t p, bool extend) {
    caret = std::min(p, text.size());
    if (!extend)
        anchor = caret;
}

size_t Editor::word(size_t p, int direction) const {
    if (direction < 0) {
        while (p && iswspace(text[previous(p)]))
            p = previous(p);
        while (p && !iswspace(text[previous(p)]))
            p = previous(p);
    } else {
        while (p < text.size() && !iswspace(text[p]))
            p = next(p);
        while (p < text.size() && iswspace(text[p]))
            p = next(p);
    }
    return p;
}

size_t Editor::lineStart(size_t p) const {
    auto at = p ? text.rfind(L'\n', p - 1) : std::wstring::npos;
    return at == std::wstring::npos ? 0 : at + 1;
}

size_t Editor::lineEnd(size_t p) const {
    auto at = text.find(L'\n', p);
    return at == std::wstring::npos ? text.size() : at;
}

std::vector<Row> Editor::rows(int width, bool wrap) const {
    std::vector<Row> result;
    size_t start = 0;
    width = std::max(1, width);
    while (true) {
        size_t end = lineEnd(start);
        if (wrap) {
            while (end - start > static_cast<size_t>(width)) {
                size_t cut = start, columns = 0, space = start;
                while (cut < end) {
                    size_t n = text[cut] == L'\t' ? 8 - columns % 8 : 1;
                    if (columns + n > static_cast<size_t>(width) && cut > start)
                        break;
                    columns += n;
                    cut = next(cut);
                    if (iswspace(text[cut - 1]))
                        space = cut;
                    if (columns >= static_cast<size_t>(width))
                        break;
                }
                if (cut >= end)
                    break;
                if (space > start)
                    cut = space;
                result.push_back({start, cut});
                start = cut;
            }
            // Tabs can occupy more columns than their string length.
            if (start < end) {
                size_t cut = start;
                int column = 0;
                while (cut < end) {
                    int n = text[cut] == L'\t' ? 8 - column % 8 : 1;
                    if (column + n > width && cut > start) {
                        result.push_back({start, cut});
                        start = cut;
                        column = 0;
                        continue;
                    }
                    column += n;
                    cut = next(cut);
                }
            }
        }
        result.push_back({start, end});
        if (end == text.size())
            break;
        start = end + 1;
    }
    return result;
}

bool Editor::find(const std::wstring& needle, bool matchCase) {
    if (needle.empty())
        return false;
    std::wstring hay = text, query = needle;
    if (!matchCase) {
        for (auto& c : hay)
            c = static_cast<wchar_t>(towlower(c));
        for (auto& c : query)
            c = static_cast<wchar_t>(towlower(c));
    }
    size_t at = hay.find(query, hi());
    if (at == std::wstring::npos)
        at = hay.find(query);
    if (at == std::wstring::npos)
        return false;
    anchor = at;
    caret = at + needle.size();
    return true;
}

bool Editor::restore(std::vector<Snapshot>& from, std::vector<Snapshot>& to) {
    if (from.empty())
        return false;
    to.push_back({text, caret, anchor});
    auto s = std::move(from.back());
    from.pop_back();
    text = std::move(s.text);
    caret = s.caret;
    anchor = s.anchor;
    return true;
}
