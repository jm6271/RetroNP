#include "graphics.h"

namespace retronp {

HICON createNotepadIcon() {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 16;
    info.bmiHeader.biHeight = -16;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color)
        return nullptr;
    auto pixels = static_cast<DWORD*>(bits);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            char p = notepadPixels[y][x];
            pixels[y * 16 + x] = p == ' ' ? 0 : p == 'W' ? 0xFFFFFFFF : p == 'G' ? 0xFF808080 : 0xFF000000;
        }
    WORD maskBits[16]{};
    HBITMAP mask = CreateBitmap(16, 16, 1, 1, maskBits);
    ICONINFO icon{};
    icon.fIcon = TRUE;
    icon.hbmColor = color;
    icon.hbmMask = mask;
    HICON result = CreateIconIndirect(&icon);
    DeleteObject(color);
    DeleteObject(mask);
    return result;
}
HBITMAP pixelBitmap(HDC reference, int width, int height) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    return CreateDIBSection(reference, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
}

void FrameBuffer::release() {
    if (dc && original)
        SelectObject(dc, original);
    if (bitmap)
        DeleteObject(bitmap);
    if (dc)
        DeleteDC(dc);
    dc = nullptr;
    bitmap = nullptr;
    original = nullptr;
    width = height = 0;
}

bool FrameBuffer::ensure(HDC reference, int w, int h) {
    if (dc && w == width && h == height)
        return true;
    release();
    dc = CreateCompatibleDC(reference);
    if (!dc)
        return false;
    bitmap = pixelBitmap(reference, w, h);
    if (!bitmap) {
        release();
        return false;
    }
    original = SelectObject(dc, bitmap);
    width = w;
    height = h;
    return true;
}

FrameBuffer::~FrameBuffer() {
    release();
}

} // namespace retronp
