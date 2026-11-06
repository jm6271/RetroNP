#pragma once
#include <windows.h>

namespace retronp {
constexpr COLORREF face = RGB(192, 192, 192), light = RGB(255, 255, 255), shadow = RGB(128, 128, 128),
                   dark = RGB(0, 0, 0), blue = RGB(0, 0, 128);
// One pixel grid is shared by the caption, About dialog, and Windows app icon.
constexpr const char* notepadPixels[] = {
    "   ###########  ", "  #W#W#W#W#WWW# ", " ##W#W#W#W#WWW# ", " #WWWWWWWWWWWW# ",
    " #WWWWWWWWWWWW#G", " #WW########WW#G", " #WWWWWWWWWWWW#G", " #WW########WW#G",
    " #WWWWWWWWWWWW#G", " #WW########WW#G", " #WWWWWWWWWWWW#G", " #WW########WW#G",
    " #WWWWWWWWWWWW#G", " #WWWWWWWWWWWW#G", " ##############G", "  GGGGGGGGGGGGGG"};

HICON createNotepadIcon();
HBITMAP pixelBitmap(HDC reference, int width, int height);

// Owns a device-sized offscreen image. Selected bitmaps must be restored
// before deletion; copying this owner would double-release GDI resources.
struct FrameBuffer {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ original = nullptr;
    int width = 0, height = 0;
    FrameBuffer() = default;
    FrameBuffer(const FrameBuffer&) = delete;
    FrameBuffer& operator=(const FrameBuffer&) = delete;
    void release();
    bool ensure(HDC reference, int w, int h);
    ~FrameBuffer();
};

} // namespace retronp
