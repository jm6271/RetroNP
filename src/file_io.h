#pragma once
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace retronp {

// Encoding is a property of the opened document, independent of the editor UI.
// UTF-16 input of either byte order is saved as little-endian UTF-16.
struct TextEncoding {
    bool utf16 = false;
    bool utf8bom = false;
};

struct LoadedDocument {
    std::filesystem::path path;
    std::wstring text;
    TextEncoding encoding;
};

// Expected file failures carry a displayable explanation. Unexpected filesystem
// exceptions are translated to a generic Open/Save message by the controller.
class FileError : public std::runtime_error {
  public:
    explicit FileError(std::wstring explanation)
        : std::runtime_error("Text file operation failed"), explanation(std::move(explanation)) {}

    std::wstring explanation;
};

// Reads without changing application state. A caller commits only after success.
LoadedDocument readDocument(const std::filesystem::path& filename);

// Expects LF-normalized text; writes CRLF to a temporary sibling, flushes it,
// then replaces the target. Returns the absolute path only after success.
std::filesystem::path writeDocument(const std::filesystem::path& filename, const std::wstring& text,
                                    TextEncoding encoding);

} // namespace retronp
