#include "file_io.h"
#include <fstream>
#include <iterator>
#include <windows.h>

namespace retronp {
namespace {
constexpr uintmax_t MaxDocumentBytes = 16 * 1024 * 1024;

std::wstring systemError() {
    wchar_t* buffer = nullptr;
    DWORD code = GetLastError();
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring s = buffer ? buffer : L"The operation could not be completed.";
    if (buffer)
        LocalFree(buffer);
    return s;
}

std::string utf8(const std::wstring& s) {
    int n =
        WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    if (n)
        WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr,
                            nullptr);
    return out;
}

std::wstring decode(const std::string& s, UINT codepage, DWORD flags) {
    int n = MultiByteToWideChar(codepage, flags, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n == 0 && !s.empty())
        throw std::runtime_error("Invalid text encoding");
    std::wstring out(n, L'\0');
    if (n)
        MultiByteToWideChar(codepage, flags, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

} // namespace

LoadedDocument readDocument(const std::filesystem::path& filename) {
    // Prefer explicit BOMs, then strict UTF-8. Legacy ANSI fallback is allowed
    // only when a UTF-8 BOM has not promised a particular encoding.
    auto resolved = std::filesystem::absolute(filename);
    if (std::filesystem::file_size(resolved) > MaxDocumentBytes) {
        throw FileError(L"This file is too large. RetroNP supports text files up to 16 MB.");
    }
    std::ifstream file(resolved, std::ios::binary);
    if (!file)
        throw std::runtime_error("open failed");
    std::string bytes((std::istreambuf_iterator<char>(file)), {});
    if (file.bad())
        throw std::runtime_error("read failed");
    bool is16 = bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF &&
                static_cast<unsigned char>(bytes[1]) == 0xFE;
    bool big16 = bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFE &&
                 static_cast<unsigned char>(bytes[1]) == 0xFF;
    bool bom = bytes.size() >= 3 && bytes.compare(0, 3, "\xEF\xBB\xBF") == 0;
    std::wstring content;
    if (is16 || big16) {
        if ((bytes.size() - 2) % 2)
            throw std::runtime_error("Invalid UTF-16");
        for (size_t i = 2; i < bytes.size(); i += 2) {
            auto a = static_cast<unsigned char>(bytes[i]), b = static_cast<unsigned char>(bytes[i + 1]);
            content += static_cast<wchar_t>(big16 ? (a << 8) | b : a | (b << 8));
        }
    } else {
        if (bom)
            bytes.erase(0, 3);
        try {
            content = decode(bytes, CP_UTF8, MB_ERR_INVALID_CHARS);
        } catch (...) {
            if (bom)
                throw;
            content = decode(bytes, CP_ACP, 0);
        }
    }
    if (content.find(L'\0') != std::wstring::npos) {
        throw FileError(L"This appears to be a binary file. Please open a plain text file.");
    }
    return {resolved, content, {is16 || big16, bom}};
}

std::filesystem::path writeDocument(const std::filesystem::path& filename, const std::wstring& text,
                                    TextEncoding encoding) {
    std::filesystem::path temp;
    bool ownsTemporaryFile = false;
    try {
        auto resolved = std::filesystem::absolute(filename);
        temp = resolved;
        temp += L".retronp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(GetTickCount64()) + L".tmp";
        std::wstring content;
        for (auto c : text) {
            if (c == L'\n')
                content += L'\r';
            content += c;
        }
        std::string bytes;
        if (encoding.utf16) {
            bytes = "\xFF\xFE";
            for (wchar_t c : content) {
                bytes += static_cast<char>(c & 255);
                bytes += static_cast<char>((c >> 8) & 255);
            }
        } else {
            bytes = encoding.utf8bom ? "\xEF\xBB\xBF" : "";
            bytes += utf8(content);
        }
        HANDLE file =
            CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            throw FileError(L"Could not create a temporary file.\n" + systemError());
        }
        ownsTemporaryFile = true;
        // Flush and close before replacing the destination. On any failure the
        // caller keeps its dirty document, and this service removes its temp file.
        DWORD count = 0;
        bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) != 0 &&
                  count == bytes.size();
        if (ok)
            ok = FlushFileBuffers(file) != 0;
        CloseHandle(file);
        if (!ok || !MoveFileExW(temp.c_str(), resolved.c_str(),
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            auto error = systemError();
            throw FileError(L"Could not save the file.\n" + error);
        }
        return resolved;
    } catch (...) {
        if (ownsTemporaryFile)
            DeleteFileW(temp.c_str());
        throw;
    }
}

} // namespace retronp
